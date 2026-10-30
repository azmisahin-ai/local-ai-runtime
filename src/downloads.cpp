#include "lar/downloads.hpp"
#include "lar/catalog.hpp"
#include "lar/diagnostics.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lar {

struct DownloadManager::Job {
    std::string id;
    std::string item_id;
    std::string kind;
    std::string state{"queued"};
    std::string phase{"Queued"};
    std::string version;
    std::string installed_path;
    std::string error;
    std::string last_error;
    std::string source_url;
    std::string expected_sha256;
    std::string asset_name;
    std::string error_code;
    std::uint64_t expected_size{0};
    std::uint64_t downloaded_bytes{0};
    std::uint64_t total_bytes{0};
    std::uint64_t created_at{0};
    std::uint64_t updated_at{0};
    std::uint64_t started_at{0};
    std::uint64_t finished_at{0};
    std::uint64_t attempt{1};
    std::uint64_t last_persisted_bytes{0};
    std::chrono::steady_clock::time_point last_persisted_at{};
    bool license_accepted{false};
    std::atomic<bool> cancel_requested{false};
    std::function<void()> persist_callback;
    mutable std::mutex mutex;
};

namespace {
namespace pt = boost::property_tree;

constexpr char kGitHubReleasesUrl[] = "https://api.github.com/repos/ggml-org/llama.cpp/releases?per_page=30";
constexpr char kGitHubAssetPrefix[] = "https://github.com/ggml-org/llama.cpp/releases/download/";
constexpr std::uint64_t kMaximumArchiveExpandedBytes = 3ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumReleaseAssetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumRuntimeUpdateBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumOutstandingInstallJobs = 16;

const catalog::ComponentDefinition& pinned_model_component(std::string_view id) {
    const auto* component = catalog::find_component(id);
    if (!component || !component->installable || component->installer != "pinned-gguf" ||
        component->kind != "model") {
        throw std::runtime_error("The requested model is not present in the compiled reviewed catalog manifest.");
    }
    return *component;
}

std::once_flag curl_init_flag;
CURLcode curl_init_result = CURLE_FAILED_INIT;

void ensure_curl_initialized() {
    std::call_once(curl_init_flag, [] { curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (curl_init_result != CURLE_OK) throw std::runtime_error("Could not initialize HTTPS download support.");
}

std::string json_quote(std::string_view value) {
    std::ostringstream out;
    out << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20U) out << "\\u00" << hex[(ch >> 4U) & 0x0fU] << hex[ch & 0x0fU];
                else out << static_cast<char>(ch);
        }
    }
    out << '"';
    return out.str();
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(encoded.begin(), encoded.end());
}

std::filesystem::path path_from_utf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char ch : value) encoded.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(encoded);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

struct ParsedVersion {
    std::uint64_t major{0};
    std::uint64_t minor{0};
    std::uint64_t patch{0};
    std::string prerelease;
};

std::optional<ParsedVersion> parse_version(std::string value) {
    if (!value.empty() && value.front() == 'v') value.erase(value.begin());
    static const std::regex pattern(R"(^([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9A-Za-z.-]+))?$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) return std::nullopt;
    try {
        return ParsedVersion{std::stoull(match[1].str()), std::stoull(match[2].str()),
                             std::stoull(match[3].str()), match[4].matched ? match[4].str() : std::string{}};
    } catch (...) { return std::nullopt; }
}

int prerelease_channel_rank(const std::string& value) {
    const auto channel = lower_ascii(value.substr(0, value.find('.')));
    if (channel == "dev" || channel.rfind("dev-", 0) == 0) return 0;
    if (channel == "alpha" || channel.rfind("alpha-", 0) == 0) return 1;
    if (channel == "beta" || channel.rfind("beta-", 0) == 0) return 2;
    if (channel == "rc" || channel.rfind("rc-", 0) == 0) return 3;
    return 0;
}

bool is_newer_version(const std::string& candidate_text, const std::string& current_text) {
    const auto candidate = parse_version(candidate_text);
    const auto current = parse_version(current_text);
    if (!candidate || !current) return false;
    if (candidate->major != current->major) return candidate->major > current->major;
    if (candidate->minor != current->minor) return candidate->minor > current->minor;
    if (candidate->patch != current->patch) return candidate->patch > current->patch;
    if (candidate->prerelease.empty()) return !current->prerelease.empty();
    if (current->prerelease.empty()) return false;
    const int candidate_rank = prerelease_channel_rank(candidate->prerelease);
    const int current_rank = prerelease_channel_rank(current->prerelease);
    if (candidate_rank != current_rank) return candidate_rank > current_rank;
    return candidate->prerelease != current->prerelease &&
           candidate->prerelease.compare(current->prerelease) > 0;
}

bool is_sha256_digest(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

bool contains(const std::string& value, std::string_view token) {
    return value.find(token) != std::string::npos;
}

bool is_hex_digest(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

std::string host_os() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unsupported";
#endif
}

std::string host_architecture() {
#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#else
    return "unknown";
#endif
}

struct JobTransferContext {
    std::ofstream* output{nullptr};
    std::function<void(std::uint64_t, std::uint64_t)> progress;
    std::function<bool()> cancelled;
    std::string write_error;
    std::uint64_t received{0};
    std::uint64_t maximum_bytes{0};
};

std::size_t write_file_callback(char* data, std::size_t size, std::size_t count, void* user_data) {
    auto* context = static_cast<JobTransferContext*>(user_data);
    const auto bytes = size * count;
    if (context->cancelled && context->cancelled()) return 0;
    if (context->maximum_bytes > 0 &&
        (context->received > context->maximum_bytes || bytes > context->maximum_bytes - context->received)) {
        context->write_error = "Downloaded data exceeded the catalogued maximum size; the partial file was discarded.";
        return 0;
    }
    context->output->write(data, static_cast<std::streamsize>(bytes));
    if (!*context->output) {
        context->write_error = "Could not write the downloaded file to disk (check free disk space and permissions).";
        return 0;
    }
    context->received += static_cast<std::uint64_t>(bytes);
    if (context->progress) context->progress(context->received, 0);
    return bytes;
}

int progress_callback(void* user_data, curl_off_t download_total, curl_off_t download_now,
                      curl_off_t, curl_off_t) {
    auto* context = static_cast<JobTransferContext*>(user_data);
    if (context->cancelled && context->cancelled()) return 1;
    if (context->progress) {
        const auto total = download_total > 0 ? static_cast<std::uint64_t>(download_total) : 0ULL;
        const auto now = download_now > 0 ? static_cast<std::uint64_t>(download_now) : context->received;
        context->progress(now, total);
    }
    return 0;
}

void configure_https_only(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 90L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Local-AI-Runtime/0.1 (verified catalog downloader)");
}

struct BoundedTextResponse {
    std::string body;
    std::size_t maximum_bytes{0};
};

std::size_t write_bounded_text_callback(char* data, std::size_t size, std::size_t count, void* user_data) {
    auto* response = static_cast<BoundedTextResponse*>(user_data);
    const auto bytes = size * count;
    if (response->body.size() > response->maximum_bytes || bytes > response->maximum_bytes - response->body.size()) return 0;
    response->body.append(data, bytes);
    return bytes;
}

std::string fetch_https_text(const std::string& url, std::size_t maximum_bytes) {
    if (url.rfind("https://", 0) != 0) throw std::runtime_error("Refusing a non-HTTPS catalog request.");
    ensure_curl_initialized();
    CURL* raw = curl_easy_init();
    if (!raw) throw std::runtime_error("Could not create HTTPS request.");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(raw, curl_easy_cleanup);
    BoundedTextResponse response;
    response.maximum_bytes = maximum_bytes;
    response.body.reserve(std::min<std::size_t>(maximum_bytes, 64 * 1024));
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    configure_https_only(curl.get());
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_bounded_text_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    const auto result = curl_easy_perform(curl.get());
    if (result != CURLE_OK) throw std::runtime_error(std::string("Could not read official llama.cpp release metadata: ") + curl_easy_strerror(result));
    long response_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code < 200 || response_code >= 300) throw std::runtime_error("Official release metadata returned HTTP " + std::to_string(response_code) + ".");
    if (response.body.empty() || response.body.size() > maximum_bytes) throw std::runtime_error("Official release metadata response is empty or too large.");
    return response.body;
}

struct ReleaseAsset {
    std::string tag;
    std::string name;
    std::string url;
    std::string sha256;
    std::uint64_t size{0};
    bool prerelease{false};
};

bool archive_asset_extension(const std::string& name) {
    return name.size() >= 4 && (name.substr(name.size() - 4) == ".zip" ||
        (name.size() >= 7 && name.substr(name.size() - 7) == ".tar.gz") ||
        (name.size() >= 4 && name.substr(name.size() - 4) == ".tgz"));
}

bool is_supported_engine_asset_name_for_host(const std::string& raw_name, const std::string& variant,
                                             const std::string& os, const std::string& architecture) {
    const auto name = lower_ascii(raw_name);
    // The upstream CUDA runtime/DLL archives intentionally share architecture
    // and backend tokens with the server binaries. Never treat those as a
    // standalone llama-server package.
    if (name.rfind("cudart-", 0) == 0 || !archive_asset_extension(name) ||
        contains(name, "debug") || contains(name, "source")) return false;

    if (architecture == "x64") {
        if (!contains(name, "x64") || contains(name, "arm64") || contains(name, "aarch64") || contains(name, "s390x")) return false;
    } else if (architecture == "arm64") {
        if (!contains(name, "arm64") && !contains(name, "aarch64")) return false;
    } else {
        return false;
    }

    if (os == "windows") {
        if (!contains(name, "win")) return false;
        if (variant == "cpu") return contains(name, "cpu") && !contains(name, "cuda") && !contains(name, "vulkan") && !contains(name, "rocm");
        if (architecture != "x64") return false;
        if (variant == "cuda12") return contains(name, "-bin-win-cuda-12.4-x64.zip");
        if (variant == "cuda13") return contains(name, "-bin-win-cuda-13.4-x64.zip");
        return false;
    }
    if (os == "linux") {
        if (!contains(name, "ubuntu") && !contains(name, "linux")) return false;
        if (variant == "cpu") {
            return !contains(name, "cuda") && !contains(name, "vulkan") && !contains(name, "rocm") &&
                   !contains(name, "openvino") && !contains(name, "sycl") && !contains(name, "metal");
        }
        if (architecture != "x64") return false;
        if (variant == "cuda12") return contains(name, "-cuda-12.8-x64.tar.gz");
        if (variant == "cuda13") return contains(name, "-cuda-13.4-x64.tar.gz");
        return false;
    }
    if (os == "macos") {
        if (!contains(name, "macos") && !contains(name, "mac")) return false;
        // Current macOS upstream bundles are native Apple CPU/GPU builds. The asset
        // must match this process architecture; GPU-specific CUDA variants are absent.
        return variant == "cpu" && !contains(name, "ios");
    }
    return false;
}

bool is_supported_cuda_runtime_asset_name_for_host(const std::string& raw_name, const std::string& variant,
                                                  const std::string& os, const std::string& architecture) {
    const auto name = lower_ascii(raw_name);
    if (architecture != "x64" || name.rfind("cudart-llama-", 0) != 0 ||
        !archive_asset_extension(name) || contains(name, "debug") || contains(name, "source")) return false;
    if (os == "windows") {
        if (variant == "cuda12") return contains(name, "-bin-win-cuda-12.4-x64.zip");
        if (variant == "cuda13") return contains(name, "-bin-win-cuda-13.4-x64.zip");
    } else if (os == "linux") {
        if (variant == "cuda12") return contains(name, "-bin-ubuntu-cuda-12.8-x64.tar.gz") || contains(name, "-bin-linux-cuda-12.8-x64.tar.gz");
        if (variant == "cuda13") return contains(name, "-bin-ubuntu-cuda-13.4-x64.tar.gz") || contains(name, "-bin-linux-cuda-13.4-x64.tar.gz");
    }
    return false;
}

struct ResolvedEngineAssets {
    ReleaseAsset engine;
    std::optional<ReleaseAsset> cuda_runtime;
    std::uint64_t total_size() const noexcept {
        return engine.size + (cuda_runtime ? cuda_runtime->size : 0U);
    }
};

std::optional<ReleaseAsset> verified_release_asset(const pt::ptree& node, const std::string& tag, bool prerelease) {
    const auto name = node.get<std::string>("name", "");
    const auto url = node.get<std::string>("browser_download_url", "");
    if (url.rfind(kGitHubAssetPrefix, 0) != 0) {
        throw std::runtime_error("The selected llama.cpp asset URL is outside the official GitHub release namespace.");
    }
    auto digest = node.get<std::string>("digest", "");
    if (digest.rfind("sha256:", 0) == 0) digest = digest.substr(7);
    if (!is_hex_digest(digest)) return std::nullopt;
    const auto size = node.get<std::uint64_t>("size", 0);
    if (size == 0 || size > kMaximumReleaseAssetBytes) return std::nullopt;
    return ReleaseAsset{tag, name, url, lower_ascii(digest), size, prerelease};
}

std::optional<ReleaseAsset> choose_cuda_runtime_asset(std::vector<ReleaseAsset> candidates,
                                                      const std::string& tag,
                                                      const std::string& variant,
                                                      const std::string& os) {
    if (candidates.empty()) return std::nullopt;
    // Windows publishes a stable, untagged companion filename. Linux companions
    // are tied to the same bNNNNN release as the matching server archive.
    const auto expected_suffix = os == "windows"
        ? (variant == "cuda12" ? std::string("cudart-llama-bin-win-cuda-12.4-x64.zip")
                                : std::string("cudart-llama-bin-win-cuda-13.4-x64.zip"))
        : std::string("cudart-llama-") + tag + (variant == "cuda12"
              ? "-bin-ubuntu-cuda-12.8-x64.tar.gz" : "-bin-ubuntu-cuda-13.4-x64.tar.gz");
    const auto exact = std::find_if(candidates.begin(), candidates.end(), [&](const ReleaseAsset& asset) {
        return lower_ascii(asset.name) == expected_suffix;
    });
    if (exact != candidates.end()) return *exact;

    // Some upstream release workflows may switch between tagged/untagged naming.
    // Accept a unique verified candidate only; ambiguity must still fail closed.
    if (candidates.size() == 1) return candidates.front();
    throw std::runtime_error("More than one verified CUDA runtime archive matched release " + tag + "; refusing an ambiguous dependency selection.");
}

ResolvedEngineAssets resolve_engine_assets(const std::string& variant) {
    if (!catalog::is_known_engine_variant(variant)) {
        throw std::runtime_error("The requested engine variant is not present in the compiled reviewed catalog manifest.");
    }
    // CUDA releases ship two distinct files: the llama-server binary and an
    // official companion archive containing CUDA runtime DLLs/libraries. Both
    // must be pinned to official release metadata and verified before activation.
    const auto body = fetch_https_text(kGitHubReleasesUrl, 12U * 1024U * 1024U);
    std::istringstream input(body);
    pt::ptree releases;
    try { pt::read_json(input, releases); }
    catch (const std::exception& ex) { throw std::runtime_error(std::string("Could not parse the official llama.cpp releases metadata: ") + ex.what()); }
    if (releases.empty()) throw std::runtime_error("The official llama.cpp releases endpoint returned no releases.");

    const auto os = host_os();
    const auto architecture = host_architecture();
    std::optional<ResolvedEngineAssets> stable_match;
    std::optional<ResolvedEngineAssets> prerelease_match;
    bool saw_engine_without_cuda_runtime = false;
    for (const auto& release_entry : releases) {
        const auto& release = release_entry.second;
        if (release.get<bool>("draft", false)) continue;
        const bool prerelease = release.get<bool>("prerelease", false);
        const auto tag = release.get<std::string>("tag_name", "");
        if (tag.empty() || tag.size() > 64 ||
            !std::all_of(tag.begin(), tag.end(), [](unsigned char c) {
                return std::isalnum(c) != 0 || c == '.' || c == '-' || c == 'v';
            })) continue;
        const auto assets = release.get_child_optional("assets");
        if (!assets) continue;

        std::vector<ReleaseAsset> engine_candidates;
        std::vector<ReleaseAsset> cuda_runtime_candidates;
        for (const auto& entry : *assets) {
            const auto& node = entry.second;
            const auto name = node.get<std::string>("name", "");
            if (catalog::is_supported_engine_archive(name, variant, os, architecture)) {
                if (auto verified = verified_release_asset(node, tag, prerelease)) engine_candidates.push_back(std::move(*verified));
            }
            if (catalog::is_supported_cuda_runtime_archive(name, variant, os, architecture)) {
                if (auto verified = verified_release_asset(node, tag, prerelease)) cuda_runtime_candidates.push_back(std::move(*verified));
            }
        }
        if (engine_candidates.empty()) continue;
        if (engine_candidates.size() > 1) {
            throw std::runtime_error("More than one verified llama-server archive matched this platform/backend in release " + tag + "; refusing an ambiguous package selection.");
        }

        ResolvedEngineAssets candidate{engine_candidates.front(), std::nullopt};
        if (variant == "cuda12" || variant == "cuda13") {
            candidate.cuda_runtime = choose_cuda_runtime_asset(std::move(cuda_runtime_candidates), tag, variant, os);
            if (!candidate.cuda_runtime) {
                saw_engine_without_cuda_runtime = true;
                continue; // A server without its matching CUDA runtime is not installable.
            }
        }
        auto& preferred = prerelease ? prerelease_match : stable_match;
        if (!preferred) preferred = std::move(candidate);
    }

    if (stable_match) return *stable_match;
    if (prerelease_match) return *prerelease_match;
    if (saw_engine_without_cuda_runtime) {
        throw std::runtime_error("An official llama.cpp CUDA server archive was found, but no matching verified CUDA runtime/DLL archive was available for this release. Nothing was installed; refresh the catalog and try a newer official release later.");
    }
    throw std::runtime_error("No verified official llama.cpp release archive with a published SHA-256 matches this OS, architecture and backend. Stable releases may not include prebuilt binaries; check the official releases page for supported packages.");
}

bool has_gguf_header(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    char magic[4]{};
    input.read(magic, 4);
    const bool matches = input.gcount() == 4 && std::string_view(magic, 4) == "GGUF";
    input.close(); // Windows may refuse rename/delete while this handle remains open.
    return matches;
}

void download_to_file(const std::string& url, const std::filesystem::path& destination,
                      std::uint64_t expected_size, const std::shared_ptr<DownloadManager::Job>& job,
                      std::uint64_t progress_offset = 0, std::uint64_t aggregate_total = 0);

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::filesystem::path::const_iterator root_it;
    std::filesystem::path::const_iterator candidate_it;
    const auto root_path = root.lexically_normal();
    const auto candidate_path = candidate.lexically_normal();
    root_it = root_path.begin();
    candidate_it = candidate_path.begin();
    for (; root_it != root_path.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate_path.end() || *root_it != *candidate_it) return false;
    }
    return true;
}

