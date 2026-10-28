#include "lar/downloads.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
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
    std::uint64_t downloaded_bytes{0};
    std::uint64_t total_bytes{0};
    std::atomic<bool> cancel_requested{false};
    mutable std::mutex mutex;
};

namespace {
namespace pt = boost::property_tree;

constexpr char kGitHubLatestReleaseUrl[] = "https://api.github.com/repos/ggml-org/llama.cpp/releases/latest";
constexpr char kGitHubAssetPrefix[] = "https://github.com/ggml-org/llama.cpp/releases/download/";
constexpr char kModelUrl[] = "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/dd26da440ef0330c47919d1ecae0966d24022222/qwen2.5-1.5b-instruct-q4_k_m.gguf";
constexpr char kModelSha256[] = "6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e";
constexpr std::uint64_t kModelSize = 1117320736ULL;
constexpr char kModelFileName[] = "qwen2.5-1.5b-instruct-q4_k_m.gguf";
constexpr std::uint64_t kMaximumArchiveExpandedBytes = 3ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumReleaseAssetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

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
};

bool archive_asset_extension(const std::string& name) {
    return name.size() >= 4 && (name.substr(name.size() - 4) == ".zip" ||
        (name.size() >= 7 && name.substr(name.size() - 7) == ".tar.gz") ||
        (name.size() >= 4 && name.substr(name.size() - 4) == ".tgz"));
}