void extract_archive_safely(const std::filesystem::path& archive_path,
                            const std::filesystem::path& destination,
                            const std::shared_ptr<DownloadManager::Job>& job) {
    std::error_code fs_error;
    std::filesystem::create_directories(destination, fs_error);
    if (fs_error) throw std::runtime_error("Could not create the engine staging directory: " + fs_error.message());
    struct ArchiveDeleter { void operator()(archive* value) const { if (value) archive_read_free(value); } };
    std::unique_ptr<archive, ArchiveDeleter> reader(archive_read_new());
    if (!reader) throw std::runtime_error("Could not allocate the archive reader.");
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());
#ifdef _WIN32
    const auto open_result = archive_read_open_filename_w(reader.get(), archive_path.c_str(), 64 * 1024);
#else
    const auto open_result = archive_read_open_filename(reader.get(), archive_path.c_str(), 64 * 1024);
#endif
    if (open_result != ARCHIVE_OK) throw std::runtime_error(std::string("Could not open downloaded engine archive: ") + archive_error_string(reader.get()));

    std::uint64_t total_written = 0;
    std::array<char, 128 * 1024> buffer{};
    archive_entry* entry = nullptr;
    while (true) {
        if (job->cancel_requested.load()) throw std::runtime_error("Download canceled by the user.");
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) break;
        if (status < ARCHIVE_WARN) throw std::runtime_error(std::string("Invalid engine archive: ") + archive_error_string(reader.get()));
        const char* entry_name = archive_entry_pathname_utf8(entry);
        if (!entry_name) entry_name = archive_entry_pathname(entry);
        if (!entry_name || !DownloadManager::is_safe_archive_entry_path(entry_name)) {
            throw std::runtime_error("The engine archive contains an unsafe path; extraction was stopped.");
        }
        const std::filesystem::path relative = path_from_utf8(entry_name);
        const auto output_path = (destination / relative).lexically_normal();
        if (!path_is_within(destination, output_path)) throw std::runtime_error("The engine archive tries to write outside its staging directory.");
        const auto type = archive_entry_filetype(entry);
        if (type == AE_IFDIR) {
            std::filesystem::create_directories(output_path, fs_error);
            if (fs_error) throw std::runtime_error("Could not create an extracted directory: " + fs_error.message());
            archive_read_data_skip(reader.get());
            continue;
        }
        if (type != AE_IFREG || archive_entry_symlink(entry) != nullptr || archive_entry_hardlink(entry) != nullptr) {
            throw std::runtime_error("The engine archive contains a link or special file; only regular files and directories are allowed.");
        }
        const auto declared_size = archive_entry_size(entry);
        if (declared_size < 0 || static_cast<std::uint64_t>(declared_size) > kMaximumArchiveExpandedBytes - total_written) {
            throw std::runtime_error("The engine archive expands beyond the 3 GiB safety limit.");
        }
        std::filesystem::create_directories(output_path.parent_path(), fs_error);
        if (fs_error) throw std::runtime_error("Could not create an extracted file directory: " + fs_error.message());
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Could not create an extracted engine file.");
        while (true) {
            if (job->cancel_requested.load()) throw std::runtime_error("Download canceled by the user.");
            const auto count = archive_read_data(reader.get(), buffer.data(), buffer.size());
            if (count == 0) break;
            if (count < 0) throw std::runtime_error(std::string("Could not read engine archive contents: ") + archive_error_string(reader.get()));
            output.write(buffer.data(), static_cast<std::streamsize>(count));
            if (!output) throw std::runtime_error("Could not write extracted engine file (check free disk space).");
            total_written += static_cast<std::uint64_t>(count);
            if (total_written > kMaximumArchiveExpandedBytes) throw std::runtime_error("The engine archive expands beyond the 3 GiB safety limit.");
        }
        output.flush();
        if (!output) throw std::runtime_error("Failed writing an extracted engine file.");
#ifndef _WIN32
        const auto permissions = static_cast<unsigned int>(archive_entry_perm(entry));
        auto perms = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                     std::filesystem::perms::group_read | std::filesystem::perms::others_read;
        if ((permissions & 0111U) != 0U) perms |= std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec;
        std::filesystem::permissions(output_path, perms, std::filesystem::perm_options::replace, fs_error);
        fs_error.clear();
#endif
    }
}

std::optional<std::filesystem::path> find_llama_server(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root)) return std::nullopt;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (error) { error.clear(); continue; }
        const auto status = it->symlink_status(error);
        if (error) { error.clear(); continue; }
        if (!std::filesystem::is_regular_file(status)) continue;
        const auto filename = lower_ascii(it->path().filename().string());
#ifdef _WIN32
        if (filename == "llama-server.exe" || filename == "llama-server") return it->path();
#else
        if (filename == "llama-server") return it->path();