bool is_supported_engine_asset_name(const std::string& raw_name, const std::string& variant) {
    const auto name = lower_ascii(raw_name);
    if (!archive_asset_extension(name) || contains(name, "debug") || contains(name, "source")) return false;
    const auto architecture = host_architecture();
    if (architecture == "x64") {
        if (!contains(name, "x64") || contains(name, "arm64") || contains(name, "aarch64") || contains(name, "s390x")) return false;
    } else if (architecture == "arm64") {
        if (!contains(name, "arm64") && !contains(name, "aarch64")) return false;
    } else {
        return false;
    }
    const auto os = host_os();
    if (os == "windows") {
        if (!contains(name, "win")) return false;
        if (variant == "cpu") return contains(name, "cpu") && !contains(name, "cuda") && !contains(name, "vulkan") && !contains(name, "rocm");
        if (variant == "cuda12") return contains(name, "cuda") && (contains(name, "12.4") || contains(name, "cuda-12") || contains(name, "cuda12"));
        if (variant == "cuda13") return contains(name, "cuda") && (contains(name, "13.4") || contains(name, "cuda-13") || contains(name, "cuda13"));
        return false;
    }
    if (os == "linux") {
        if (!contains(name, "ubuntu") && !contains(name, "linux")) return false;
        if (variant == "cpu") {
            return !contains(name, "cuda") && !contains(name, "vulkan") && !contains(name, "rocm") &&
                   !contains(name, "openvino") && !contains(name, "sycl") && !contains(name, "metal");
        }
        if (variant == "cuda12") return contains(name, "cuda") && (contains(name, "12.8") || contains(name, "cuda-12") || contains(name, "cuda12"));
        if (variant == "cuda13") return contains(name, "cuda") && (contains(name, "13.4") || contains(name, "cuda-13") || contains(name, "cuda13"));
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

ReleaseAsset resolve_engine_asset(const std::string& variant) {
    const auto body = fetch_https_text(kGitHubLatestReleaseUrl, 10U * 1024U * 1024U);
    std::istringstream input(body);
    pt::ptree release;
    try { pt::read_json(input, release); }
    catch (const std::exception& ex) { throw std::runtime_error(std::string("Could not parse the official llama.cpp release metadata: ") + ex.what()); }
    if (release.get<bool>("draft", false) || release.get<bool>("prerelease", false)) {
        throw std::runtime_error("The official llama.cpp endpoint did not return a stable release.");
    }
    ReleaseAsset selected;
    selected.tag = release.get<std::string>("tag_name", "");
    if (selected.tag.empty() || selected.tag.size() > 64 ||
        !std::all_of(selected.tag.begin(), selected.tag.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '.' || c == '-' || c == 'v';
        })) {
        throw std::runtime_error("The official llama.cpp release tag is missing or unsafe.");
    }
    const auto assets = release.get_child_optional("assets");
    if (!assets) throw std::runtime_error("The latest stable llama.cpp release has no downloadable assets.");
    for (const auto& entry : *assets) {
        const auto& node = entry.second;
        const auto name = node.get<std::string>("name", "");
        if (!is_supported_engine_asset_name(name, variant)) continue;
        const auto url = node.get<std::string>("browser_download_url", "");
        if (url.rfind(kGitHubAssetPrefix, 0) != 0) throw std::runtime_error("The selected llama.cpp asset URL is outside the official GitHub release namespace.");
        auto digest = node.get<std::string>("digest", "");
        if (digest.rfind("sha256:", 0) == 0) digest = digest.substr(7);
        if (!is_hex_digest(digest)) {
            throw std::runtime_error("The official release metadata does not provide a SHA-256 digest for " + name + "; refusing to install an unverified engine archive.");
        }
        const auto size = node.get<std::uint64_t>("size", 0);
        if (size == 0 || size > kMaximumReleaseAssetBytes) throw std::runtime_error("The selected llama.cpp asset has an invalid or unsupported archive size.");
        if (!selected.name.empty()) throw std::runtime_error("More than one upstream release asset matched this platform/backend; the catalog needs to be updated before installation.");
        selected.name = name;
        selected.url = url;
        selected.sha256 = lower_ascii(digest);
        selected.size = size;
    }
    if (selected.name.empty()) {
        throw std::runtime_error("No verified llama.cpp release archive matches this OS, architecture and backend. Try the CPU option or check the official release assets.");
    }
    return selected;
}

void download_to_file(const std::string& url, const std::filesystem::path& destination,
                      std::uint64_t expected_size, const std::shared_ptr<DownloadManager::Job>& job);

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

void mutate_job(const std::shared_ptr<DownloadManager::Job>& job,
                const std::function<void(DownloadManager::Job&)>& fn) {
    std::lock_guard<std::mutex> lock(job->mutex);
    fn(*job);
}

DownloadJobSnapshot snapshot_job(const std::shared_ptr<DownloadManager::Job>& job) {
    std::lock_guard<std::mutex> lock(job->mutex);
    return {job->id, job->item_id, job->kind, job->state, job->phase, job->version,
            job->installed_path, job->error, job->downloaded_bytes, job->total_bytes};
}

void download_to_file(const std::string& url, const std::filesystem::path& destination,
                      std::uint64_t expected_size, const std::shared_ptr<DownloadManager::Job>& job) {
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
    context.progress = [job](std::uint64_t received, std::uint64_t total) {
        mutate_job(job, [&](DownloadManager::Job& current) {
            current.downloaded_bytes = received;
            if (total > 0) current.total_bytes = total;
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
    if (expected_size > 0) mutate_job(job, [&](DownloadManager::Job& current) { current.total_bytes = expected_size; });
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
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Could not finalize downloaded file: " + fs_error.message());
    }
}

} // namespace

DownloadManager::DownloadManager(RuntimeConfig config, EngineManager& engines)
    : config_(std::move(config)), engines_(engines) {}

DownloadManager::~DownloadManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& job : jobs_) job->cancel_requested.store(true);
    }
    for (auto& worker : workers_) if (worker.thread.joinable()) worker.thread.join();
}

std::filesystem::path DownloadManager::user_data_directory() {
#ifdef _WIN32
    std::wstring value(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) {
        value.resize(length);
        return std::filesystem::path(value) / L"LocalAIRuntime";
    }
    return std::filesystem::temp_directory_path() / L"LocalAIRuntime";
#elif defined(__APPLE__)
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "local-ai-runtime";
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / "Library" / "Application Support" / "LocalAIRuntime";
    return std::filesystem::temp_directory_path() / "LocalAIRuntime";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "local-ai-runtime";
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local" / "share" / "local-ai-runtime";
    return std::filesystem::temp_directory_path() / "local-ai-runtime";
#endif
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
    const auto model_path = root / "models" / kModelFileName;
    std::error_code fs_error;
    bool model_installed = false;
    if (std::filesystem::is_regular_file(model_path, fs_error) && !fs_error) {
        const auto model_size = std::filesystem::file_size(model_path, fs_error);
        if (!fs_error && model_size == kModelSize) {
            try {
                // Catalog UI must not label a same-size but corrupted file as verified.
                model_installed = sha256_file(model_path) == kModelSha256;
            } catch (...) {
                model_installed = false;
            }
        }
    }
    const auto engine_root = root / "engines" / "llama.cpp";
    const auto os = host_os();
    const auto arch = host_architecture();
    // Only show combinations whose upstream asset naming/availability is covered by
    // the current selector. Linux ARM64 is intentionally hidden until a specific
    // official release asset is included in CI validation.
    const bool can_download_cpu =
        ((os == "windows" || os == "linux") && arch == "x64") ||
        (os == "macos" && (arch == "x64" || arch == "arm64"));
    const bool can_download_cuda12 = (os == "windows" || os == "linux") && arch == "x64";
    const bool can_download_cuda13 = (os == "windows" || os == "linux") && arch == "x64";
    std::ostringstream out;
    out << "{\"data\":[";
    bool first = true;
    const auto write_engine = [&](const std::string& id, const std::string& variant, const std::string& title,
                                  const std::string& backend, bool supported) {
        if (!supported) return;
        if (!first) out << ',';
        first = false;
        std::filesystem::path installed_dir;
        std::error_code scan_error;
        if (std::filesystem::exists(engine_root, scan_error) && !scan_error) {
            for (std::filesystem::directory_iterator it(engine_root, scan_error), end; it != end && !scan_error; it.increment(scan_error)) {
                if (!it->is_directory(scan_error) || scan_error) { scan_error.clear(); continue; }
                const auto candidate_name = it->path().filename().string();
                const auto suffix = std::string("-") + variant;
                if (candidate_name.size() >= suffix.size() && candidate_name.compare(candidate_name.size() - suffix.size(), suffix.size(), suffix) == 0 && find_llama_server(it->path())) {
                    installed_dir = it->path();
                    break;
                }
            }
        }
        const bool installed = !installed_dir.empty();
        out << "{\"id\":" << json_quote(id)
            << ",\"type\":\"engine\",\"name\":" << json_quote(title)
            << ",\"description\":" << json_quote("Downloads the latest stable official llama.cpp server archive, verifies the SHA-256 digest published by GitHub, safely extracts it and registers the engine.")
            << ",\"version\":\"latest-stable\",\"backend\":" << json_quote(backend)
            << ",\"license\":\"MIT\",\"license_url\":\"https://github.com/ggml-org/llama.cpp/blob/master/LICENSE\""
            << ",\"source_url\":\"https://github.com/ggml-org/llama.cpp/releases\""
            << ",\"size_bytes\":0,\"installed\":" << (installed ? "true" : "false")
            << ",\"installed_path\":" << json_quote(installed ? path_to_utf8(installed_dir) : std::string{}) << '}';
    };
    write_engine("llama.cpp-cpu", "cpu", "llama.cpp server — CPU", "cpu", can_download_cpu);
    write_engine("llama.cpp-cuda12", "cuda12", "llama.cpp server — NVIDIA CUDA 12", "cuda12", can_download_cuda12);
    write_engine("llama.cpp-cuda13", "cuda13", "llama.cpp server — NVIDIA CUDA 13", "cuda13", can_download_cuda13);
    if (!first) out << ',';
    out << "{\"id\":\"qwen2.5-1.5b-q4km\",\"type\":\"model\",\"name\":\"Qwen2.5 1.5B Instruct — Q4_K_M\","
        << "\"description\":\"Small starter chat model in GGUF format. The download is about 1.12 GB; context and runtime memory add to this.\","
        << "\"version\":\"dd26da440ef0330c47919d1ecae0966d24022222\",\"backend\":\"gguf\","
        << "\"license\":\"Apache-2.0\",\"license_url\":\"https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/blob/dd26da440ef0330c47919d1ecae0966d24022222/LICENSE\","
        << "\"source_url\":\"https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF\","
        << "\"size_bytes\":" << kModelSize << ",\"sha256\":" << json_quote(kModelSha256)
        << ",\"installed\":" << (model_installed ? "true" : "false")
        << ",\"installed_path\":" << json_quote(model_installed ? path_to_utf8(model_path) : std::string{}) << "}]}";
    return out.str();
}