#endif
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> find_installed_llama_variant(const std::filesystem::path& engine_root,
                                                                  const std::string& variant) {
    std::error_code error;
    if (!std::filesystem::is_directory(engine_root, error) || error) return std::nullopt;
    std::optional<std::filesystem::path> selected;
    std::filesystem::file_time_type selected_time{};
    const auto suffix = std::string("-") + variant;
    for (std::filesystem::directory_iterator it(engine_root, error), end; it != end && !error; it.increment(error)) {
        if (!it->is_directory(error) || error) { error.clear(); continue; }
        const auto name = it->path().filename().string();
        if (name.size() < suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const auto server = find_llama_server(it->path());
        if (!server) continue;
        const auto write_time = it->last_write_time(error);
        if (error) { error.clear(); continue; }
        if (!selected || write_time > selected_time) {
            selected = it->path();
            selected_time = write_time;
        }
    }
    return selected;
}

std::string infer_engine_component_id(const EngineConfig& engine) {
    const auto path = lower_ascii(engine.executable_path.generic_string());
    for (const auto& component : catalog::components()) {
        if (component.kind != "engine" || component.installer != "llama-release") continue;
        const auto suffix = std::string("-") + component.variant;
        if (path.find(suffix + "/") != std::string::npos || path.find(suffix + "\\") != std::string::npos) return component.id;
    }
    return {};
}

std::string infer_model_component_id(const EngineConfig& engine) {
    if (engine.model_path.empty()) return {};
    const auto filename = lower_ascii(engine.model_path.filename().string());
    for (const auto& component : catalog::components()) {
        if (component.kind == "model" && component.installer == "pinned-gguf" &&
            lower_ascii(component.filename) == filename) return component.id;
    }
    return {};
}

std::uint16_t configured_engine_port(const EngineConfig& engine) {
    const auto colon = engine.upstream_base_url.find_last_of(':');
    if (colon == std::string::npos || colon + 1 >= engine.upstream_base_url.size()) return 8081;
    try {
        const auto parsed = std::stoul(engine.upstream_base_url.substr(colon + 1));
        if (parsed >= 1024 && parsed <= 65535) return static_cast<std::uint16_t>(parsed);
    } catch (...) {}
    return 8081;
}

unsigned int configured_context_size(const EngineConfig& engine) {
    for (std::size_t i = 0; i + 1 < engine.args.size(); ++i) {
        if (engine.args[i] == "--ctx-size") {
            try { return static_cast<unsigned int>(std::stoul(engine.args[i + 1])); } catch (...) { return 2048; }
        }
    }
    return 2048;
}

std::string configured_gpu_layers(const EngineConfig& engine) {
    for (std::size_t i = 0; i + 1 < engine.args.size(); ++i) {
        if (engine.args[i] == "--n-gpu-layers") return engine.args[i + 1];
    }
    return "0";
}

bool any_engine_running(EngineManager& engines) {
    for (const auto& status : engines.statuses()) if (status.state == ProcessState::running) return true;
    return false;
}

} // namespace

namespace {

std::uint64_t unix_time_millis() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool is_active_job_state(std::string_view state) {
    return state == "queued" || state == "running" || state == "downloading" || state == "verifying" ||
           state == "extracting" || state == "configuring";
}

bool is_terminal_job_state(std::string_view state) {
    return state == "succeeded" || state == "failed" || state == "canceled" || state == "interrupted";
}

bool is_retryable_job(const DownloadManager::Job& job) {
    if (job.state == "succeeded" || job.state == "queued" || job.state == "downloading" ||
        job.state == "verifying" || job.state == "extracting" || job.state == "configuring") return false;
    if (job.item_id == "runtime-update") {
        if (job.kind != "update") return false;
        const auto expected_asset = DownloadManager::runtime_update_asset_name();
        const auto expected_url = "https://github.com/azmisahin-ai/local-ai-runtime/releases/download/" +
                                  job.version + "/" + expected_asset;
        return !expected_asset.empty() && job.asset_name == expected_asset && job.source_url == expected_url &&
               DownloadManager::is_newer_release_tag(job.version, "0.0.0") &&
               is_sha256_digest(job.expected_sha256) && job.expected_size >= 1024 &&
               job.expected_size <= kMaximumRuntimeUpdateBytes;
    }
    if (!job.license_accepted) return false;
    const auto* component = catalog::find_component(job.item_id);
    if (!component || !component->installable || component->kind != job.kind) return false;
    return catalog::supports_target(*component, host_os(), host_architecture());
}

void mutate_job(const std::shared_ptr<DownloadManager::Job>& job,
                const std::function<void(DownloadManager::Job&)>& fn) {
    std::function<void()> persist;
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        const auto previous_state = job->state;
        const auto previous_phase = job->phase;
        const auto previous_bytes = job->downloaded_bytes;
        fn(*job);
        job->updated_at = unix_time_millis();
        const auto now = std::chrono::steady_clock::now();
        const bool state_changed = previous_state != job->state || previous_phase != job->phase;
        const bool progress_checkpoint = job->downloaded_bytes >= job->last_persisted_bytes + 1024ULL * 1024ULL ||
            (previous_bytes != job->downloaded_bytes &&
             now - job->last_persisted_at >= std::chrono::seconds(2));
        if (job->persist_callback && (state_changed || progress_checkpoint || job->last_persisted_at.time_since_epoch().count() == 0)) {
            job->last_persisted_bytes = job->downloaded_bytes;
            job->last_persisted_at = now;
            persist = job->persist_callback;
        }
    }
    if (persist) persist();
}

DownloadJobSnapshot snapshot_job(const std::shared_ptr<DownloadManager::Job>& job) {
    std::lock_guard<std::mutex> lock(job->mutex);
    return {job->id, job->item_id, job->kind, job->state, job->phase, job->version,
            job->installed_path, job->error, job->last_error, job->error_code, job->downloaded_bytes, job->total_bytes,
            job->created_at, job->updated_at, job->started_at, job->finished_at, job->attempt,
            job->cancel_requested.load(), is_retryable_job(*job)};
}

std::map<std::string, std::size_t> build_queue_positions(
    const std::vector<std::shared_ptr<DownloadManager::Job>>& jobs) {
    std::vector<std::pair<std::uint64_t, std::shared_ptr<DownloadManager::Job>>> queued;
    for (const auto& job : jobs) {
        const auto view = snapshot_job(job);
        if (view.state == "queued") queued.emplace_back(view.updated_at, job);
    }
    std::stable_sort(queued.begin(), queued.end(), [](const auto& left, const auto& right) {
        if (left.first != right.first) return left.first < right.first;
        return snapshot_job(left.second).created_at < snapshot_job(right.second).created_at;
    });
    std::map<std::string, std::size_t> result;
    for (std::size_t i = 0; i < queued.size(); ++i) result.emplace(snapshot_job(queued[i].second).id, i + 1U);
    return result;
}

std::string dependency_plan_json(const std::string& item_id) {
    const auto* component = catalog::find_component(item_id);
    if (!component || component->dependencies.empty()) return "[]";
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < component->dependencies.size(); ++i) {
        if (i) out << ',';
        const auto& dependency = component->dependencies[i];
        out << "{\"id\":" << json_quote(dependency.id)
            << ",\"kind\":" << json_quote(dependency.kind)
            << ",\"resolution\":" << json_quote(dependency.resolution) << '}';
    }
    out << ']';
    return out.str();
}

void download_to_file(const std::string& url, const std::filesystem::path& destination,
                      std::uint64_t expected_size, const std::shared_ptr<DownloadManager::Job>& job,
                      std::uint64_t progress_offset, std::uint64_t aggregate_total) {
    if (url.rfind("https://", 0) != 0) throw std::runtime_error("Refusing to download a non-HTTPS URL.");
    ensure_curl_initialized();
    std::error_code fs_error;
    std::filesystem::create_directories(destination.parent_path(), fs_error);
    if (fs_error) throw std::runtime_error("Could not create the download directory: " + fs_error.message());
    auto partial = destination;
    partial += ".part";
    std::filesystem::remove(partial, fs_error);
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not create the partial download file (check free disk space and permissions).");

    CURL* raw = curl_easy_init();
    if (!raw) throw std::runtime_error("Could not create the HTTPS downloader.");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(raw, curl_easy_cleanup);
    JobTransferContext context;
    context.output = &output;
    context.maximum_bytes = expected_size;
    context.cancelled = [job] { return job->cancel_requested.load(); };
    context.progress = [job, progress_offset, aggregate_total](std::uint64_t received, std::uint64_t total) {
        mutate_job(job, [&](DownloadManager::Job& current) {
            current.downloaded_bytes = progress_offset + received;
            if (aggregate_total > 0) current.total_bytes = aggregate_total;
            else if (total > 0) current.total_bytes = total;
        });
    };
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    configure_https_only(curl.get());
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 25L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 0L); // large model downloads may legitimately take a long time
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &context);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_file_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context);
    if (expected_size > 0) curl_easy_setopt(curl.get(), CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(expected_size));
    if (expected_size > 0) mutate_job(job, [&](DownloadManager::Job& current) {
        current.total_bytes = aggregate_total > 0 ? aggregate_total : expected_size;
    });
    const auto result = curl_easy_perform(curl.get());
    long response_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
    output.flush();
    output.close();
    if (job->cancel_requested.load()) {
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Download canceled by the user.");
    }
    if (result != CURLE_OK) {
        std::filesystem::remove(partial, fs_error);
        if (!context.write_error.empty()) throw std::runtime_error(context.write_error);
        throw std::runtime_error(std::string("HTTPS download failed: ") + curl_easy_strerror(result));
    }
    if (response_code < 200 || response_code >= 300) {
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Download source returned HTTP " + std::to_string(response_code) + ".");
    }
    const auto actual_size = std::filesystem::file_size(partial, fs_error);
    if (fs_error || actual_size == 0 || (expected_size > 0 && actual_size != expected_size)) {
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Downloaded file size does not match the catalog metadata.");
    }
    std::filesystem::rename(partial, destination, fs_error);
    if (fs_error) {
        const auto detail = "file=" + destination.filename().string() + " os_error=" + fs_error.message();
        log_diagnostic("ERROR", "download.file_finalize_failed", detail);
        std::error_code remove_error;
        std::filesystem::remove(partial, remove_error);
        throw std::runtime_error("Could not finalize downloaded file: " + fs_error.message());
    }
}

} // namespace

namespace catalog {
bool is_supported_engine_archive(const std::string& name, const std::string& variant,
                                 const std::string& os, const std::string& architecture) {
    return is_known_engine_variant(variant) && is_supported_engine_asset_name_for_host(name, variant, os, architecture);
}
bool is_supported_cuda_runtime_archive(const std::string& name, const std::string& variant,
                                       const std::string& os, const std::string& architecture) {
    return is_known_engine_variant(variant) && is_supported_cuda_runtime_asset_name_for_host(name, variant, os, architecture);
}
} // namespace catalog

DownloadManager::DownloadManager(RuntimeConfig config, EngineManager& engines, QueueTestExecutor test_executor)
    : config_(std::move(config)), engines_(engines), test_executor_(std::move(test_executor)) {
    load_jobs();
    worker_thread_ = std::thread([this] { worker_loop(); });
}

bool DownloadManager::is_newer_release_tag(const std::string& candidate_tag, const std::string& current_version) {
    static const std::regex valid_tag(R"(^v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$)");
    return std::regex_match(candidate_tag, valid_tag) && is_newer_version(candidate_tag, current_version);
}

DownloadManager::~DownloadManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        // Stop only the active transfer. Explicitly queued jobs remain persisted
        // and are resumed in FIFO order the next time the runtime starts.
        if (active_job_) active_job_->cancel_requested.store(true);
    }
    worker_cv_.notify_all();
    if (worker_thread_.joinable()) worker_thread_.join();
}