std::string DownloadManager::jobs_json() const {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs = jobs_;
    }
    std::ostringstream out;
    out << "{\"data\":[";
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        if (i) out << ',';
        const auto job = snapshot_job(jobs[i]);
        const auto percent = job.total_bytes > 0 ? std::min<std::uint64_t>(100, (job.downloaded_bytes * 100ULL) / job.total_bytes)
                          : (job.state == "succeeded" ? 100ULL : 0ULL);
        out << "{\"id\":" << json_quote(job.id) << ",\"item_id\":" << json_quote(job.item_id)
            << ",\"kind\":" << json_quote(job.kind) << ",\"state\":" << json_quote(job.state)
            << ",\"phase\":" << json_quote(job.phase) << ",\"version\":" << json_quote(job.version)
            << ",\"downloaded_bytes\":" << job.downloaded_bytes << ",\"total_bytes\":" << job.total_bytes
            << ",\"progress_percent\":" << percent << ",\"installed_path\":" << json_quote(job.installed_path)
            << ",\"error\":" << json_quote(job.error) << '}';
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
    for (const auto& job : jobs) {
        const auto view = snapshot_job(job);
        if (view.id != id) continue;
        const auto percent = view.total_bytes > 0 ? std::min<std::uint64_t>(100, (view.downloaded_bytes * 100ULL) / view.total_bytes)
                          : (view.state == "succeeded" ? 100ULL : 0ULL);
        return "{\"id\":" + json_quote(view.id) + ",\"item_id\":" + json_quote(view.item_id) +
            ",\"kind\":" + json_quote(view.kind) + ",\"state\":" + json_quote(view.state) +
            ",\"phase\":" + json_quote(view.phase) + ",\"version\":" + json_quote(view.version) +
            ",\"downloaded_bytes\":" + std::to_string(view.downloaded_bytes) +
            ",\"total_bytes\":" + std::to_string(view.total_bytes) +
            ",\"progress_percent\":" + std::to_string(percent) +
            ",\"installed_path\":" + json_quote(view.installed_path) + ",\"error\":" + json_quote(view.error) + "}";
    }
    return std::nullopt;
}

bool DownloadManager::start(const std::string& item_id, bool accept_license,
                            std::string& job_id, std::string& error) {
    if (!accept_license) { error = "Confirm that you reviewed the upstream license before downloading."; return false; }
    if (item_id != "llama.cpp-cpu" && item_id != "llama.cpp-cuda12" && item_id != "llama.cpp-cuda13" && item_id != "qwen2.5-1.5b-q4km") {
        error = "This item is not in the built-in reviewed catalog.";
        return false;
    }
    if (any_engine_running(engines_)) {
        error = "Stop running engines before installing or changing runtime components.";
        return false;
    }
    const auto os = host_os();
    const auto arch = host_architecture();
    if (item_id == "llama.cpp-cpu" &&
        !(((os == "windows" || os == "linux") && arch == "x64") ||
          (os == "macos" && (arch == "x64" || arch == "arm64")))) {
        error = "A verified official CPU server archive is not catalogued for this OS/architecture yet.";
        return false;
    }
    if ((item_id == "llama.cpp-cuda12" || item_id == "llama.cpp-cuda13") &&
        !((os == "windows" || os == "linux") && arch == "x64")) {
        error = "A verified official CUDA server archive is not catalogued for this OS/architecture yet.";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reap_finished_workers_locked();
        for (const auto& existing : jobs_) {
            const auto view = snapshot_job(existing);
            const bool active = view.state == "queued" || view.state == "downloading" || view.state == "verifying" ||
                                view.state == "extracting" || view.state == "configuring";
            if (!active) continue;
            error = view.item_id == item_id
                ? "This catalog item is already being installed in job " + view.id + "."
                : "Another component installation is active in job " + view.id + ". Finish or cancel it before starting another download.";
            return false;
        }
        // Keep the in-memory history and joinable thread handles bounded during
        // long-lived app sessions. Recent terminal jobs remain visible for diagnosis.
        constexpr std::size_t max_job_history = 64;
        while (jobs_.size() >= max_job_history) {
            const auto terminal = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& candidate) {
                const auto state = snapshot_job(candidate).state;
                return state == "succeeded" || state == "failed" || state == "canceled";
            });
            if (terminal == jobs_.end()) {
                error = "Download job history is full and no completed job can be removed yet. Restart Local AI Runtime to clear the history.";
                return false;
            }
            jobs_.erase(terminal);
        }
        auto job = std::make_shared<Job>();
        job->id = "download-" + std::to_string(next_job_number_++);
        job->item_id = item_id;
        job->kind = item_id.rfind("llama.cpp-", 0) == 0 ? "engine" : "model";
        job_id = job->id;
        try {
            workers_.reserve(workers_.size() + 1);
            jobs_.push_back(job);
            workers_.push_back(Worker{job->id, std::thread([this, job] { run_job(job); })});
        } catch (const std::exception& ex) {
            // If the job was added but thread creation failed, remove it from the
            // list. reserve() occurs before constructing the thread, so the move
            // into workers_ cannot require another vector allocation.
            const auto found = std::find(jobs_.begin(), jobs_.end(), job);
            if (found != jobs_.end()) jobs_.erase(found);
            error = std::string("Could not start download worker: ") + ex.what();
            return false;
        }
    }
    return true;
}