void DownloadManager::load_jobs() {
    const auto directory = user_data_directory() / "jobs";
    std::error_code fs_error;
    if (!std::filesystem::exists(directory, fs_error)) return;
    if (fs_error || !std::filesystem::is_directory(directory, fs_error)) {
        log_diagnostic("WARN", "jobs.persistence_unavailable", "Could not inspect the persistent job history directory.");
        return;
    }

    std::vector<std::shared_ptr<Job>> loaded;
    for (std::filesystem::directory_iterator it(directory, fs_error), end; !fs_error && it != end; it.increment(fs_error)) {
        if (!it->is_regular_file(fs_error) || fs_error || it->path().extension() != ".json") {
            fs_error.clear();
            continue;
        }
        try {
            std::ifstream input(it->path(), std::ios::binary);
            if (!input) continue;
            pt::ptree record;
            pt::read_json(input, record);
            auto job = std::make_shared<Job>();
            job->id = record.get<std::string>("id", "");
            if (job->id.empty() || job->id.size() > 96 ||
                !std::all_of(job->id.begin(), job->id.end(), [](unsigned char c) {
                    return std::isalnum(c) != 0 || c == '-' || c == '_';
                })) continue;
            if (it->path().filename().string() != job->id + ".json") continue;
            job->item_id = record.get<std::string>("item_id", "");
            job->kind = record.get<std::string>("kind", "component");
            if (job->item_id.empty() || (job->kind != "engine" && job->kind != "model" && job->kind != "update")) continue;
            job->state = record.get<std::string>("state", "interrupted");
            job->phase = record.get<std::string>("phase", "Restored from local job history");
            job->version = record.get<std::string>("version", "");
            job->installed_path = record.get<std::string>("installed_path", "");
            job->error = record.get<std::string>("error", "");
            job->last_error = record.get<std::string>("last_error", "");
            job->error_code = record.get<std::string>("error_code", "");
            job->source_url = record.get<std::string>("source_url", "");
            job->expected_sha256 = record.get<std::string>("expected_sha256", "");
            job->asset_name = record.get<std::string>("asset_name", "");
            job->expected_size = record.get<std::uint64_t>("expected_size", 0);
            job->downloaded_bytes = record.get<std::uint64_t>("downloaded_bytes", 0);
            job->total_bytes = record.get<std::uint64_t>("total_bytes", 0);
            job->created_at = record.get<std::uint64_t>("created_at", 0);
            job->updated_at = record.get<std::uint64_t>("updated_at", job->created_at);
            job->started_at = record.get<std::uint64_t>("started_at", 0);
            job->finished_at = record.get<std::uint64_t>("finished_at", 0);
            job->attempt = std::max<std::uint64_t>(1, record.get<std::uint64_t>("attempt", 1));
            job->license_accepted = record.get<bool>("license_accepted", false);
            job->cancel_requested.store(record.get<bool>("cancel_requested", false));
            if (job->created_at == 0) job->created_at = unix_time_millis();
            if (job->updated_at == 0) job->updated_at = job->created_at;

            if (job->state == "queued") {
                bool safe_to_resume = false;
                if (job->item_id == "runtime-update") {
                    const auto expected_asset = runtime_update_asset_name();
                    const auto expected_url = "https://github.com/azmisahin-ai/local-ai-runtime/releases/download/" +
                                              job->version + "/" + expected_asset;
                    safe_to_resume = job->kind == "update" && !expected_asset.empty() &&
                        job->asset_name == expected_asset && job->source_url == expected_url &&
                        is_newer_release_tag(job->version, "0.0.0") &&
                        is_sha256_digest(job->expected_sha256) && job->expected_size >= 1024 &&
                        job->expected_size <= kMaximumRuntimeUpdateBytes;
                } else {
                    const auto* component = catalog::find_component(job->item_id);
                    safe_to_resume = job->license_accepted && component && component->installable &&
                        component->kind == job->kind &&
                        (component->installer == "llama-release" || component->installer == "pinned-gguf") &&
                        catalog::supports_target(*component, host_os(), host_architecture());
                }
                if (!safe_to_resume || job->cancel_requested.load()) {
                    job->state = job->cancel_requested.load() ? "canceled" : "failed";
                    job->phase = job->cancel_requested.load() ? "Canceled while waiting in the install queue" : "Queued item failed startup validation";
                    job->error_code = job->cancel_requested.load() ? "job_canceled" : "queue_validation_failed";
                    job->error = job->cancel_requested.load()
                        ? "The queued installation was canceled before it started."
                        : "The saved queue entry no longer matches a trusted, installable catalog item for this platform.";
                    job->finished_at = unix_time_millis();
                    job->updated_at = job->finished_at;
                    job->cancel_requested.store(false);
                }
            } else if (!is_terminal_job_state(job->state)) {
                job->state = "interrupted";
                job->phase = "Interrupted when Local AI Runtime was not running";
                job->error_code = "process_interrupted";
                job->error = "The active installation was interrupted when the runtime stopped. It was not replayed; retry it to create a new queued attempt.";
                job->finished_at = unix_time_millis();
                job->updated_at = job->finished_at;
                job->cancel_requested.store(false);
            }
            if (job->error_code.empty() && job->state == "failed") job->error_code = "job_failed";
            if (job->error_code.empty() && job->state == "canceled") job->error_code = "job_canceled";
            if (job->id.rfind("download-", 0) == 0) {
                try {
                    const auto number = std::stoull(job->id.substr(9));
                    next_job_number_ = std::max(next_job_number_, static_cast<std::uint64_t>(number + 1));
                } catch (...) {}
            }
            job->persist_callback = [this, weak = std::weak_ptr<Job>(job)] {
                if (const auto locked = weak.lock()) persist_job(locked);
            };
            loaded.push_back(std::move(job));
        } catch (const std::exception& ex) {
            log_diagnostic("WARN", "jobs.persistence_record_skipped", "A saved job record could not be read: " + std::string(ex.what()));
        }
    }
    std::sort(loaded.begin(), loaded.end(), [](const auto& left, const auto& right) {
        return snapshot_job(left).created_at < snapshot_job(right).created_at;
    });
    if (loaded.size() > 64) {
        const auto first_kept = loaded.end() - 64;
        for (auto it = loaded.begin(); it != first_kept; ++it) remove_persisted_job((*it)->id);
        loaded.erase(loaded.begin(), first_kept);
    }
    jobs_ = std::move(loaded);
    for (const auto& job : jobs_) persist_job(job);
}

bool DownloadManager::persist_job(const std::shared_ptr<Job>& job) const {
    if (!job) return false;
    std::lock_guard<std::mutex> persistence_lock(persistence_mutex_);
    std::string id;
    std::ostringstream json;
    {
        std::lock_guard<std::mutex> job_lock(job->mutex);
        id = job->id;
        if (id.empty() || id.size() > 96 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return std::isalnum(c) != 0 || c == '-' || c == '_';
            })) return false;
        json << "{\"schema_version\":1"
             << ",\"id\":" << json_quote(job->id)
             << ",\"item_id\":" << json_quote(job->item_id)
             << ",\"kind\":" << json_quote(job->kind)
             << ",\"state\":" << json_quote(job->state)
             << ",\"phase\":" << json_quote(job->phase)
             << ",\"version\":" << json_quote(job->version)
             << ",\"installed_path\":" << json_quote(job->installed_path)
             << ",\"error\":" << json_quote(job->error)
             << ",\"last_error\":" << json_quote(job->last_error)
             << ",\"error_code\":" << json_quote(job->error_code)
             << ",\"source_url\":" << json_quote(job->source_url)
             << ",\"expected_sha256\":" << json_quote(job->expected_sha256)
             << ",\"asset_name\":" << json_quote(job->asset_name)
             << ",\"expected_size\":" << job->expected_size
             << ",\"downloaded_bytes\":" << job->downloaded_bytes
             << ",\"total_bytes\":" << job->total_bytes
             << ",\"created_at\":" << job->created_at
             << ",\"updated_at\":" << job->updated_at
             << ",\"started_at\":" << job->started_at
             << ",\"finished_at\":" << job->finished_at
             << ",\"attempt\":" << job->attempt
             << ",\"license_accepted\":" << (job->license_accepted ? "true" : "false")
             << ",\"cancel_requested\":" << (job->cancel_requested.load() ? "true" : "false")
             << "}\n";
    }

    const auto directory = user_data_directory() / "jobs";
    std::error_code fs_error;
    std::filesystem::create_directories(directory, fs_error);
    if (fs_error) {
        log_diagnostic("WARN", "jobs.persistence_write_failed", "Could not create the persistent job history directory: " + fs_error.message());
        return false;
    }
    const auto destination = directory / (id + ".json");
    auto temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            log_diagnostic("WARN", "jobs.persistence_write_failed", "Could not open a temporary job history record for writing.");
            return false;
        }
        const auto value = json.str();
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, fs_error);
            log_diagnostic("WARN", "jobs.persistence_write_failed", "Could not flush a temporary job history record.");
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto code = GetLastError();
        std::filesystem::remove(temporary, fs_error);
        log_diagnostic("WARN", "jobs.persistence_replace_failed", "Could not atomically replace a saved job record; Windows error=" + std::to_string(code));
        return false;
    }
#else
    std::filesystem::rename(temporary, destination, fs_error);
    if (fs_error) {
        std::filesystem::remove(temporary, fs_error);
        log_diagnostic("WARN", "jobs.persistence_replace_failed", "Could not atomically replace a saved job record.");
        return false;
    }
#endif
    return true;
}

void DownloadManager::remove_persisted_job(const std::string& job_id) const {
    if (job_id.empty() || job_id.size() > 96 || !std::all_of(job_id.begin(), job_id.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '-' || c == '_';
        })) return;
    std::lock_guard<std::mutex> persistence_lock(persistence_mutex_);
    std::error_code fs_error;
    std::filesystem::remove(user_data_directory() / "jobs" / (job_id + ".json"), fs_error);
}

std::filesystem::path DownloadManager::user_data_directory() {
    // The desktop shell/launcher set this explicit contract so configuration,
    // catalog state, downloads, diagnostics and the embedded browser all resolve
    // to the same persistent per-user directory even across upgrades or when
    // the process environment differs from a terminal session.
#ifdef _WIN32
    std::wstring explicit_value(32768, L'\0');
    const DWORD explicit_length = GetEnvironmentVariableW(
        L"LOCAL_AI_RUNTIME_DATA_DIR", explicit_value.data(), static_cast<DWORD>(explicit_value.size()));
    if (explicit_length > 0 && explicit_length < explicit_value.size()) {
        explicit_value.resize(explicit_length);
        std::error_code ec;
        auto selected = std::filesystem::absolute(std::filesystem::path(explicit_value), ec);
        return (ec ? std::filesystem::path(explicit_value) : selected).lexically_normal();
    }
    std::wstring value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) {
        value.resize(length);
        return std::filesystem::path(value) / L"LocalAIRuntime";
    }
    return std::filesystem::temp_directory_path() / L"LocalAIRuntime";
#else
    if (const char* explicit_root = std::getenv("LOCAL_AI_RUNTIME_DATA_DIR"); explicit_root && *explicit_root) {
        std::error_code ec;
        const auto selected = std::filesystem::absolute(std::filesystem::path(explicit_root), ec);
        return (ec ? std::filesystem::path(explicit_root) : selected).lexically_normal();
    }
#ifdef __APPLE__
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "local-ai-runtime";
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / "Library" / "Application Support" / "LocalAIRuntime";
    return std::filesystem::temp_directory_path() / "LocalAIRuntime";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "local-ai-runtime";
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local" / "share" / "local-ai-runtime";
    return std::filesystem::temp_directory_path() / "local-ai-runtime";
#endif
#endif
}

std::string DownloadManager::runtime_update_asset_name() {
    const auto os = host_os();
    const auto arch = host_architecture();
    if (os == "windows" && arch == "x64") return "local-ai-runtime-windows-x64.zip";
    if (os == "linux" && arch == "x64") return "local-ai-runtime-linux-x64.tar.gz";
    if (os == "macos" && arch == "x64") return "local-ai-runtime-macos-x64.tar.gz";
    if (os == "macos" && arch == "arm64") return "local-ai-runtime-macos-arm64.tar.gz";
    return {};
}

bool DownloadManager::is_safe_archive_entry_path(const std::string& entry_path) {
    if (entry_path.empty() || entry_path.size() > 4096 || entry_path.front() == '/' || entry_path.front() == '\\') return false;
    if (entry_path.find('\\') != std::string::npos || entry_path.find(':') != std::string::npos) return false;
    const auto relative = path_from_utf8(entry_path);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) return false;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    const auto normalized = relative.lexically_normal();
    return !normalized.empty() && normalized != ".";
}

std::string DownloadManager::sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open file for SHA-256 verification.");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) throw std::runtime_error("Could not initialize SHA-256 verification.");
    // A 1 MiB automatic array can exhaust the default 1 MiB MSVC thread stack
    // before this function even reads a file. Keep the streaming buffer on the
    // heap; this is also used to hash large model files during installation.
    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<std::size_t>(count)) != 1) {
            throw std::runtime_error("Could not update SHA-256 verification.");
        }
    }
    if (!input.eof()) throw std::runtime_error("Could not read the full file for SHA-256 verification.");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &length) != 1 || length != 32) throw std::runtime_error("Could not finalize SHA-256 verification.");
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < length; ++i) out << std::setw(2) << static_cast<unsigned int>(digest[i]);
    return out.str();
}

std::string DownloadManager::catalog_json() const {
    const auto root = user_data_directory();
    const auto os = host_os();
    const auto arch = host_architecture();
    std::ostringstream out;
    out << "{\"schema_version\":1,\"data\":[";
    bool first = true;
    for (const auto& component : catalog::components()) {
        if (!component.installable || !catalog::supports_target(component, os, arch)) continue;

        bool installed = false;
        std::filesystem::path installed_path;
        std::error_code fs_error;
        if (component.installer == "pinned-gguf") {
            installed_path = root / "models" / component.filename;
            if (std::filesystem::is_regular_file(installed_path, fs_error) && !fs_error) {
                const auto actual_size = std::filesystem::file_size(installed_path, fs_error);
                if (!fs_error && actual_size == component.size_bytes) {
                    try {
                        installed = sha256_file(installed_path) == component.sha256;
                    } catch (...) {
                        installed = false;
                    }
                }
            }
        } else if (component.installer == "llama-release") {
            const auto engine_root = root / "engines" / "llama.cpp";
            if (std::filesystem::exists(engine_root, fs_error) && !fs_error) {
                for (std::filesystem::directory_iterator it(engine_root, fs_error), end; it != end && !fs_error; it.increment(fs_error)) {
                    if (!it->is_directory(fs_error) || fs_error) { fs_error.clear(); continue; }
                    const auto candidate_name = it->path().filename().string();
                    const auto suffix = std::string("-") + component.variant;
                    if (candidate_name.size() >= suffix.size() &&
                        candidate_name.compare(candidate_name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                        find_llama_server(it->path())) {
                        installed_path = it->path();
                        installed = true;
                        break;
                    }
                }
            }
        }

        if (!first) out << ',';
        first = false;
        auto metadata = component.metadata_json;
        const auto close = metadata.find_last_of('}');
        if (close == std::string::npos || close + 1U != metadata.size()) {
            throw std::runtime_error("Compiled catalog metadata is not a valid JSON object: " + component.id);
        }
        metadata.resize(close);
        out << metadata << ",\"installed\":" << (installed ? "true" : "false")
            << ",\"installed_path\":" << json_quote(installed ? path_to_utf8(installed_path) : std::string{}) << '}';
    }
    out << "]}";
    return out.str();
}

std::string DownloadManager::runtime_profile_json() const {
    const auto configs = engines_.configurations();
    const auto active = std::find_if(configs.begin(), configs.end(), [](const EngineConfig& engine) {
        return engine.default_for_chat;
    });
    std::string engine_component_id = config_.runtime_profile.engine_component_id;
    std::string model_component_id = config_.runtime_profile.model_component_id;
    if (active != configs.end()) {
        if (engine_component_id.empty()) engine_component_id = infer_engine_component_id(*active);
        if (model_component_id.empty()) model_component_id = infer_model_component_id(*active);
    }
    const bool engine_configured = active != configs.end() && !active->executable_path.empty();
    const bool model_configured = active != configs.end() && !active->model_path.empty();
    const auto context = active != configs.end() ? configured_context_size(*active) : config_.runtime_profile.context_size;
    const auto backend = active != configs.end() ? configured_gpu_layers(*active) : std::string("0");

    std::ostringstream out;
    out << "{\"schema_version\":1"
        << ",\"engine_component_id\":" << json_quote(engine_component_id)
        << ",\"model_component_id\":" << json_quote(model_component_id)
        << ",\"context_size\":" << context
        << ",\"compute_mode\":" << json_quote(backend == "0" ? "cpu" : "auto")
        << ",\"engine_configured\":" << (engine_configured ? "true" : "false")
        << ",\"model_configured\":" << (model_configured ? "true" : "false")
        << ",\"can_chat\":" << (engine_configured && model_configured ? "true" : "false")
        << '}';
    return out.str();
}

bool DownloadManager::apply_runtime_profile(const std::string& engine_component_id,
                                            const std::string& model_component_id,
                                            unsigned int context_size, std::string& error) {
    if (context_size < 512U || context_size > 32768U) {
        error = "Context size must be between 512 and 32768 tokens.";
        return false;
    }
    const auto* engine_component = catalog::find_component(engine_component_id);
    if (!engine_component || engine_component->kind != "engine" ||
        engine_component->installer != "llama-release" || !engine_component->installable ||
        !catalog::supports_target(*engine_component, host_os(), host_architecture())) {
        error = "Select an installed engine component supported by this OS and architecture.";
        return false;
    }
    const auto* model_component = catalog::find_component(model_component_id);
    if (!model_component || model_component->kind != "model" ||
        model_component->installer != "pinned-gguf" || !model_component->installable ||
        !catalog::supports_target(*model_component, host_os(), host_architecture())) {
        error = "Select a reviewed, supported model component.";
        return false;
    }
    if (config_.source_path.empty()) {
        error = "The runtime configuration path is unknown; the active profile cannot be saved.";
        return false;
    }

    const auto data_root = user_data_directory();
    const auto engine_directory = find_installed_llama_variant(data_root / "engines" / "llama.cpp", engine_component->variant);
    if (!engine_directory) {
        error = "The selected engine package is not installed. Install it from Models before applying this profile.";
        return false;
    }
    const auto executable = find_llama_server(*engine_directory);
    if (!executable) {
        error = "The installed engine package does not contain llama-server.";
        return false;
    }

    const auto model_path = data_root / "models" / model_component->filename;
    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(model_path, fs_error) || fs_error) {
        error = "The selected model is not installed. Download it from Models before applying this profile.";
        return false;
    }
    const auto actual_size = std::filesystem::file_size(model_path, fs_error);
    if (fs_error || actual_size != model_component->size_bytes) {
        error = "The installed model size does not match the reviewed catalog. Reinstall the model before using it.";
        return false;
    }
    try {
        if (sha256_file(model_path) != model_component->sha256 || !has_gguf_header(model_path)) {
            error = "The installed model failed its SHA-256 or GGUF header check. Reinstall it before using it.";
            return false;
        }
    } catch (const std::exception& ex) {
        error = std::string("Could not verify the selected model: ") + ex.what();
        return false;
    }

    auto candidates = engines_.configurations();
    auto selected = std::find_if(candidates.begin(), candidates.end(), [](const EngineConfig& engine) {
        return engine.id == "llama-cpp";
    });
    EngineConfig engine;
    if (selected != candidates.end()) engine = *selected;
    engine.id = "llama-cpp";
    engine.display_name = "llama.cpp server";
    engine.executable_path = *executable;
    engine.model_path = model_path;
    engine.working_directory = executable->parent_path();
    const auto port = configured_engine_port(engine);
    const bool gpu_requested = engine_component->variant == "cuda12" || engine_component->variant == "cuda13";
    engine.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                   "--ctx-size", std::to_string(context_size), "--n-gpu-layers",
                   gpu_requested ? "auto" : "0", "--cors-origins", "localhost",
                   "--no-cors-credentials", "-m", "{model_path}"};
    engine.upstream_base_url = "http://127.0.0.1:" + std::to_string(port);
    engine.capabilities = {"chat"};
    engine.default_for_chat = true;
    if (selected == candidates.end()) candidates.push_back(engine);
    else *selected = engine;
    for (auto& current : candidates) current.default_for_chat = current.id == "llama-cpp";

    RuntimeConfig next_config = config_;
    next_config.engines = candidates;
    next_config.runtime_profile.engine_component_id = engine_component_id;
    next_config.runtime_profile.model_component_id = model_component_id;
    next_config.runtime_profile.context_size = context_size;
    const auto persist = [&](std::string& persist_error) {
        try {
            save_config(config_.source_path, next_config);
            return true;
        } catch (const std::exception& ex) {
            persist_error = std::string("Could not save runtime profile: ") + ex.what();
            return false;
        }
    };
    if (!engines_.replace_configs(candidates, persist, error)) return false;
    config_ = std::move(next_config);
    error.clear();
    log_diagnostic("INFO", "runtime.profile.applied", "engine_component=" + engine_component_id +
                   " model_component=" + model_component_id + " context=" + std::to_string(context_size));
    return true;
}

std::string DownloadManager::jobs_json() const {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs = jobs_;
    }
    const auto queue_positions = build_queue_positions(jobs);
    std::ostringstream out;
    out << "{\"data\":[";
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        if (i) out << ',';
        const auto job = snapshot_job(jobs[i]);
        const auto percent = job.total_bytes > 0 ? std::min<std::uint64_t>(100, (job.downloaded_bytes * 100ULL) / job.total_bytes)
                          : (job.state == "succeeded" ? 100ULL : 0ULL);
        const auto position = queue_positions.find(job.id);
        out << "{\"id\":" << json_quote(job.id) << ",\"item_id\":" << json_quote(job.item_id)
            << ",\"kind\":" << json_quote(job.kind) << ",\"state\":" << json_quote(job.state)
            << ",\"phase\":" << json_quote(job.phase) << ",\"version\":" << json_quote(job.version)
            << ",\"downloaded_bytes\":" << job.downloaded_bytes << ",\"total_bytes\":" << job.total_bytes
            << ",\"progress_percent\":" << percent << ",\"installed_path\":" << json_quote(job.installed_path)
            << ",\"error\":" << json_quote(job.error)
            << ",\"last_error\":" << json_quote(job.last_error)
            << ",\"error_code\":" << json_quote(job.error_code)
            << ",\"created_at\":" << job.created_at
            << ",\"updated_at\":" << job.updated_at
            << ",\"started_at\":" << job.started_at
            << ",\"finished_at\":" << job.finished_at
            << ",\"attempt\":" << job.attempt
            << ",\"cancel_requested\":" << (job.cancel_requested ? "true" : "false")
            << ",\"retryable\":" << (job.retryable ? "true" : "false")
            << ",\"queue_position\":" << (position == queue_positions.end() ? 0U : position->second)
            << ",\"dependency_plan\":" << dependency_plan_json(job.item_id) << '}';
    }
    out << "]}";
    return out.str();
}

std::optional<std::string> DownloadManager::job_json(const std::string& id) const {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs = jobs_;
    }
    const auto queue_positions = build_queue_positions(jobs);
    for (const auto& job : jobs) {
        const auto view = snapshot_job(job);
        if (view.id != id) continue;
        const auto percent = view.total_bytes > 0 ? std::min<std::uint64_t>(100, (view.downloaded_bytes * 100ULL) / view.total_bytes)
                          : (view.state == "succeeded" ? 100ULL : 0ULL);
        const auto position = queue_positions.find(view.id);
        return "{\"id\":" + json_quote(view.id) + ",\"item_id\":" + json_quote(view.item_id) +
            ",\"kind\":" + json_quote(view.kind) + ",\"state\":" + json_quote(view.state) +
            ",\"phase\":" + json_quote(view.phase) + ",\"version\":" + json_quote(view.version) +
            ",\"downloaded_bytes\":" + std::to_string(view.downloaded_bytes) +
            ",\"total_bytes\":" + std::to_string(view.total_bytes) +
            ",\"progress_percent\":" + std::to_string(percent) +
            ",\"installed_path\":" + json_quote(view.installed_path) + ",\"error\":" + json_quote(view.error) +
            ",\"last_error\":" + json_quote(view.last_error) + ",\"error_code\":" + json_quote(view.error_code) +
            ",\"created_at\":" + std::to_string(view.created_at) + ",\"updated_at\":" + std::to_string(view.updated_at) +
            ",\"started_at\":" + std::to_string(view.started_at) + ",\"finished_at\":" + std::to_string(view.finished_at) +
            ",\"attempt\":" + std::to_string(view.attempt) +
            ",\"cancel_requested\":" + (view.cancel_requested ? "true" : "false") +
            ",\"retryable\":" + (view.retryable ? "true" : "false") +
            ",\"queue_position\":" + std::to_string(position == queue_positions.end() ? 0U : position->second) +
            ",\"dependency_plan\":" + dependency_plan_json(view.item_id) + "}";
    }
    return std::nullopt;
}