void DownloadManager::reap_finished_workers_locked() {
    for (auto it = workers_.begin(); it != workers_.end();) {
        const auto job_it = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto& job) {
            return job->id == it->job_id;
        });
        bool finished = job_it == jobs_.end();
        if (!finished) {
            const auto state = snapshot_job(*job_it).state;
            finished = state == "succeeded" || state == "failed" || state == "canceled";
        }
        if (!finished) {
            ++it;
            continue;
        }
        if (it->thread.joinable()) it->thread.join();
        it = workers_.erase(it);
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
        if (view.state == "queued" || view.state == "downloading" || view.state == "verifying" ||
            view.state == "extracting" || view.state == "configuring") return true;
    }
    return false;
}

bool DownloadManager::cancel(const std::string& job_id, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& job : jobs_) {
        if (job->id != job_id) continue;
        std::lock_guard<std::mutex> job_lock(job->mutex);
        if (job->state == "succeeded" || job->state == "failed" || job->state == "canceled") {
            error = "This download job has already finished.";
            return false;
        }
        job->cancel_requested.store(true);
        job->phase = "Canceling";
        error.clear();
        return true;
    }
    error = "Unknown download job id.";
    return false;
}

void DownloadManager::run_job(const std::shared_ptr<Job>& job) {
    try {
        if (job->cancel_requested.load()) throw std::runtime_error("Download canceled by the user.");
        if (job->item_id == "qwen2.5-1.5b-q4km") download_model(job);
        else if (job->item_id == "llama.cpp-cpu") download_engine(job, "cpu");
        else if (job->item_id == "llama.cpp-cuda12") download_engine(job, "cuda12");
        else if (job->item_id == "llama.cpp-cuda13") download_engine(job, "cuda13");
        else throw std::runtime_error("Unknown catalog item.");
        mutate_job(job, [](DownloadManager::Job& current) { current.state = "succeeded"; current.phase = "Installed and configured"; current.downloaded_bytes = current.total_bytes > 0 ? current.total_bytes : current.downloaded_bytes; });
    } catch (const std::exception& ex) {
        const bool canceled = job->cancel_requested.load() || std::string_view(ex.what()).find("canceled") != std::string_view::npos || std::string_view(ex.what()).find("Canceled") != std::string_view::npos;
        mutate_job(job, [&](DownloadManager::Job& current) {
            current.state = canceled ? "canceled" : "failed";
            current.phase = canceled ? "Canceled" : "Failed";
            current.error = ex.what();
        });
        std::error_code ignored;
        const auto root = user_data_directory();
        std::filesystem::remove_all(root / "staging" / job->id, ignored);
        if (job->item_id == "qwen2.5-1.5b-q4km") {
            ignored.clear();
            std::filesystem::remove(root / "models" / (std::string(kModelFileName) + ".part"), ignored);
            ignored.clear();
            std::filesystem::remove(root / "models" / (std::string(kModelFileName) + ".part.part"), ignored);
        }
    }
}