std::size_t DownloadManager::outstanding_install_job_count_locked() const {
    std::size_t count = 0;
    for (const auto& job : jobs_) {
        if (is_active_job_state(snapshot_job(job).state)) ++count;
    }
    return count;
}

bool DownloadManager::start(const std::string& item_id, bool accept_license,
                            std::string& job_id, std::string& error) {
    if (!accept_license) { error = "Confirm that you reviewed the upstream license before downloading."; return false; }
    const auto* component = catalog::find_component(item_id);
    if (!component || !component->installable ||
        (component->installer != "llama-release" && component->installer != "pinned-gguf")) {
        error = "This item is not in the compiled reviewed catalog manifest.";
        return false;
    }
    if (!catalog::supports_target(*component, host_os(), host_architecture())) {
        error = "This catalog item is not supported on the current OS/architecture.";
        return false;
    }
    if (any_engine_running(engines_)) {
        error = "Stop running engines before installing or changing runtime components.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) { error = "The installation queue is shutting down."; return false; }
        for (const auto& existing : jobs_) {
            const auto view = snapshot_job(existing);
            if (!is_active_job_state(view.state)) continue;
            if (view.item_id == item_id) {
                // Duplicate requests are idempotent: return the existing queued/active job.
                job_id = view.id;
                error.clear();
                log_diagnostic("INFO", "download.duplicate_request_reused", "job=" + job_id + " item=" + item_id);
                return true;
            }
        }
        if (outstanding_install_job_count_locked() >= kMaximumOutstandingInstallJobs) {
            error = "The installation queue is full. Wait for a queued item to finish before adding another.";
            return false;
        }
        constexpr std::size_t max_job_history = 64;
        while (jobs_.size() >= max_job_history) {
            const auto terminal = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& candidate) {
                return is_terminal_job_state(snapshot_job(candidate).state);
            });
            if (terminal == jobs_.end()) {
                error = "Download job history is full and no completed job can be removed yet.";
                return false;
            }
            remove_persisted_job((*terminal)->id);
            jobs_.erase(terminal);
        }

        auto job = std::make_shared<Job>();
        job->id = "download-" + std::to_string(next_job_number_++);
        job->item_id = item_id;
        job->kind = component->kind;
        job->license_accepted = accept_license;
        job->state = "queued";
        job->phase = "Waiting in installation queue";
        job->created_at = unix_time_millis();
        job->updated_at = job->created_at;
        job->persist_callback = [this, weak = std::weak_ptr<Job>(job)] {
            if (const auto locked = weak.lock()) persist_job(locked);
        };
        job_id = job->id;
        jobs_.push_back(job);
        if (!persist_job(job)) {
            jobs_.pop_back();
            error = "Could not persist this job in the local user-data folder; it was not added to the queue.";
            return false;
        }
    }
    worker_cv_.notify_one();
    log_diagnostic("INFO", "download.queued", "job=" + job_id + " item=" + item_id);
    error.clear();
    return true;
}

bool DownloadManager::start_update(const std::string& current_version, const std::string& tag_name,
                                   const std::string& asset_name, const std::string& download_url,
                                   const std::string& digest, std::uint64_t expected_size,
                                   std::string& job_id, std::string& error) {
    const auto expected_asset = runtime_update_asset_name();
    if (expected_asset.empty()) {
        error = "Automatic update staging is not supported for this OS/architecture.";
        return false;
    }
    if (asset_name != expected_asset) {
        error = "The release asset does not match this runtime's OS and architecture.";
        return false;
    }
    if (!is_newer_release_tag(tag_name, current_version)) {
        error = "The requested release tag is invalid or is not newer than the installed runtime.";
        return false;
    }
    const std::string expected_url = "https://github.com/azmisahin-ai/local-ai-runtime/releases/download/" +
                                     tag_name + "/" + expected_asset;
    if (download_url != expected_url) {
        error = "Refusing a runtime update URL outside the exact official release asset path.";
        return false;
    }
    std::string normalized_digest = digest;
    if (normalized_digest.rfind("sha256:", 0) == 0) normalized_digest.erase(0, 7);
    normalized_digest = lower_ascii(std::move(normalized_digest));
    if (!is_sha256_digest(normalized_digest)) {
        error = "The GitHub release asset did not provide a valid SHA-256 digest. Update staging is disabled.";
        return false;
    }
    if (expected_size < 1024 || expected_size > kMaximumRuntimeUpdateBytes) {
        error = "The release asset size is missing or exceeds the 256 MiB safety limit.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) { error = "The installation queue is shutting down."; return false; }
        for (const auto& existing : jobs_) {
            const auto view = snapshot_job(existing);
            if (is_active_job_state(view.state) && view.item_id == "runtime-update" && view.version == tag_name) {
                job_id = view.id;
                error.clear();
                log_diagnostic("INFO", "update.duplicate_request_reused", "job=" + job_id + " version=" + tag_name);
                return true;
            }
        }
        if (outstanding_install_job_count_locked() >= kMaximumOutstandingInstallJobs) {
            error = "The installation queue is full. Wait for a queued item to finish before adding another.";
            return false;
        }
        constexpr std::size_t max_job_history = 64;
        while (jobs_.size() >= max_job_history) {
            const auto terminal = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& candidate) {
                return is_terminal_job_state(snapshot_job(candidate).state);
            });
            if (terminal == jobs_.end()) {
                error = "Download job history is full and no completed job can be removed yet.";
                return false;
            }
            remove_persisted_job((*terminal)->id);
            jobs_.erase(terminal);
        }
        auto job = std::make_shared<Job>();
        job->id = "download-" + std::to_string(next_job_number_++);
        job->item_id = "runtime-update";
        job->kind = "update";
        job->version = tag_name;
        job->source_url = download_url;
        job->expected_sha256 = normalized_digest;
        job->asset_name = asset_name;
        job->expected_size = expected_size;
        job->total_bytes = expected_size;
        job->state = "queued";
        job->phase = "Waiting in installation queue";
        job->created_at = unix_time_millis();
        job->updated_at = job->created_at;
        job->persist_callback = [this, weak = std::weak_ptr<Job>(job)] {
            if (const auto locked = weak.lock()) persist_job(locked);
        };
        job_id = job->id;
        jobs_.push_back(job);
        if (!persist_job(job)) {
            jobs_.pop_back();
            error = "Could not persist this update job in the local user-data folder; no update download was queued.";
            return false;
        }
    }
    worker_cv_.notify_one();
    log_diagnostic("INFO", "update.download_queued", "job=" + job_id + " version=" + tag_name + " asset=" + asset_name);
    error.clear();
    return true;
}

void DownloadManager::worker_loop() {
    for (;;) {
        std::shared_ptr<Job> next_job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            worker_cv_.wait(lock, [&] {
                if (stopping_) return true;
                return std::any_of(jobs_.begin(), jobs_.end(), [](const auto& job) {
                    return snapshot_job(job).state == "queued";
                });
            });
            if (stopping_) return;

            for (const auto& candidate : jobs_) {
                const auto view = snapshot_job(candidate);
                if (view.state != "queued") continue;
                if (!next_job) {
                    next_job = candidate;
                    continue;
                }
                const auto selected = snapshot_job(next_job);
                if (view.updated_at < selected.updated_at ||
                    (view.updated_at == selected.updated_at && view.created_at < selected.created_at)) {
                    next_job = candidate;
                }
            }
            if (!next_job) continue;
            active_job_ = next_job;
        }

        run_job(next_job);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_job_ == next_job) active_job_.reset();
        }
        worker_cv_.notify_one();
    }
}

bool DownloadManager::has_active_jobs() const {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs = jobs_;
    }
    for (const auto& job : jobs) {
        const auto view = snapshot_job(job);
        if (is_active_job_state(view.state)) return true;
    }
    return false;
}

bool DownloadManager::cancel(const std::string& job_id, std::string& error) {
    std::shared_ptr<Job> selected;
    bool queued_canceled = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto& job) { return job->id == job_id; });
        if (found != jobs_.end()) selected = *found;
        if (!selected) { error = "Unknown download job id."; return false; }
        const auto view = snapshot_job(selected);
        if (is_terminal_job_state(view.state)) { error = "This job has already finished."; return false; }
        if (view.state == "queued" && active_job_ != selected) {
            mutate_job(selected, [](Job& job) {
                job.cancel_requested.store(false);
                job.state = "canceled";
                job.phase = "Canceled while waiting in the installation queue";
                job.error = "The queued installation was canceled before it started.";
                job.error_code = "job_canceled";
                job.finished_at = unix_time_millis();
            });
            queued_canceled = true;
        } else {
            mutate_job(selected, [](Job& job) {
                job.cancel_requested.store(true);
                job.phase = "Cancel requested; waiting for a safe cancellation point";
            });
        }
    }
    worker_cv_.notify_one();
    error.clear();
    log_diagnostic("INFO", queued_canceled ? "jobs.queue_item_canceled" : "jobs.cancel_requested",
                   "job=" + job_id);
    return true;
}

bool DownloadManager::retry(const std::string& job_id, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) { error = "The installation queue is shutting down."; return false; }
        const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto& job) { return job->id == job_id; });
        if (found == jobs_.end()) { error = "Unknown job id."; return false; }
        const auto job = *found;
        const auto previous = snapshot_job(job);
        if (previous.state != "failed" && previous.state != "canceled" && previous.state != "interrupted") {
            error = "Only failed, canceled or interrupted jobs can be retried.";
            return false;
        }
        if (!is_retryable_job(*job)) {
            error = "This saved job does not contain enough trusted task information to retry safely.";
            return false;
        }
        if (any_engine_running(engines_)) {
            error = "Stop running engines before retrying a component installation.";
            return false;
        }
        if (outstanding_install_job_count_locked() >= kMaximumOutstandingInstallJobs) {
            error = "The installation queue is full. Wait for a queued item to finish before retrying.";
            return false;
        }
        for (const auto& existing : jobs_) {
            if (existing == job) continue;
            const auto view = snapshot_job(existing);
            if (is_active_job_state(view.state) && view.item_id == previous.item_id) {
                error = "This catalog item already has an active or queued installation in job " + view.id + ".";
                return false;
            }
        }
        if (job->item_id != "runtime-update") {
            const auto* component = catalog::find_component(job->item_id);
            if (!component || !component->installable || component->kind != job->kind ||
                !catalog::supports_target(*component, host_os(), host_architecture())) {
                error = "This catalog item is not in the reviewed manifest for the current OS/architecture and cannot be retried.";
                return false;
            }
        }
        if (job->item_id == "runtime-update") {
            const auto expected_asset = runtime_update_asset_name();
            const auto expected_url = "https://github.com/azmisahin-ai/local-ai-runtime/releases/download/" +
                                      job->version + "/" + expected_asset;
            if (expected_asset.empty() || job->asset_name != expected_asset || job->source_url != expected_url ||
                !is_newer_release_tag(job->version, "0.0.0") || !is_sha256_digest(job->expected_sha256) ||
                job->expected_size < 1024 || job->expected_size > kMaximumRuntimeUpdateBytes) {
                error = "The saved runtime update metadata failed its safety checks and cannot be retried.";
                return false;
            }
        } else if (!job->license_accepted) {
            error = "The original upstream license acceptance was not recorded; start this catalog item again and review its license.";
            return false;
        }

        mutate_job(job, [](Job& current) {
            current.last_error = current.error.empty() ? current.last_error : current.error;
            current.error.clear();
            current.error_code.clear();
            current.state = "queued";
            current.phase = "Waiting in installation queue — retry";
            current.downloaded_bytes = 0;
            current.total_bytes = current.item_id == "runtime-update" ? current.expected_size : 0;
            current.cancel_requested.store(false);
            current.started_at = 0;
            current.finished_at = 0;
            ++current.attempt;
        });
        if (!persist_job(job)) {
            mutate_job(job, [&](Job& current) {
                current.state = previous.state;
                current.phase = previous.phase;
                current.error = previous.error;
                current.error_code = previous.error_code;
                current.downloaded_bytes = previous.downloaded_bytes;
                current.total_bytes = previous.total_bytes;
                current.attempt = previous.attempt;
                current.started_at = previous.started_at;
                current.finished_at = previous.finished_at;
                current.cancel_requested.store(previous.cancel_requested);
            });
            error = "Could not persist the retry attempt in the local user-data folder; no retry was queued.";
            return false;
        }
    }
    worker_cv_.notify_one();
    log_diagnostic("INFO", "jobs.retry_queued", "job=" + job_id);
    error.clear();
    return true;
}

void DownloadManager::run_job(const std::shared_ptr<Job>& job) {
    mutate_job(job, [](Job& current) {
        current.state = "running";
        current.started_at = unix_time_millis();
        current.phase = current.attempt > 1 ? "Starting retry attempt" : "Starting queued installation";
    });
    log_diagnostic("INFO", "download.started", "job=" + job->id + " item=" + job->item_id);
    try {
        if (job->cancel_requested.load()) throw std::runtime_error("Download canceled by the user.");
        if (test_executor_) {
            test_executor_(job->item_id, [job] { return job->cancel_requested.load(); });
            if (job->cancel_requested.load()) throw std::runtime_error("Download canceled by the user.");
        } else if (job->item_id == "runtime-update") {
            download_runtime_update(job);
        } else {
            const auto* component = catalog::find_component(job->item_id);
            if (!component || !component->installable || component->kind != job->kind ||
                !catalog::supports_target(*component, host_os(), host_architecture())) {
                throw std::runtime_error("Saved job no longer matches an installable component in the compiled catalog manifest.");
            }
            if (component->installer == "pinned-gguf") download_model(job);
            else if (component->installer == "llama-release") download_engine(job, component->variant);
            else throw std::runtime_error("The catalog manifest contains an unsupported installer strategy.");
        }
        mutate_job(job, [](DownloadManager::Job& current) {
            current.state = "succeeded";
            if (current.item_id != "runtime-update") current.phase = "Installed and configured";
            current.downloaded_bytes = current.total_bytes > 0 ? current.total_bytes : current.downloaded_bytes;
            current.error.clear();
            current.error_code.clear();
            current.finished_at = unix_time_millis();
        });
        const auto finished = snapshot_job(job);
        log_diagnostic("INFO", "download.succeeded", "job=" + finished.id + " item=" + finished.item_id +
                       " bytes=" + std::to_string(finished.downloaded_bytes) + " version=" + finished.version);
    } catch (const std::exception& ex) {
        const auto previous = snapshot_job(job);
        const bool canceled = job->cancel_requested.load() || std::string_view(ex.what()).find("canceled") != std::string_view::npos || std::string_view(ex.what()).find("Canceled") != std::string_view::npos;
        mutate_job(job, [&](DownloadManager::Job& current) {
            current.state = canceled ? "canceled" : "failed";
            current.phase = canceled ? "Canceled" : "Failed";
            current.error = ex.what();
            current.error_code = canceled ? "job_canceled" : "job_failed";
            current.finished_at = unix_time_millis();
        });
        log_diagnostic(canceled ? "WARN" : "ERROR", canceled ? "download.canceled" : "download.failed",
                       "job=" + previous.id + " item=" + previous.item_id +
                       " phase=" + previous.phase + " bytes=" + std::to_string(previous.downloaded_bytes) +
                       " error=" + ex.what());
        std::error_code ignored;
        const auto root = user_data_directory();
        std::filesystem::remove_all(root / "staging" / job->id, ignored);
        // Keep models/<name>.part: download_model only leaves this final staging
        // file behind after the size, SHA-256 and GGUF checks have all passed.
        // A retry can finalize it without downloading another gigabyte.
        if (const auto* component = catalog::find_component(job->item_id);
            component && component->installer == "pinned-gguf") {
            ignored.clear();
            std::filesystem::remove(root / "models" / (component->filename + ".part.part"), ignored);
        }
    }
}

void DownloadManager::download_runtime_update(const std::shared_ptr<Job>& job) {
    const auto root = user_data_directory() / "updates" / job->version;
    std::error_code fs_error;
    std::filesystem::create_directories(root, fs_error);
    if (fs_error) throw std::runtime_error("Could not create the runtime update staging directory: " + fs_error.message());
    const auto destination = root / job->asset_name;

    // A previously verified package can be reused; never silently accept a file
    // merely because its name or size looks correct.
    if (std::filesystem::is_regular_file(destination, fs_error) && !fs_error) {
        try {
            if (std::filesystem::file_size(destination, fs_error) == job->expected_size && !fs_error &&
                sha256_file(destination) == job->expected_sha256) {
                mutate_job(job, [&](DownloadManager::Job& current) {
                    current.state = "verifying";
                    current.phase = "Reusing and re-verifying the staged release package";
                    current.downloaded_bytes = job->expected_size;
                    current.total_bytes = job->expected_size;
                });
            } else {
                std::filesystem::remove(destination, fs_error);
                fs_error.clear();
            }
        } catch (...) {
            std::filesystem::remove(destination, fs_error);
            fs_error.clear();
        }
    }

    if (!(std::filesystem::is_regular_file(destination, fs_error) && !fs_error &&
          std::filesystem::file_size(destination, fs_error) == job->expected_size && !fs_error &&
          sha256_file(destination) == job->expected_sha256)) {
        mutate_job(job, [](DownloadManager::Job& current) {
            current.state = "downloading";
            current.phase = "Downloading the official platform package";
        });
        download_to_file(job->source_url, destination, job->expected_size, job);
        mutate_job(job, [](DownloadManager::Job& current) {
            current.state = "verifying";
            current.phase = "Verifying downloaded package SHA-256 against GitHub release metadata";
        });
        if (sha256_file(destination) != job->expected_sha256) {
            std::filesystem::remove(destination, fs_error);
            throw std::runtime_error("The downloaded runtime update SHA-256 does not match GitHub's release asset digest. The package was discarded.");
        }
    }

    const auto manifest_path = root / "staged-update.json";
    auto temporary_manifest = manifest_path;
    temporary_manifest += ".part";
    {
        std::ofstream output(temporary_manifest, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Could not write the verified update metadata.");
        output << "{\"tag_name\":" << json_quote(job->version)
               << ",\"asset_name\":" << json_quote(job->asset_name)
               << ",\"sha256\":" << json_quote(job->expected_sha256)
               << ",\"size_bytes\":" << job->expected_size
#ifdef _WIN32
               << ",\"verified\":true,\"apply_supported\":true}\n";
#else
               << ",\"verified\":true,\"apply_supported\":false}\n";
#endif
        output.flush();
        if (!output) throw std::runtime_error("Could not flush the verified update metadata.");
    }
    // std::filesystem::rename does not replace an existing target on Windows.
    // Metadata is reproducible from the verified job; remove the old small file
    // before renaming so re-verifying an already-staged package is idempotent.
    std::filesystem::remove(manifest_path, fs_error);
    fs_error.clear();
    std::filesystem::rename(temporary_manifest, manifest_path, fs_error);
    if (fs_error) {
        const auto detail = fs_error.message();
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_manifest, cleanup_error);
        throw std::runtime_error("Could not finalize verified update metadata: " + detail);
    }
    mutate_job(job, [&](DownloadManager::Job& current) {
        current.state = "succeeded";
        current.phase = "Package downloaded and SHA-256 verified; not installed";
        current.downloaded_bytes = job->expected_size;
        current.total_bytes = job->expected_size;
        current.installed_path = path_to_utf8(destination);
    });
    log_diagnostic("INFO", "update.package_staged", "job=" + job->id + " version=" + job->version +
                   " bytes=" + std::to_string(job->expected_size) + " sha256_verified=true install=not_performed");
}

void DownloadManager::download_model(const std::shared_ptr<Job>& job) {
    const auto& model = pinned_model_component(job->item_id);
    const auto root = user_data_directory();
    const auto directory = root / "models";
    const auto final_path = directory / model.filename;
    auto partial = final_path;
    partial += ".part";
    mutate_job(job, [&](DownloadManager::Job& current) {
        current.state = "downloading";
        current.phase = "Downloading " + job->item_id;
        current.total_bytes = model.size_bytes;
    });
    log_diagnostic("INFO", "model.install.begin", "job=" + job->id + " item=" + job->item_id);
    std::error_code fs_error;
    std::filesystem::create_directories(directory, fs_error);
    if (fs_error) throw std::runtime_error("Could not create the model directory: " + fs_error.message());
    if (std::filesystem::exists(final_path)) {
        bool existing_model_verified = false;
        try {
            const auto size = std::filesystem::file_size(final_path, fs_error);
            existing_model_verified = !fs_error && size == model.size_bytes && sha256_file(final_path) == model.sha256 && has_gguf_header(final_path);
        } catch (...) {
            existing_model_verified = false;
        }
        if (existing_model_verified) {
            mutate_job(job, [&](DownloadManager::Job& current) {
                current.downloaded_bytes = model.size_bytes;
                current.total_bytes = model.size_bytes;
                current.installed_path = path_to_utf8(final_path);
                current.phase = "Model already installed and verified";
            });
            log_diagnostic("INFO", "model.install.already_verified", "job=" + job->id);
            // Installing or re-verifying a model must never change the active chat model.
            // Selection is an explicit Runtime Profile action.
            return;
        }
        fs_error.clear();
        std::filesystem::remove(final_path, fs_error);
        if (fs_error) throw std::runtime_error("An existing model file failed verification but could not be removed: " + fs_error.message());
    }

    bool reusable_staging_file = false;
    if (std::filesystem::exists(partial, fs_error) && !fs_error) {
        try {
            const auto staged_size = std::filesystem::file_size(partial, fs_error);
            reusable_staging_file = !fs_error && staged_size == model.size_bytes &&
                                    sha256_file(partial) == model.sha256 && has_gguf_header(partial);
        } catch (...) {
            reusable_staging_file = false;
        }
        if (reusable_staging_file) {
            mutate_job(job, [&](DownloadManager::Job& current) {
                current.downloaded_bytes = model.size_bytes;
                current.total_bytes = model.size_bytes;
                current.phase = "Reusing downloaded model after SHA-256 and GGUF verification";
            });
            log_diagnostic("INFO", "model.install.reusing_verified_staging_file", "job=" + job->id + " bytes=" + std::to_string(model.size_bytes));
        } else {
            fs_error.clear();
            std::filesystem::remove(partial, fs_error);
            if (fs_error) throw std::runtime_error("An incomplete or invalid staging model could not be removed: " + fs_error.message());
            log_diagnostic("WARN", "model.install.discarded_unverified_staging_file", "job=" + job->id);
        }
    }

    if (!reusable_staging_file) {
        const auto space = std::filesystem::space(directory, fs_error);
        if (fs_error || space.available < model.size_bytes + 256ULL * 1024ULL * 1024ULL) {
            throw std::runtime_error("Not enough free disk space. At least 1.4 GB is required for a new model download and staging.");
        }
        log_diagnostic("INFO", "model.download.begin", "job=" + job->id + " expected_bytes=" + std::to_string(model.size_bytes));
        download_to_file(model.url, partial, model.size_bytes, job);
        log_diagnostic("INFO", "model.download.completed", "job=" + job->id + " bytes=" + std::to_string(model.size_bytes));
    }
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "verifying"; current.phase = "Verifying model size and SHA-256"; });
    const auto actual_size = std::filesystem::file_size(partial, fs_error);
    if (fs_error || actual_size != model.size_bytes) throw std::runtime_error("Downloaded model size does not match the pinned catalog.");
    if (sha256_file(partial) != model.sha256) {
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Model SHA-256 did not match the catalog. The partial file was discarded.");
    }
    if (!has_gguf_header(partial)) {
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Downloaded file does not have a GGUF header.");
    }
    log_diagnostic("INFO", "model.verify.succeeded", "job=" + job->id + " sha256=" + model.sha256);

    mutate_job(job, [](DownloadManager::Job& current) { current.phase = "Finalizing verified model file"; });
    std::filesystem::rename(partial, final_path, fs_error);
    if (fs_error) {
        const auto os_error = fs_error.message();
        log_diagnostic("ERROR", "model.finalize.failed", "job=" + job->id + " verified_bytes=" + std::to_string(model.size_bytes) + " os_error=" + os_error + " recovery=verified_staging_file_preserved");
        throw std::runtime_error("The model downloaded completely and passed SHA-256 verification, but Windows could not move it into place. The verified staging file was kept; close programs that may be using this GGUF file and select Download & install again. It will retry finalization without downloading the model again. Details: " + os_error);
    }
    mutate_job(job, [&](DownloadManager::Job& current) { current.installed_path = path_to_utf8(final_path); });
    log_diagnostic("INFO", "model.install.finalized", "job=" + job->id + " bytes=" + std::to_string(model.size_bytes));
    // Downloading a model only installs a verified artifact. It does not silently
    // replace the model currently selected for Chat.
}