void DownloadManager::download_model(const std::shared_ptr<Job>& job) {
    const auto root = user_data_directory();
    const auto directory = root / "models";
    const auto final_path = directory / kModelFileName;
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "downloading"; current.phase = "Downloading Qwen2.5 1.5B Instruct Q4_K_M (about 1.12 GB)"; current.total_bytes = kModelSize; });
    std::error_code fs_error;
    std::filesystem::create_directories(directory, fs_error);
    if (fs_error) throw std::runtime_error("Could not create the model directory: " + fs_error.message());
    const auto space = std::filesystem::space(directory, fs_error);
    if (fs_error || space.available < kModelSize + 256ULL * 1024ULL * 1024ULL) {
        throw std::runtime_error("Not enough free disk space. At least 1.4 GB is required for this model download and staging.");
    }
    if (std::filesystem::exists(final_path)) {
        bool existing_model_verified = false;
        try {
            const auto size = std::filesystem::file_size(final_path, fs_error);
            existing_model_verified = !fs_error && size == kModelSize && sha256_file(final_path) == kModelSha256;
        } catch (...) {
            existing_model_verified = false;
        }
        if (existing_model_verified) {
            mutate_job(job, [&](DownloadManager::Job& current) { current.downloaded_bytes = kModelSize; current.total_bytes = kModelSize; current.installed_path = path_to_utf8(final_path); current.phase = "Model already installed and verified"; });
            const auto configs = engines_.configurations();
            const auto current = std::find_if(configs.begin(), configs.end(), [](const EngineConfig& e) { return e.id == "llama-cpp" && !e.executable_path.empty(); });
            if (current != configs.end()) configure_llama_engine(current->executable_path, final_path, configured_gpu_layers(*current) == "0" ? "cpu" : "gpu");
            return;
        }
        fs_error.clear();
        std::filesystem::remove(final_path, fs_error);
        if (fs_error) throw std::runtime_error("An existing model file failed verification but could not be removed: " + fs_error.message());
    }
    auto partial = final_path;
    partial += ".part";
    std::filesystem::remove(partial, fs_error);
    download_to_file(kModelUrl, partial, kModelSize, job);
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "verifying"; current.phase = "Verifying model size and SHA-256"; });
    const auto actual_size = std::filesystem::file_size(partial, fs_error);
    if (fs_error || actual_size != kModelSize) throw std::runtime_error("Downloaded model size does not match the pinned catalog.");
    if (sha256_file(partial) != kModelSha256) {
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Model SHA-256 did not match the catalog. The partial file was discarded.");
    }
    // The magic is checked as a second guard after the content hash.
    std::ifstream check(partial, std::ios::binary);
    char magic[4]{};
    check.read(magic, 4);
    if (check.gcount() != 4 || std::string_view(magic, 4) != "GGUF") {
        std::filesystem::remove(partial, fs_error);
        throw std::runtime_error("Downloaded file does not have a GGUF header.");
    }
    std::filesystem::rename(partial, final_path, fs_error);
    if (fs_error) throw std::runtime_error("Could not finalize the verified model file: " + fs_error.message());
    mutate_job(job, [&](DownloadManager::Job& current) { current.installed_path = path_to_utf8(final_path); });
    const auto configs = engines_.configurations();
    const auto current = std::find_if(configs.begin(), configs.end(), [](const EngineConfig& e) { return e.id == "llama-cpp" && !e.executable_path.empty(); });
    if (current != configs.end()) configure_llama_engine(current->executable_path, final_path, configured_gpu_layers(*current) == "0" ? "cpu" : "gpu");
}