void DownloadManager::download_engine(const std::shared_ptr<Job>& job, const std::string& variant) {
    const auto* component = catalog::find_component(job->item_id);
    if (!component || component->installer != "llama-release" || component->variant != variant) {
        throw std::runtime_error("The queued engine no longer matches its reviewed catalog installer strategy.");
    }
    const bool dependency_required = variant == "cuda12" || variant == "cuda13";
    const auto expected_dependency_id = "llama.cpp-cudart-" + variant;
    const bool dependency_declared = std::any_of(component->dependencies.begin(), component->dependencies.end(),
        [&](const catalog::ComponentDependency& dependency) {
            return dependency.id == expected_dependency_id && dependency.kind == "runtime-archive";
        });
    if (dependency_required != dependency_declared) {
        throw std::runtime_error("The engine's required CUDA runtime dependency is missing or does not match the selected backend in the reviewed manifest.");
    }
    mutate_job(job, [&](DownloadManager::Job& current) {
        current.state = "downloading";
        current.phase = dependency_required
            ? "Resolving llama.cpp server and required " + expected_dependency_id + " dependency"
            : "Finding a compatible official llama.cpp CPU package and SHA-256 metadata";
    });
    const auto assets = resolve_engine_assets(variant);
    if (dependency_required && !assets.cuda_runtime) {
        throw std::runtime_error("The official release did not provide the required matching CUDA runtime dependency.");
    }
    if (!dependency_required && assets.cuda_runtime) {
        throw std::runtime_error("A CUDA runtime archive was resolved for a CPU-only engine package; refusing an unexpected dependency.");
    }
    const auto total_download_size = assets.total_size();
    log_diagnostic("INFO", "engine.asset.selected", "variant=" + variant + " tag=" + assets.engine.tag +
        " engine_asset=" + assets.engine.name + " engine_bytes=" + std::to_string(assets.engine.size) +
        " engine_sha256=" + assets.engine.sha256 +
        (assets.cuda_runtime ? " cuda_runtime_asset=" + assets.cuda_runtime->name +
            " cuda_runtime_bytes=" + std::to_string(assets.cuda_runtime->size) +
            " cuda_runtime_sha256=" + assets.cuda_runtime->sha256 : std::string{}));
    mutate_job(job, [&](DownloadManager::Job& current) {
        current.version = assets.engine.tag;
        current.total_bytes = total_download_size;
        current.downloaded_bytes = 0;
        current.phase = "Downloading official llama.cpp " + assets.engine.tag +
            (assets.engine.prerelease ? " prerelease package" : " stable package") +
            (assets.cuda_runtime ? " and its matching CUDA runtime libraries" : "");
    });
    const auto data_root = user_data_directory();
    const auto stage_root = data_root / "staging" / job->id;
    std::error_code fs_error;
    std::filesystem::create_directories(data_root / "staging", fs_error);
    if (fs_error) throw std::runtime_error("Could not create the component staging directory: " + fs_error.message());
    const auto available = std::filesystem::space(data_root / "staging", fs_error);
    // CUDA installs may include a large server archive plus a separate runtime
    // archive. Both stay in staging while their extracted payload is assembled,
    // so budget conservatively for the downloads and expansion rather than only
    // the compressed transfer size.
    const auto required_free_space = total_download_size * 3ULL + 512ULL * 1024ULL * 1024ULL;
    if (fs_error || available.available < required_free_space) {
        throw std::runtime_error("Not enough free disk space to install llama.cpp safely. Free space for at least three times the combined archive size plus 512 MB, then retry. No engine files were activated.");
    }
    std::filesystem::remove_all(stage_root, fs_error);
    fs_error.clear();
    std::filesystem::create_directories(stage_root, fs_error);
    if (fs_error) throw std::runtime_error("Could not create a staging directory for llama.cpp: " + fs_error.message());
    const auto archive_path = stage_root / assets.engine.name;
    download_to_file(assets.engine.url, archive_path, assets.engine.size, job, 0, total_download_size);
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "verifying"; current.phase = "Verifying official llama.cpp archive SHA-256"; });
    if (sha256_file(archive_path) != assets.engine.sha256) {
        std::filesystem::remove_all(stage_root, fs_error);
        throw std::runtime_error("llama.cpp archive SHA-256 did not match the official release API. The archive was discarded.");
    }
    const auto extracted = stage_root / "extracted";
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "extracting"; current.phase = "Safely extracting the verified engine archive"; });
    extract_archive_safely(archive_path, extracted, job);

    if (assets.cuda_runtime) {
        const auto runtime_archive_path = stage_root / assets.cuda_runtime->name;
        mutate_job(job, [&](DownloadManager::Job& current) {
            current.state = "downloading";
            current.phase = "Downloading matching CUDA runtime libraries";
        });
        download_to_file(assets.cuda_runtime->url, runtime_archive_path, assets.cuda_runtime->size,
                         job, assets.engine.size, total_download_size);
        mutate_job(job, [](DownloadManager::Job& current) {
            current.state = "verifying";
            current.phase = "Verifying CUDA runtime SHA-256";
        });
        if (sha256_file(runtime_archive_path) != assets.cuda_runtime->sha256) {
            std::filesystem::remove_all(stage_root, fs_error);
            throw std::runtime_error("CUDA runtime archive SHA-256 did not match the official release API. Both downloaded archives were discarded.");
        }
        mutate_job(job, [](DownloadManager::Job& current) {
            current.state = "extracting";
            current.phase = "Safely extracting the verified CUDA runtime libraries";
        });
        const auto runtime_extracted = stage_root / "cuda-runtime-extracted";
        extract_archive_safely(runtime_archive_path, runtime_extracted, job);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(runtime_extracted)) {
            const auto relative = entry.path().lexically_relative(runtime_extracted);
            if (relative.empty() || relative.is_absolute() || relative.has_root_name()) {
                throw std::runtime_error("CUDA runtime archive produced an invalid relative path.");
            }
            for (const auto& part : relative) {
                if (part == "..") throw std::runtime_error("CUDA runtime archive path escaped its extraction directory.");
            }
            const auto destination = extracted / relative;
            if (entry.is_directory()) {
                std::filesystem::create_directories(destination, fs_error);
                if (fs_error) throw std::runtime_error("Could not merge CUDA runtime directories: " + fs_error.message());
                continue;
            }
            if (!entry.is_regular_file()) throw std::runtime_error("CUDA runtime archive contains an unsupported file type.");
            std::filesystem::create_directories(destination.parent_path(), fs_error);
            if (fs_error) throw std::runtime_error("Could not create a destination for a CUDA runtime library: " + fs_error.message());
            if (std::filesystem::exists(destination)) {
                if (sha256_file(destination) != sha256_file(entry.path())) {
                    throw std::runtime_error("A CUDA runtime library conflicts with a file in the server archive: " + relative.string());
                }
            } else {
                std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::none, fs_error);
                if (fs_error) throw std::runtime_error("Could not install a verified CUDA runtime library: " + fs_error.message());
            }
        }
        log_diagnostic("INFO", "engine.cuda_runtime.merged", "variant=" + variant + " asset=" + assets.cuda_runtime->name);
    }

    const auto server_path = find_llama_server(extracted);
    if (!server_path) throw std::runtime_error("The verified archive set did not contain llama-server (or llama-server.exe).");
    auto safe_tag = assets.engine.tag;
    if (!safe_tag.empty() && safe_tag.front() == 'v') safe_tag.erase(safe_tag.begin());
    const auto final_dir = data_root / "engines" / "llama.cpp" / (safe_tag + "-" + variant);
    std::filesystem::create_directories(final_dir.parent_path(), fs_error);
    if (fs_error) throw std::runtime_error("Could not create the engine installation directory: " + fs_error.message());
    if (std::filesystem::exists(final_dir)) throw std::runtime_error("This llama.cpp version/backend is already installed; choose another catalog version when an upgrade is available.");
    std::filesystem::rename(extracted, final_dir, fs_error);
    if (fs_error) throw std::runtime_error("Could not activate the extracted engine directory: " + fs_error.message());
    const auto installed_server = find_llama_server(final_dir);
    if (!installed_server) throw std::runtime_error("Could not find llama-server after extraction.");
    std::filesystem::remove_all(stage_root, fs_error);
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "configuring"; current.phase = "Registering installed llama.cpp package without changing the active runtime profile"; });
    try {
        configure_llama_engine(*installed_server, {}, variant);
    } catch (...) {
        // Do not leave an orphaned directory that the catalog would mark as
        // installed if configuration persistence failed.
        std::error_code rollback_error;
        std::filesystem::remove_all(final_dir, rollback_error);
        throw;
    }
    mutate_job(job, [&](DownloadManager::Job& current) { current.installed_path = path_to_utf8(final_dir); });
}

void DownloadManager::configure_llama_engine(const std::filesystem::path& executable,
                                             const std::filesystem::path& model,
                                             const std::string& variant) {
    (void)model; // Model installation/selection is deliberately independent from engine installation.
    auto candidates = engines_.configurations();
    const auto existing = std::find_if(candidates.begin(), candidates.end(), [](const EngineConfig& engine) {
        return engine.id == "llama-cpp";
    });
    // Additional CPU/CUDA packages are installed side-by-side. They must not
    // silently replace the backend currently used by Chat.
    if (existing != candidates.end() && !existing->executable_path.empty()) return;
    if (config_.source_path.empty()) throw std::runtime_error("The runtime configuration path is unknown; cannot register the downloaded engine.");

    EngineConfig engine;
    if (existing != candidates.end()) engine = *existing;
    engine.id = "llama-cpp";
    engine.display_name = "llama.cpp server";
    engine.executable_path = executable;
    engine.model_path.clear(); // The user explicitly selects an installed model in Runtime Profile.
    engine.working_directory = executable.parent_path();
    const auto port = configured_engine_port(engine);
    const auto context = config_.runtime_profile.context_size >= 512U ? config_.runtime_profile.context_size : 2048U;
    const bool use_gpu = variant == "cuda12" || variant == "cuda13";
    engine.args = {"--host", "127.0.0.1", "--port", std::to_string(port), "--ctx-size", std::to_string(context),
                   "--n-gpu-layers", use_gpu ? "auto" : "0", "--cors-origins", "localhost",
                   "--no-cors-credentials", "-m", "{model_path}"};
    engine.upstream_base_url = "http://127.0.0.1:" + std::to_string(port);
    engine.capabilities = {"chat"};
    engine.default_for_chat = true;
    for (auto& current : candidates) current.default_for_chat = false;
    const auto new_existing = std::find_if(candidates.begin(), candidates.end(), [](const EngineConfig& item) {
        return item.id == "llama-cpp";
    });
    if (new_existing == candidates.end()) candidates.push_back(engine);
    else *new_existing = engine;

    RuntimeConfig next_config = config_;
    next_config.engines = candidates;
    const auto component = std::find_if(catalog::components().begin(), catalog::components().end(), [&](const catalog::ComponentDefinition& item) {
        return item.kind == "engine" && item.installer == "llama-release" && item.variant == variant;
    });
    if (component != catalog::components().end()) next_config.runtime_profile.engine_component_id = component->id;
    // Do not pick a model automatically. The profile remains incomplete until
    // the user explicitly chooses an installed model from the Models screen.
    next_config.runtime_profile.model_component_id.clear();
    const auto persist = [&](std::string& persist_error) {
        try { save_config(config_.source_path, next_config); return true; }
        catch (const std::exception& ex) { persist_error = std::string("Could not save engine configuration: ") + ex.what(); return false; }
    };
    std::string error;
    if (!engines_.replace_configs(candidates, persist, error)) throw std::runtime_error("Could not register the downloaded llama.cpp engine: " + error);
    config_ = std::move(next_config);
}

} // namespace lar