void DownloadManager::download_engine(const std::shared_ptr<Job>& job, const std::string& variant) {
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "downloading"; current.phase = "Resolving official stable llama.cpp release and SHA-256 metadata"; });
    const auto asset = resolve_engine_asset(variant);
    mutate_job(job, [&](DownloadManager::Job& current) {
        current.version = asset.tag;
        current.total_bytes = asset.size;
        current.phase = "Downloading official llama.cpp " + asset.tag + " package";
    });
    const auto data_root = user_data_directory();
    const auto stage_root = data_root / "staging" / job->id;
    std::error_code fs_error;
    std::filesystem::create_directories(data_root / "staging", fs_error);
    if (fs_error) throw std::runtime_error("Could not create the component staging directory: " + fs_error.message());
    const auto available = std::filesystem::space(data_root / "staging", fs_error);
    if (fs_error || available.available < asset.size + 512ULL * 1024ULL * 1024ULL) {
        throw std::runtime_error("Not enough free disk space to download and extract llama.cpp. At least the archive size plus 512 MB is required.");
    }
    std::filesystem::remove_all(stage_root, fs_error);
    fs_error.clear();
    std::filesystem::create_directories(stage_root, fs_error);
    if (fs_error) throw std::runtime_error("Could not create a staging directory for llama.cpp: " + fs_error.message());
    const auto archive_path = stage_root / (asset.name);
    download_to_file(asset.url, archive_path, asset.size, job);
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "verifying"; current.phase = "Verifying official GitHub release SHA-256"; });
    if (sha256_file(archive_path) != asset.sha256) {
        std::filesystem::remove_all(stage_root, fs_error);
        throw std::runtime_error("llama.cpp archive SHA-256 did not match the official release API. The archive was discarded.");
    }
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "extracting"; current.phase = "Safely extracting the verified engine archive"; });
    const auto extracted = stage_root / "extracted";
    extract_archive_safely(archive_path, extracted, job);
    const auto server_path = find_llama_server(extracted);
    if (!server_path) throw std::runtime_error("The verified archive did not contain llama-server (or llama-server.exe).");
    auto safe_tag = asset.tag;
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
    const auto known_model = data_root / "models" / kModelFileName;
    std::filesystem::path model_for_config;
    std::error_code model_error;
    if (std::filesystem::is_regular_file(known_model, model_error) && !model_error) {
        try {
            if (std::filesystem::file_size(known_model, model_error) == kModelSize && !model_error && sha256_file(known_model) == kModelSha256) {
                model_for_config = known_model;
            }
        } catch (...) {
            model_for_config.clear();
        }
    }
    mutate_job(job, [](DownloadManager::Job& current) { current.state = "configuring"; current.phase = "Registering llama.cpp with Local AI Runtime"; });
    try {
        configure_llama_engine(*installed_server, model_for_config, variant);
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
    if (any_engine_running(engines_)) throw std::runtime_error("Stop all running engines before the installer updates config/engines.json.");
    if (config_.source_path.empty()) throw std::runtime_error("The runtime configuration path is unknown; cannot register the downloaded engine.");
    auto candidates = engines_.configurations();
    auto existing = std::find_if(candidates.begin(), candidates.end(), [](const EngineConfig& e) { return e.id == "llama-cpp"; });
    EngineConfig engine;
    if (existing != candidates.end()) engine = *existing;
    engine.id = "llama-cpp";
    engine.display_name = "llama.cpp server";
    if (!executable.empty()) engine.executable_path = executable;
    if (!model.empty()) engine.model_path = model;
    if (engine.model_path.empty()) {
        const auto default_model = user_data_directory() / "models" / kModelFileName;
        std::error_code ec;
        if (std::filesystem::is_regular_file(default_model, ec) && !ec) engine.model_path = default_model;
    }
    if (engine.executable_path.empty()) return; // Model can be downloaded before an engine is installed.
    engine.working_directory = engine.executable_path.parent_path();
    const auto port = configured_engine_port(engine);
    const auto context = configured_context_size(engine);
    const bool use_gpu = variant == "cuda12" || variant == "cuda13" || variant == "gpu" || variant == "native";
    engine.args = {"--host", "127.0.0.1", "--port", std::to_string(port), "--ctx-size", std::to_string(context),
                   "--n-gpu-layers", use_gpu ? "auto" : "0", "-m", "{model_path}"};
    engine.upstream_base_url = "http://127.0.0.1:" + std::to_string(port);
    engine.capabilities = {"chat"};
    engine.default_for_chat = true;
    for (auto& current : candidates) current.default_for_chat = false;
    existing = std::find_if(candidates.begin(), candidates.end(), [](const EngineConfig& e) { return e.id == "llama-cpp"; });
    if (existing == candidates.end()) candidates.push_back(engine);
    else *existing = engine;
    RuntimeConfig next_config = config_;
    next_config.engines = candidates;
    std::string error;
    const auto persist = [&](std::string& persist_error) {
        try { save_config(config_.source_path, next_config); return true; }
        catch (const std::exception& ex) { persist_error = std::string("Could not save engine configuration: ") + ex.what(); return false; }
    };
    if (!engines_.replace_configs(candidates, persist, error)) throw std::runtime_error("Could not register the downloaded llama.cpp engine: " + error);
    config_.engines = std::move(candidates);
}

} // namespace lar
