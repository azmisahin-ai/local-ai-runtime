#include "lar/server.hpp"
#include "lar/downloads.hpp"
#include "lar/diagnostics.hpp"
#include "lar/hardware.hpp"
#include "lar/backend_evidence.hpp"
#include "lar/gpu_monitor.hpp"

#ifndef LAR_VERSION
#define LAR_VERSION "0.1.0-dev"
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <regex>
#include <set>
#include <cctype>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>
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
namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

volatile std::sig_atomic_t shutdown_requested = 0;
void request_shutdown(int) { shutdown_requested = 1; }

std::string runtime_data_root_id() {
    const char* value = std::getenv("LOCAL_AI_RUNTIME_DATA_ROOT_ID");
    if (!value) return {};
    const std::string id(value);
    static const std::regex guid_pattern(R"(^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$)");
    return std::regex_match(id, guid_pattern) ? id : std::string{};
}

std::string json_escape(std::string_view input) {
    std::ostringstream out;
    for (const unsigned char ch : input) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20U) {
                    constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(ch >> 4U) & 0x0fU] << hex[ch & 0x0fU];
                } else out << static_cast<char>(ch);
        }
    }
    return out.str();
}

std::string json_quoted(std::string_view value) { return "\"" + json_escape(value) + "\""; }


std::string backend_package_from_path(const EngineConfig& config) {
    auto value = config.executable_path.generic_string();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value.find("cuda12") != std::string::npos) return "cuda12-package";
    if (value.find("cuda13") != std::string::npos) return "cuda13-package";
    if (value.find("-cpu") != std::string::npos || value.find("/cpu/") != std::string::npos || value.find("\\cpu\\") != std::string::npos) return "cpu-package";
    return "manual-or-unknown-package";
}

std::string engine_backend_target(const EngineConfig& config) {
    const auto option = std::find(config.args.begin(), config.args.end(), "--n-gpu-layers");
    if (option == config.args.end() || std::next(option) == config.args.end()) return "unspecified";
    return *std::next(option) == "0" ? "cpu" : "gpu-requested";
}

std::string hardware_json(const HardwareInfo& info) {
    std::ostringstream out;
    out << "{\"platform\":" << json_quoted(info.platform)
        << ",\"architecture\":" << json_quoted(info.architecture)
        << ",\"logical_cpu_count\":" << info.logical_cpu_count
        << ",\"total_memory_bytes\":" << info.total_memory_bytes
        << ",\"available_memory_bytes\":" << info.available_memory_bytes
        << ",\"available_disk_bytes\":" << info.available_disk_bytes
        << ",\"gpu_probe_status\":" << json_quoted(info.gpu_probe_status)
        << ",\"gpu_probe_message\":" << json_quoted(info.gpu_probe_message)
        << ",\"gpus\":[";
    for (std::size_t i = 0; i < info.gpus.size(); ++i) {
        if (i) out << ',';
        const auto& gpu = info.gpus[i];
        out << "{\"name\":" << json_quoted(gpu.name)
            << ",\"vendor\":" << json_quoted(gpu.vendor)
            << ",\"vendor_id\":" << json_quoted(gpu.vendor_id)
            << ",\"device_id\":" << json_quoted(gpu.device_id)
            << ",\"dedicated_memory_bytes\":" << gpu.dedicated_memory_bytes << '}';
    }
    out << "],\"memory_profile\":";
    const auto gib = 1024ULL * 1024ULL * 1024ULL;
    if (info.total_memory_bytes == 0) out << json_quoted("unknown");
    else if (info.total_memory_bytes < 8ULL * gib) out << json_quoted("low-memory");
    else if (info.total_memory_bytes < 16ULL * gib) out << json_quoted("baseline");
    else out << json_quoted("memory-headroom");
    out << ",\"recommendation\":";
    if (info.total_memory_bytes == 0) {
        out << json_quoted("Memory could not be measured. Start with the CPU baseline and verify each model on this computer.");
    } else if (info.total_memory_bytes < 8ULL * gib) {
        out << json_quoted("Low-memory starting point: choose small quantized models and a modest context size. Larger models may fail even when a GPU is present.");
    } else if (info.total_memory_bytes < 16ULL * gib) {
        out << json_quoted("Baseline starting point: begin with small quantized models. Model size, available memory, context and GPU memory still decide compatibility.");
    } else {
        out << json_quoted("System memory headroom detected. This alone does not guarantee a model fits; check available RAM, GPU memory, quantization and context size.");
    }
    out << ",\"gpu_use_requires_validation\":true,\"recommendations\":[";
    const auto mib = 1024ULL * 1024ULL;
    const auto minimum_setup_disk = 3ULL * gib;
    std::string cpu_status = "recommended";
    std::string cpu_reason;
    if (info.available_disk_bytes == 0) {
        cpu_status = "unknown";
        cpu_reason = "Free disk space could not be measured for the model storage volume. Check available space before starting a large download.";
    } else if (info.available_disk_bytes < minimum_setup_disk) {
        cpu_status = "needs-space";
        cpu_reason = "Less than 3 GiB is available on the model storage volume. Free more space before downloading the starter model; keep additional room for temporary files.";
    } else if (info.total_memory_bytes == 0 || info.available_memory_bytes == 0) {
        cpu_status = "unknown";
        cpu_reason = "The operating system did not provide a reliable memory reading. The pinned Qwen2.5 1.5B Q4_K_M file is about 1.12 GB; runtime memory depends on context and other applications.";
    } else if (info.total_memory_bytes < 4ULL * gib || info.available_memory_bytes < 1536ULL * mib) {
        cpu_status = "resource-constrained";
        cpu_reason = "Available memory is tight for the starter model. It may still run with a modest context, but close other applications first; this is a heuristic, not a guarantee.";
    } else if (info.total_memory_bytes < 8ULL * gib || info.available_memory_bytes < 3ULL * gib) {
        cpu_status = "caution";
        cpu_reason = "The starter CPU model is a reasonable first try, but memory headroom is limited. Keep context modest and verify a real chat task on this machine.";
    } else {
        cpu_status = "recommended";
        cpu_reason = "Best conservative starting point in the current catalogue. The model file is about 1.12 GB; runtime memory also depends on context and other applications. This is not a compatibility guarantee.";
    }
    out << "{\"id\":\"starter-chat-cpu\",\"title\":\"Start with local chat\","
        << "\"status\":" << json_quoted(cpu_status)
        << ",\"backend\":\"CPU\",\"engine\":\"llama.cpp server — CPU\","
        << "\"model\":\"Qwen2.5 1.5B Instruct · Q4_K_M\","
        << "\"download_bytes\":1117320736,\"evidence\":\"hardware-heuristic\","
        << "\"reason\":" << json_quoted(cpu_reason) << "}";

    const GpuAdapterInfo* nvidia = nullptr;
    for (const auto& gpu : info.gpus) {
        if (gpu.vendor == "NVIDIA" || gpu.vendor_id == "0x10de" || gpu.vendor_id == "0x10DE") {
            if (!nvidia || gpu.dedicated_memory_bytes > nvidia->dedicated_memory_bytes) nvidia = &gpu;
        }
    }
    out << ",";
    std::string cuda_status;
    std::string cuda_reason;
    if (info.gpu_probe_status != "complete") {
        cuda_status = "unknown";
        cuda_reason = "The operating system did not provide a complete graphics-adapter inventory. Do not infer GPU availability; the CPU setup remains the fallback.";
    } else if (!nvidia) {
        cuda_status = "not-detected";
        cuda_reason = "No NVIDIA adapter was reported by the operating system. Keep using the CPU setup; other GPU backends are not yet available in this catalogue.";
    } else if (nvidia->dedicated_memory_bytes < 4ULL * gib) {
        cuda_status = "review";
        cuda_reason = "An NVIDIA adapter was detected, but dedicated VRAM is below 4 GiB or not reported. Partial offload may work, but model fit and driver compatibility need a real task test.";
    } else {
        cuda_status = "candidate";
        cuda_reason = "An NVIDIA adapter with at least 4 GiB reported dedicated VRAM was detected. This is only a screening candidate: driver compatibility, offloaded layers and a real chat task must still be verified.";
    }
    out << "{\"id\":\"nvidia-cuda-chat\",\"title\":\"NVIDIA GPU acceleration\","
        << "\"status\":" << json_quoted(cuda_status)
        << ",\"backend\":\"NVIDIA CUDA\",\"engine\":\"llama.cpp server — CUDA 12/13\","
        << "\"model\":\"Qwen2.5 1.5B Instruct · Q4_K_M\",\"download_bytes\":null,"
        << "\"evidence\":\"hardware-screen-only\",\"reason\":" << json_quoted(cuda_reason) << "}]}";
    return out.str();
}

bool is_loopback_host(const std::string& header, unsigned short port) {
    const auto expected_port = ":" + std::to_string(port);
    return header == "localhost" + expected_port || header == "127.0.0.1" + expected_port ||
           (port == 80 && (header == "localhost" || header == "127.0.0.1"));
}

bool origin_is_same_loopback(const std::string& origin, unsigned short port) {
    if (origin.empty()) return true;
    const auto expected_port = ":" + std::to_string(port);
    if (origin == "http://127.0.0.1" + expected_port || origin == "http://localhost" + expected_port) return true;
    return port == 80 && (origin == "http://127.0.0.1" || origin == "http://localhost");
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

#ifdef _WIN32
std::filesystem::path current_executable_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) return {};
    buffer.resize(count);
    return std::filesystem::path(buffer);
}

bool windows_desktop_shell_installed(const std::filesystem::path& web_dir) {
    try {
        return std::filesystem::is_regular_file(web_dir / L"desktop-shell.json");
    } catch (...) {
        return false;
    }
}

bool windows_update_apply_supported(const std::filesystem::path& web_dir,
                                   std::filesystem::path* install_root_out = nullptr) {
    // Preview/development builds may come from a newer source commit than the
    // latest public tag. Never offer an in-place update that could replace a
    // preview with an older release package merely because its semver sorts higher.
    if (std::string_view(LAR_VERSION).find("-dev") != std::string_view::npos) return false;
    // Desktop installations need the full setup installer: the portable ZIP
    // must not update only the C++ runtime and leave the desktop shell stale.
    if (windows_desktop_shell_installed(web_dir)) return false;
    try {
        const auto executable = current_executable_path();
        if (executable.empty()) return false;
        const auto root = std::filesystem::weakly_canonical(executable.parent_path());
        const auto actual_web = std::filesystem::weakly_canonical(std::filesystem::absolute(web_dir));
        const auto expected_web = std::filesystem::weakly_canonical(root / L"web");
        if (actual_web != expected_web) return false;
        if (!std::filesystem::is_regular_file(root / L"local-ai-runtime.exe") ||
            !std::filesystem::is_regular_file(root / L"web" / L"index.html") ||
            !std::filesystem::is_regular_file(root / L"scripts" / L"Apply-LocalAIRuntimeUpdate.ps1")) return false;
        if (install_root_out) *install_root_out = root;
        return true;
    } catch (...) {
        return false;
    }
}

std::wstring quote_windows_argument(const std::wstring& value) {
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

http::status start_windows_update_apply(const std::string& tag_name,
                                       const RuntimeConfig& config,
                                       DownloadManager& downloads,
                                       const std::filesystem::path& web_dir,
                                       std::string& error) {
    static const std::regex valid_tag(R"(^v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$)");
    if (!std::regex_match(tag_name, valid_tag)) {
        error = "The update tag is invalid.";
        return http::status::unprocessable_entity;
    }
    if (!DownloadManager::is_newer_release_tag(tag_name, LAR_VERSION)) {
        error = "The staged release is no longer newer than the installed runtime. Nothing was changed.";
        return http::status::conflict;
    }
    if (downloads.has_active_jobs()) {
        error = "Finish or cancel active downloads before applying a runtime update.";
        return http::status::conflict;
    }
    std::filesystem::path install_root;
    if (!windows_update_apply_supported(web_dir, &install_root)) {
        error = "Apply-and-restart is available only from a portable Windows release package. This development layout can stage verified packages but will not modify its files.";
        return http::status::not_implemented;
    }

    const auto expected_asset = DownloadManager::runtime_update_asset_name();
    const auto data_root = DownloadManager::user_data_directory();
    const auto stage_root = data_root / L"updates" / std::filesystem::path(tag_name);
    const auto manifest_path = stage_root / L"staged-update.json";
    const auto package_path = stage_root / std::filesystem::path(expected_asset);
    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(manifest_path, fs_error) || fs_error ||
        !std::filesystem::is_regular_file(package_path, fs_error) || fs_error) {
        error = "No verified staged update exists for this release. Download and verify it first.";
        return http::status::not_found;
    }

    try {
        std::istringstream input(read_text_file(manifest_path));
        boost::property_tree::ptree metadata;
        boost::property_tree::read_json(input, metadata);
        const auto manifest_tag = metadata.get<std::string>("tag_name", "");
        const auto asset_name = metadata.get<std::string>("asset_name", "");
        auto digest = metadata.get<std::string>("sha256", "");
        const auto expected_size = metadata.get<std::uint64_t>("size_bytes", 0);
        const auto verified = metadata.get<bool>("verified", false);
        if (digest.rfind("sha256:", 0) == 0) digest.erase(0, 7);
        std::transform(digest.begin(), digest.end(), digest.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!verified || manifest_tag != tag_name || asset_name != expected_asset ||
            !std::regex_match(digest, std::regex(R"(^[0-9a-f]{64}$)"))) {
            error = "The staged update manifest is invalid, unsupported or does not match this platform/release.";
            return http::status::unprocessable_entity;
        }
        const auto actual_size = std::filesystem::file_size(package_path, fs_error);
        if (fs_error || actual_size != expected_size || DownloadManager::sha256_file(package_path) != digest) {
            error = "The staged package no longer matches its verified size and SHA-256 manifest. Download it again before applying.";
            return http::status::unprocessable_entity;
        }
        const auto helper_source = install_root / L"scripts" / L"Apply-LocalAIRuntimeUpdate.ps1";
        const auto helper_copy = stage_root / L"Apply-LocalAIRuntimeUpdate.ps1";
        if (!std::filesystem::is_regular_file(helper_source, fs_error) || fs_error) {
            error = "The update helper is missing from this package. No files were changed.";
            return http::status::not_implemented;
        }
        std::filesystem::copy_file(helper_source, helper_copy, std::filesystem::copy_options::overwrite_existing, fs_error);
        if (fs_error) {
            error = "Could not prepare the isolated update helper: " + fs_error.message();
            return http::status::internal_server_error;
        }

        WCHAR windows_dir[MAX_PATH + 1]{};
        const UINT windows_len = GetWindowsDirectoryW(windows_dir, MAX_PATH);
        if (windows_len == 0 || windows_len >= MAX_PATH) {
            error = "Could not locate Windows PowerShell to run the verified update helper.";
            return http::status::internal_server_error;
        }
        const auto powershell = std::filesystem::path(windows_dir) / L"System32" / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
        if (!std::filesystem::is_regular_file(powershell, fs_error) || fs_error) {
            error = "Windows PowerShell was not found; the update remains staged and no files were changed.";
            return http::status::not_implemented;
        }
        std::vector<std::wstring> arguments = {
            L"-NoLogo", L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-WindowStyle", L"Hidden",
            L"-File", helper_copy.wstring(),
            L"-InstallRoot", install_root.wstring(),
            L"-DataRoot", data_root.wstring(),
            L"-StageRoot", stage_root.wstring(),
            L"-PackagePath", package_path.wstring(),
            L"-ManifestPath", manifest_path.wstring(),
            L"-TagName", std::wstring(tag_name.begin(), tag_name.end()),
            L"-AssetName", std::wstring(expected_asset.begin(), expected_asset.end()),
            L"-ExpectedSha256", std::wstring(digest.begin(), digest.end()),
            L"-RuntimePid", std::to_wstring(GetCurrentProcessId()),
            L"-Port", std::to_wstring(config.port),
            L"-CurrentVersion", std::wstring(LAR_VERSION, LAR_VERSION + std::char_traits<char>::length(LAR_VERSION))
        };
        std::wstring command = quote_windows_argument(powershell.wstring());
        for (const auto& argument : arguments) { command.push_back(L' '); command += quote_windows_argument(argument); }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        const auto working_directory = data_root.wstring();
        const BOOL created = CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE,
                                             CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                                             working_directory.c_str(), &startup, &process);
        if (!created) {
            error = "Could not start the isolated update helper (Windows error " + std::to_string(GetLastError()) + "). The verified package is still staged.";
            return http::status::internal_server_error;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        log_diagnostic("INFO", "update.apply_started", "tag=" + tag_name + " platform=windows helper=detached");
        return http::status::accepted;
    } catch (const std::exception& ex) {
        error = std::string("Could not validate or start the staged update: ") + ex.what();
        return http::status::unprocessable_entity;
    }
}
#endif

std::string staged_update_status_json(const std::string& tag_name,
                                      const std::filesystem::path& web_dir) {
    static const std::regex valid_tag(R"(^v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$)");
    const auto asset_name = DownloadManager::runtime_update_asset_name();
    const auto data_root = DownloadManager::user_data_directory();
    const auto stage_root = data_root / "updates" / std::filesystem::path(tag_name);
    const auto manifest_path = stage_root / "staged-update.json";
    const auto package_path = stage_root / std::filesystem::path(asset_name);
    bool staged = false;
    bool apply_supported = false;
    std::string apply_state;
    std::string apply_message;
    std::uint64_t package_size = 0;

    if (std::regex_match(tag_name, valid_tag) && !asset_name.empty()) {
        std::error_code fs_error;
        if (std::filesystem::is_regular_file(manifest_path, fs_error) && !fs_error &&
            std::filesystem::is_regular_file(package_path, fs_error) && !fs_error) {
            try {
                std::istringstream input(read_text_file(manifest_path));
                boost::property_tree::ptree metadata;
                boost::property_tree::read_json(input, metadata);
                const auto manifest_tag = metadata.get<std::string>("tag_name", "");
                const auto manifest_asset = metadata.get<std::string>("asset_name", "");
                auto digest = metadata.get<std::string>("sha256", "");
                const auto expected_size = metadata.get<std::uint64_t>("size_bytes", 0);
                const auto verified = metadata.get<bool>("verified", false);
                if (digest.rfind("sha256:", 0) == 0) digest.erase(0, 7);
                std::transform(digest.begin(), digest.end(), digest.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                const auto actual_size = std::filesystem::file_size(package_path, fs_error);
                staged = verified && manifest_tag == tag_name && manifest_asset == asset_name &&
                    std::regex_match(digest, std::regex(R"(^[0-9a-f]{64}$)")) && !fs_error &&
                    actual_size == expected_size && DownloadManager::sha256_file(package_path) == digest;
                if (staged) {
                    package_size = actual_size;
#ifdef _WIN32
                    apply_supported = windows_update_apply_supported(web_dir);
#else
                    (void)web_dir;
#endif
                }
            } catch (...) {
                staged = false;
                apply_supported = false;
            }
        }
    }

    const auto status_path = stage_root / "apply-status.json";
    std::error_code status_error;
    if (std::filesystem::is_regular_file(status_path, status_error) && !status_error) {
        try {
            std::istringstream input(read_text_file(status_path));
            boost::property_tree::ptree status;
            boost::property_tree::read_json(input, status);
            if (status.get<std::string>("tag_name", "") == tag_name) {
                apply_state = status.get<std::string>("state", "");
                apply_message = status.get<std::string>("message", "");
            }
        } catch (...) {
            // A malformed status record must not be trusted or break dashboard loading.
        }
    }

    return "{\"tag_name\":" + json_quoted(tag_name) +
        ",\"staged\":" + std::string(staged ? "true" : "false") +
        ",\"verified\":" + std::string(staged ? "true" : "false") +
        ",\"apply_supported\":" + std::string(apply_supported ? "true" : "false") +
        ",\"size_bytes\":" + std::to_string(package_size) +
        ",\"apply_state\":" + json_quoted(apply_state) +
        ",\"apply_message\":" + json_quoted(apply_message) + "}";
}

std::string read_engine_log_tails(const RuntimeConfig& config) {
    constexpr std::uintmax_t max_bytes_per_log = 256U * 1024U;
    constexpr std::size_t max_lines_per_log = 120;
    const auto log_directory = runtime_logs_directory();
    std::ostringstream report;
    bool included_any = false;
    for (const auto& engine : config.engines) {
        if (!is_valid_engine_id(engine.id)) continue;
        const auto path = log_directory / (engine.id + ".log");
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) continue;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) continue;
        std::ifstream input(path, std::ios::binary);
        if (!input) continue;
        if (size > max_bytes_per_log) {
            input.seekg(static_cast<std::streamoff>(size - max_bytes_per_log), std::ios::beg);
            std::string discarded;
            std::getline(input, discarded); // the first line may start mid-record
        }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(std::move(line));
            if (lines.size() > max_lines_per_log) lines.erase(lines.begin());
        }
        if (lines.empty()) continue;
        included_any = true;
        report << "-- engine " << engine.id << " --\n";
        for (const auto& item : lines) report << sanitize_diagnostic_text(item) << '\n';
    }
    return included_any ? report.str() : std::string("No engine process log files were available.\n");
}

bool upstream_is_ready(const EngineStatus& engine);

std::string engine_status_json(const EngineStatus& engine) {
    auto gpu_evidence = inspect_gpu_execution_evidence(engine.config.id);
    const auto backend_target = engine_backend_target(engine.config);
    if (backend_target == "cpu") {
        // The active config explicitly requests CPU execution. Do not let an old
        // CUDA session's appended log lines appear as evidence for this config.
        gpu_evidence.state = "not-verified";
        gpu_evidence.summary = "The current engine configuration explicitly requests CPU execution. GPU offload evidence from another configuration is not used for this status.";
        gpu_evidence.cuda_runtime_reported = false;
        gpu_evidence.offload_count_found = false;
        gpu_evidence.offloaded_layers = -1;
        gpu_evidence.total_layers = -1;
    }
    const bool chat_capable = std::find(engine.config.capabilities.begin(), engine.config.capabilities.end(), "chat") != engine.config.capabilities.end();
    const bool model_missing = chat_capable && engine.config.model_path.empty();
    const bool ready = !model_missing && upstream_is_ready(engine);
    std::string readiness = "stopped";
    if (engine.config.executable_path.empty()) readiness = "not-configured";
    else if (model_missing) readiness = "model-not-configured";
    else if (engine.state == ProcessState::failed) readiness = "failed";
    else if (engine.state == ProcessState::running) readiness = ready ? "ready" : "loading-or-unavailable";
    std::ostringstream out;
    out << "{\"id\":" << json_quoted(engine.config.id)
        << ",\"display_name\":" << json_quoted(engine.config.display_name)
        << ",\"status\":" << json_quoted(to_string(engine.state))
        << ",\"pid\":" << engine.pid
        << ",\"exit_code\":" << engine.exit_code
        << ",\"last_error\":" << json_quoted(engine.last_error)
        << ",\"executable_configured\":" << (engine.config.executable_path.empty() ? "false" : "true")
        << ",\"model_configured\":" << (engine.config.model_path.empty() ? "false" : "true")
        << ",\"default_for_chat\":" << (engine.config.default_for_chat ? "true" : "false")
        << ",\"ready\":" << (ready ? "true" : "false")
        << ",\"readiness\":" << json_quoted(readiness)
        << ",\"upstream_base_url\":" << json_quoted(engine.config.upstream_base_url)
        << ",\"backend_target\":" << json_quoted(backend_target)
        << ",\"backend_package\":" << json_quoted(backend_package_from_path(engine.config))
        << ",\"gpu_execution_evidence\":" << json_quoted(gpu_evidence.state)
        << ",\"gpu_execution_evidence_summary\":" << json_quoted(gpu_evidence.summary)
        << ",\"engine_log_available\":" << (gpu_evidence.engine_log_available ? "true" : "false")
        << ",\"gpu_evidence_current_session\":" << (gpu_evidence.current_session_found ? "true" : "false")
        << ",\"cuda_runtime_reported\":" << (gpu_evidence.cuda_runtime_reported ? "true" : "false")
        << ",\"gpu_offloaded_layers\":";
    if (gpu_evidence.offloaded_layers >= 0) out << gpu_evidence.offloaded_layers; else out << "null";
    out << ",\"gpu_total_layers\":";
    if (gpu_evidence.total_layers >= 0) out << gpu_evidence.total_layers; else out << "null";
    out << ",\"capabilities\":[";
    for (std::size_t i = 0; i < engine.config.capabilities.size(); ++i) {
        if (i) out << ',';
        out << json_quoted(engine.config.capabilities[i]);
    }
    out << "]}";
    return out.str();
}

http::response<http::string_body> json_response(http::status status, unsigned version,
                                                const std::string& body) {
    http::response<http::string_body> response{status, version};
    response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
    response.set(http::field::content_type, "application/json; charset=utf-8");
    response.set("X-Content-Type-Options", "nosniff");
    response.set("Cache-Control", "no-store");
    response.body() = body;
    response.keep_alive(false);
    response.prepare_payload();
    return response;
}

http::response<http::string_body> error_response(http::status status, unsigned version,
                                                  const std::string& message) {
    return json_response(status, version, "{\"error\":{\"message\":" + json_quoted(message) + "}}");
}

std::string nullable_int_json(int value) {
    return value >= 0 ? std::to_string(value) : std::string("null");
}

std::string gpu_observation_json(const NvidiaGpuObservation& observation,
                                 std::string_view engine_id,
                                 std::string_view backend_target,
                                 const GpuExecutionEvidence& evidence) {
    std::ostringstream out;
    out << "{\"task_succeeded\":" << (observation.task_succeeded ? "true" : "false")
        << ",\"telemetry_available\":" << (observation.telemetry_available ? "true" : "false")
        << ",\"status\":" << json_quoted(observation.status)
        << ",\"message\":" << json_quoted(observation.message)
        << ",\"elapsed_ms\":" << observation.elapsed_ms
        << ",\"samples_during_request\":" << observation.samples_during_request
        << ",\"engine_id\":" << json_quoted(engine_id)
        << ",\"backend_target\":" << json_quoted(backend_target)
        << ",\"startup_evidence\":" << json_quoted(evidence.state)
        << ",\"startup_evidence_summary\":" << json_quoted(evidence.summary)
        << ",\"devices\":[";
    for (std::size_t i = 0; i < observation.devices.size(); ++i) {
        if (i) out << ',';
        const auto& device = observation.devices[i];
        out << "{\"index\":" << device.index
            << ",\"samples_during_request\":" << device.samples_during_request
            << ",\"peak_utilization_percent\":" << nullable_int_json(device.peak_utilization_percent)
            << ",\"memory_before_mib\":" << nullable_int_json(device.memory_before_mib)
            << ",\"memory_peak_mib\":" << nullable_int_json(device.memory_peak_mib)
            << ",\"memory_after_mib\":" << nullable_int_json(device.memory_after_mib)
            << ",\"memory_total_mib\":" << nullable_int_json(device.memory_total_mib) << '}';
    }
    out << "]}";
    return out.str();
}

std::string engine_id_from_path(const std::string& path, const std::string& action) {
    const std::string prefix = "/v1/engines/";
    const std::string suffix = "/" + action;
    if (path.rfind(prefix, 0) != 0 || path.size() <= prefix.size() + suffix.size()) return {};
    if (path.substr(path.size() - suffix.size()) != suffix) return {};
    const auto id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
    return is_valid_engine_id(id) ? id : std::string{};
}

std::string upstream_port(const std::string& base_url) {
    const auto colon = base_url.find_last_of(':');
    if (colon == std::string::npos || colon + 1 >= base_url.size()) return {};
    return base_url.substr(colon + 1);
}

bool probe_status(const std::string& port_text, const std::string& target, unsigned int& status_code) {
    try {
        std::size_t consumed = 0;
        const auto port_value = std::stoul(port_text, &consumed);
        if (consumed != port_text.size() || port_value == 0 || port_value > 65535) return false;
        asio::io_context ioc;
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::milliseconds(450));
        stream.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port_value)));
        http::request<http::empty_body> request{http::verb::get, target, 11};
        request.set(http::field::host, "127.0.0.1:" + port_text);
        request.set(http::field::user_agent, "local-ai-runtime-readiness/" LAR_VERSION);
        request.keep_alive(false);
        http::write(stream, request);
        beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(4096);
        http::read(stream, buffer, parser);
        status_code = parser.get().result_int();
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
        return true;
    } catch (...) {
        return false;
    }
}

bool upstream_is_ready(const EngineStatus& engine) {
    if (engine.state != ProcessState::running || engine.config.upstream_base_url.empty()) return false;
    const auto port_text = upstream_port(engine.config.upstream_base_url);
    if (port_text.empty()) return false;
    unsigned int status_code = 0;
    if (!probe_status(port_text, "/health", status_code)) return false;
    if (status_code == 200) return true;
    // Some older OpenAI-compatible engines do not implement /health.
    if (status_code == 404 && probe_status(port_text, "/v1/models", status_code)) return status_code == 200;
    return false;
}

std::filesystem::path path_from_utf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char ch : value) encoded.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(encoded);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path require_absolute_regular_file(const std::string& raw, const std::string& field) {
    const auto text_first = raw.find_first_not_of(" \t\r\n");
    const auto text_last = raw.find_last_not_of(" \t\r\n");
    if (text_first == std::string::npos) throw std::runtime_error(field + " is required.");
    auto normalized = raw.substr(text_first, text_last - text_first + 1);
    // Windows Explorer's "Copy as path" includes surrounding quotes. Accept
    // that common format while still passing the path as a single argument.
    if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
        normalized = normalized.substr(1, normalized.size() - 2);
    }
    const auto path = path_from_utf8(normalized);
    if (!path.is_absolute()) throw std::runtime_error(field + " must be an absolute path.");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        throw std::runtime_error(field + " does not point to an existing regular file.");
    }
    return path;
}

EngineConfig build_llama_config(const boost::property_tree::ptree& body, const RuntimeConfig& runtime) {
    const auto executable = require_absolute_regular_file(body.get<std::string>("executable_path", ""), "llama-server executable path");
    const auto model = require_absolute_regular_file(body.get<std::string>("model_path", ""), "GGUF model path");
    const auto executable_name = lower_ascii(executable.filename().string());
#ifdef _WIN32
    if (executable_name != "llama-server.exe" && executable_name != "llama-server") {
        throw std::runtime_error("Choose the official llama-server.exe executable, not a different program.");
    }
#else
    if (executable_name != "llama-server") {
        throw std::runtime_error("Choose the official llama-server executable, not a different program.");
    }
#endif
    if (lower_ascii(model.extension().string()) != ".gguf") {
        throw std::runtime_error("Model path must point to a .gguf file.");
    }
    std::error_code size_error;
    const auto model_size = std::filesystem::file_size(model, size_error);
    if (size_error || model_size < 4) throw std::runtime_error("GGUF model file is empty or too small to contain a header.");
    {
        std::ifstream model_input(model, std::ios::binary);
        char magic[4]{};
        model_input.read(magic, sizeof(magic));
        if (model_input.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
            std::string_view(magic, sizeof(magic)) != "GGUF") {
            throw std::runtime_error("Selected model does not start with the GGUF file signature.");
        }
    }

    const auto port = body.get<unsigned int>("engine_port", 8081U);
    if (port < 1024U || port > 65535U) throw std::runtime_error("Engine port must be between 1024 and 65535.");
    if (port == runtime.port) throw std::runtime_error("Engine port must differ from the Local AI Runtime port.");
    const auto context = body.get<unsigned int>("context_size", 2048U);
    if (context < 512U || context > 32768U) throw std::runtime_error("Context size must be between 512 and 32768 tokens.");
    const auto backend = body.get<std::string>("backend", "cpu");
    if (backend != "cpu" && backend != "gpu") throw std::runtime_error("Backend must be 'cpu' or 'gpu'.");

    EngineConfig engine;
    engine.id = "llama-cpp";
    engine.display_name = "llama.cpp server";
    engine.executable_path = executable;
    engine.model_path = model;
    engine.working_directory = executable.parent_path();
    engine.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                   "--ctx-size", std::to_string(context), "--n-gpu-layers",
                   backend == "gpu" ? "auto" : "0", "--cors-origins", "localhost",
                   "--no-cors-credentials", "-m", "{model_path}"};
    engine.upstream_base_url = "http://127.0.0.1:" + std::to_string(port);
    engine.capabilities = {"chat"};
    engine.default_for_chat = true;
    return engine;
}

struct ProxyResult {
    http::status status{http::status::bad_gateway};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body;
};

ProxyResult proxy_local_request(const EngineStatus& engine, http::verb method,
                                const std::string& target, const std::string& body) {
    ProxyResult result;
    if (engine.state != ProcessState::running) {
        result.status = http::status::service_unavailable;
        result.body = "{\"error\":{\"message\":\"Configured engine is not running. Start it from the dashboard first.\"}}";
        return result;
    }
    const auto port_text = upstream_port(engine.config.upstream_base_url);
    if (port_text.empty()) {
        result.body = "{\"error\":{\"message\":\"No valid local upstream_base_url is configured for this engine.\"}}";
        return result;
    }
    try {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::seconds(120));
        const auto endpoints = resolver.resolve("127.0.0.1", port_text);
        stream.connect(endpoints);
        http::request<http::string_body> request{method, target, 11};
        request.set(http::field::host, "127.0.0.1:" + port_text);
        request.set(http::field::user_agent, "local-ai-runtime/" LAR_VERSION);
        request.keep_alive(false);
        if (method == http::verb::post) request.set(http::field::content_type, "application/json");
        request.body() = body;
        request.prepare_payload();
        http::write(stream, request);

        beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(16U * 1024U * 1024U);
        http::read(stream, buffer, parser);
        auto upstream_response = parser.release();
        result.status = upstream_response.result();
        const auto content_type = upstream_response[http::field::content_type];
        if (!content_type.empty()) result.content_type = std::string(content_type.data(), content_type.size());
        result.body = std::move(upstream_response.body());
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    } catch (const std::exception& ex) {
        result.status = http::status::bad_gateway;
        result.body = "{\"error\":{\"message\":" + json_quoted(std::string("Could not reach local model server: ") + ex.what()) + "}}";
    }
    return result;
}

ProxyResult proxy_chat(const EngineStatus& engine, const std::string& body) {
    return proxy_local_request(engine, http::verb::post, "/v1/chat/completions", body);
}

bool has_nonempty_chat_choices(std::string_view response_body) {
    try {
        std::istringstream input{std::string(response_body)};
        boost::property_tree::ptree response;
        boost::property_tree::read_json(input, response);
        const auto choices = response.get_child_optional("choices");
        return choices && !choices->empty();
    } catch (...) {
        return false;
    }
}

ProxyResult proxy_models(const EngineStatus& engine) {
    return proxy_local_request(engine, http::verb::get, "/v1/models", {});
}

http::response<http::string_body> handle_request(const http::request<http::string_body>& request,
                                                  const RuntimeConfig& config,
                                                  EngineManager& engines,
                                                  DownloadManager& downloads,
                                                  const std::filesystem::path& web_dir) {
    const auto version = request.version();
    const std::string host = std::string(request[http::field::host].data(), request[http::field::host].size());
    if (!is_loopback_host(host, config.port)) return error_response(http::status::forbidden, version, "Host header is not allowed.");
    const std::string origin = std::string(request[http::field::origin].data(), request[http::field::origin].size());
    if (!origin_is_same_loopback(origin, config.port)) {
        return error_response(http::status::forbidden, version, "Cross-origin requests are disabled. Use the local dashboard origin.");
    }

    std::string target = std::string(request.target().data(), request.target().size());
    const auto query = target.find('?');
    const bool include_engine_logs = query != std::string::npos &&
        target.substr(query + 1).find("include_engine_logs=1") != std::string::npos;
    if (query != std::string::npos) target.resize(query);

    if (request.method() == http::verb::get && (target == "/" || target == "/index.html")) {
        const auto body = read_text_file(web_dir / "index.html");
        if (body.empty()) return error_response(http::status::not_found, version, "Dashboard file not found. Start from the repository root or pass --web-dir.");
        http::response<http::string_body> response{http::status::ok, version};
        response.set(http::field::content_type, "text/html; charset=utf-8");
        response.set("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self' https://api.github.com; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Referrer-Policy", "no-referrer");
        response.set("Cache-Control", "no-store");
        response.body() = body;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::get && (target == "/app.js" || target == "/style.css")) {
        const auto file = web_dir / (target == "/app.js" ? "app.js" : "style.css");
        const auto body = read_text_file(file);
        if (body.empty()) return error_response(http::status::not_found, version, "Dashboard asset not found.");
        http::response<http::string_body> response{http::status::ok, version};
        response.set(http::field::content_type, target == "/app.js" ? "text/javascript; charset=utf-8" : "text/css; charset=utf-8");
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Cache-Control", "no-store");
        response.body() = body;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::get && target == "/health") {
        return json_response(http::status::ok, version,
            "{\"status\":\"ok\",\"version\":" + json_quoted(LAR_VERSION) + ",\"bind\":\"loopback\"}");
    }
    if (request.method() == http::verb::get && target == "/v1/system/identity") {
        // This is an opaque per-user data-root identifier, not a filesystem path
        // or a credential. The desktop shell uses it to refuse attaching to a
        // stale runtime instance that happens to own the configured port.
        return json_response(http::status::ok, version,
            "{\"schema_version\":1,\"version\":" + json_quoted(LAR_VERSION) +
            ",\"data_root_id\":" + json_quoted(runtime_data_root_id()) + "}");
    }
    if (request.method() == http::verb::post && target == "/v1/runtime/shutdown") {
        log_diagnostic("INFO", "runtime.shutdown_requested", "source=local-launcher");
        return json_response(http::status::accepted, version, "{\"status\":\"shutting-down\"}");
    }
    if (request.method() == http::verb::get && target == "/version") {
        return json_response(http::status::ok, version,
            "{\"version\":" + json_quoted(LAR_VERSION) +
            ",\"repository\":\"https://github.com/azmisahin-ai/local-ai-runtime\"" +
            ",\"releases_api_url\":\"https://api.github.com/repos/azmisahin-ai/local-ai-runtime/releases?per_page=10\"" +
            ",\"releases_url\":\"https://github.com/azmisahin-ai/local-ai-runtime/releases\"" +
            ",\"update_asset_name\":" + json_quoted(DownloadManager::runtime_update_asset_name()) +
#ifdef _WIN32
            ",\"desktop_shell_installed\":" + std::string(windows_desktop_shell_installed(web_dir) ? "true" : "false") +
            ",\"desktop_installer_asset_name\":\"local-ai-runtime-windows-x64-setup.exe\"" +
            ",\"update_apply_supported\":" + std::string(windows_update_apply_supported(web_dir) ? "true" : "false") + "}");
#else
            ",\"desktop_shell_installed\":false,\"desktop_installer_asset_name\":\"\",\"update_apply_supported\":false}");
#endif
    }
    if (request.method() == http::verb::post && target == "/v1/updates/stage") {
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            const auto tag_name = body.get<std::string>("tag_name", "");
            const auto asset_name = body.get<std::string>("asset_name", "");
            const auto download_url = body.get<std::string>("browser_download_url", "");
            const auto digest = body.get<std::string>("digest", "");
            const auto expected_size = body.get<std::uint64_t>("size", 0);
            std::string job_id;
            std::string error;
            if (!downloads.start_update(LAR_VERSION, tag_name, asset_name, download_url, digest,
                                        expected_size, job_id, error)) {
                const auto status = error.find("active in job") != std::string::npos
                    ? http::status::conflict : http::status::unprocessable_entity;
                return error_response(status, version, error);
            }
            return json_response(http::status::accepted, version,
                "{\"job_id\":" + json_quoted(job_id) + ",\"tag_name\":" +
                json_quoted(tag_name) + ",\"status\":\"queued\"}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version,
                                  std::string("Invalid runtime update request JSON: ") + ex.what());
        } catch (const boost::property_tree::ptree_bad_data&) {
            return error_response(http::status::bad_request, version,
                                  "Runtime update request fields have invalid types.");
        }
    }
    if (request.method() == http::verb::post && target == "/v1/updates/status") {
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            const auto tag_name = body.get<std::string>("tag_name", "");
            static const std::regex valid_tag(R"(^v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$)");
            if (!std::regex_match(tag_name, valid_tag)) {
                return error_response(http::status::unprocessable_entity, version, "The update status tag is invalid.");
            }
            return json_response(http::status::ok, version, staged_update_status_json(tag_name, web_dir));
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version,
                                  std::string("Invalid update status request JSON: ") + ex.what());
        } catch (const boost::property_tree::ptree_bad_data&) {
            return error_response(http::status::bad_request, version,
                                  "The update status request fields have invalid types.");
        }
    }
    if (request.method() == http::verb::post && target == "/v1/updates/apply") {
#ifdef _WIN32
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            const auto tag_name = body.get<std::string>("tag_name", "");
            std::string error;
            const auto result = start_windows_update_apply(tag_name, config, downloads, web_dir, error);
            if (result != http::status::accepted) return error_response(result, version, error);
            return json_response(http::status::accepted, version,
                "{\"status\":\"applying\",\"tag_name\":" + json_quoted(tag_name) +
                ",\"message\":\"The verified update helper will shut down and restart the runtime.\"}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version,
                                  std::string("Invalid update apply request JSON: ") + ex.what());
        } catch (const boost::property_tree::ptree_bad_data&) {
            return error_response(http::status::bad_request, version,
                                  "The update apply request fields have invalid types.");
        }
#else
        return error_response(http::status::not_implemented, version,
                              "Verified package staging is available on this platform, but apply-and-restart is not yet supported. The current installation remains unchanged.");
#endif
    }
    if (request.method() == http::verb::get && target == "/v1/diagnostics/export") {
        log_diagnostic("INFO", "diagnostics.export_requested", std::string("source=dashboard engine_logs=") + (include_engine_logs ? "included" : "omitted"));
        const auto statuses = engines.statuses();
        std::ostringstream engine_json;
        engine_json << "{\"data\":[";
        for (std::size_t i = 0; i < statuses.size(); ++i) {
            if (i) engine_json << ',';
            engine_json << engine_status_json(statuses[i]);
        }
        engine_json << "]}";
        const auto engine_logs = include_engine_logs ? read_engine_log_tails(config) : std::string{};
        const auto report = build_diagnostic_report(LAR_VERSION, downloads.jobs_json(), engine_json.str(), engine_logs);
        http::response<http::string_body> response{http::status::ok, version};
        response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
        response.set(http::field::content_type, "text/plain; charset=utf-8");
        response.set(http::field::content_disposition, "attachment; filename=\"local-ai-runtime-diagnostics.txt\"");
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Cache-Control", "no-store");
        response.body() = report;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::post && target == "/v1/diagnostics/events") {
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            auto event = body.get<std::string>("event", "ui.unknown");
            auto detail = body.get<std::string>("detail", "");
            if (event.size() > 64 || detail.size() > 1200 || event.rfind("ui.", 0) != 0) {
                return error_response(http::status::unprocessable_entity, version, "Diagnostic event is outside the allowed format.");
            }
            const bool is_failure = event.ends_with(".failed") || event == "ui.error" || event == "ui.unhandledrejection";
            log_diagnostic(is_failure ? "WARN" : "INFO", event, detail);
            return json_response(http::status::accepted, version, "{\"status\":\"recorded-locally\"}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version, std::string("Invalid diagnostic event JSON: ") + ex.what());
        }
    }
    if (request.method() == http::verb::get && target == "/v1/models") {
        const auto engine = engines.default_chat_engine();
        if (!engine) return error_response(http::status::service_unavailable, version, "No chat engine is configured. Set upstream_base_url in config/engines.json.");
        const auto proxied = proxy_models(*engine);
        http::response<http::string_body> response{proxied.status, version};
        response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
        response.set(http::field::content_type, proxied.content_type);
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Cache-Control", "no-store");
        response.body() = proxied.body;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::get && target == "/v1/system/hardware") {
        const auto hardware = detect_hardware(DownloadManager::user_data_directory());
        std::ostringstream summary;
        summary << "platform=" << hardware.platform << " architecture=" << hardware.architecture
                << " logical_cpus=" << hardware.logical_cpu_count
                << " total_ram_bytes=" << hardware.total_memory_bytes
                << " available_ram_bytes=" << hardware.available_memory_bytes
                << " free_disk_bytes=" << hardware.available_disk_bytes
                << " gpu_probe=" << hardware.gpu_probe_status;
        for (const auto& gpu : hardware.gpus) {
            summary << " gpu=" << gpu.vendor << ':' << gpu.device_id
                    << ":vram_bytes=" << gpu.dedicated_memory_bytes;
        }
        log_diagnostic("INFO", "hardware.snapshot", summary.str());
        return json_response(http::status::ok, version, hardware_json(hardware));
    }
    if (request.method() == http::verb::post && target == "/v1/diagnostics/gpu-observation") {
        static std::mutex observation_mutex;
        std::unique_lock observation_lock(observation_mutex, std::try_to_lock);
        if (!observation_lock.owns_lock()) {
            return error_response(http::status::conflict, version, "A GPU observation is already running. Wait for it to finish before starting another.");
        }
        if (downloads.has_active_jobs()) {
            return error_response(http::status::conflict, version, "A component installation is active. Finish or cancel it before measuring GPU activity.");
        }
        const auto engine = engines.default_chat_engine();
        if (!engine) return error_response(http::status::service_unavailable, version, "Configure a local chat engine before running a GPU observation.");
        const auto backend_target = engine_backend_target(engine->config);
        if (backend_target != "gpu-requested") {
            return error_response(http::status::conflict, version, "Select a CUDA-enabled llama.cpp engine and set GPU layers to auto before running this observation. The test will not silently change your engine configuration.");
        }
        const auto states = engines.statuses();
        const auto state = std::find_if(states.begin(), states.end(), [&](const EngineStatus& item) {
            return item.config.id == engine->config.id;
        });
        if (state == states.end() || !upstream_is_ready(*state)) {
            return error_response(http::status::conflict, version, "Start the selected CUDA-enabled engine and wait until it is ready before running this observation.");
        }

        log_diagnostic("INFO", "gpu.observation.started", "engine=" + engine->config.id + " backend=" + backend_target);
        constexpr std::string_view probe_body = R"({"model":"local-model","messages":[{"role":"system","content":"This is a local runtime validation request. Answer the user directly in a helpful way."},{"role":"user","content":"Explain in eight concise sentences how a computer carries out a program. This is a local GPU observation check."}],"temperature":0.2,"max_tokens":192,"stream":false})";
        const auto observation = observe_nvidia_gpu_during([&] {
            const auto result = proxy_chat(*engine, std::string(probe_body));
            return result.status == http::status::ok && has_nonempty_chat_choices(result.body);
        });
        const auto evidence = inspect_gpu_execution_evidence(engine->config.id);
        std::ostringstream detail;
        detail << "engine=" << engine->config.id << " backend=" << backend_target
               << " task_succeeded=" << (observation.task_succeeded ? "true" : "false")
               << " measurement=" << observation.status
               << " samples=" << observation.samples_during_request
               << " elapsed_ms=" << observation.elapsed_ms;
        for (const auto& device : observation.devices) {
            detail << " gpu_index=" << device.index
                   << " samples=" << device.samples_during_request
                   << " peak_utilization_percent=" << device.peak_utilization_percent
                   << " memory_before_mib=" << device.memory_before_mib
                   << " memory_peak_mib=" << device.memory_peak_mib
                   << " memory_after_mib=" << device.memory_after_mib
                   << " memory_total_mib=" << device.memory_total_mib;
        }
        log_diagnostic(observation.task_succeeded ? "INFO" : "WARN", "gpu.observation.finished", detail.str());
        return json_response(http::status::ok, version,
            gpu_observation_json(observation, engine->config.id, backend_target, evidence));
    }
    if (request.method() == http::verb::get && target == "/v1/catalog") {
        return json_response(http::status::ok, version, downloads.catalog_json());
    }
    if (request.method() == http::verb::get && target == "/v1/downloads") {
        return json_response(http::status::ok, version, downloads.jobs_json());
    }
    constexpr std::string_view download_prefix = "/v1/downloads/";
    if (target.rfind(download_prefix, 0) == 0 && target.size() > download_prefix.size()) {
        const auto download_id = target.substr(download_prefix.size());
        if (download_id.find('/') == std::string::npos && request.method() == http::verb::get) {
            const auto job = downloads.job_json(download_id);
            if (!job) return error_response(http::status::not_found, version, "Download job was not found.");
            return json_response(http::status::ok, version, *job);
        }
        if (download_id.find('/') == std::string::npos && request.method() == http::verb::delete_) {
            std::string error;
            if (!downloads.cancel(download_id, error)) return error_response(http::status::conflict, version, error);
            return json_response(http::status::accepted, version, "{\"id\":" + json_quoted(download_id) + ",\"status\":\"canceling\"}");
        }
        if (download_id.find('/') == std::string::npos && request.method() == http::verb::post) {
            try {
                std::istringstream input(request.body());
                boost::property_tree::ptree body;
                boost::property_tree::read_json(input, body);
                const bool accept_license = body.get<bool>("accept_license", false);
                std::string job_id;
                std::string error;
                if (!downloads.start(download_id, accept_license, job_id, error)) {
                    const auto status = error.find("already being installed") != std::string::npos || error.find("running engines") != std::string::npos
                        ? http::status::conflict : http::status::unprocessable_entity;
                    return error_response(status, version, error);
                }
                return json_response(http::status::accepted, version,
                    "{\"job_id\":" + json_quoted(job_id) + ",\"item_id\":" + json_quoted(download_id) + ",\"status\":\"queued\"}");
            } catch (const boost::property_tree::json_parser_error& ex) {
                return error_response(http::status::bad_request, version, std::string("Invalid download request JSON: ") + ex.what());
            }
        }
    }
    if (request.method() == http::verb::get && target == "/v1/engines") {
        const auto status_list = engines.statuses();
        std::ostringstream body;
        body << "{\"data\":[";
        for (std::size_t i = 0; i < status_list.size(); ++i) {
            if (i) body << ',';
            body << engine_status_json(status_list[i]);
        }
        body << "]}";
        return json_response(http::status::ok, version, body.str());
    }
    if (request.method() == http::verb::post && target == "/v1/setup/llama-cpp") {
        if (downloads.has_active_jobs()) {
            return error_response(http::status::conflict, version, "A component installation is active. Finish or cancel it before changing engine configuration.");
        }
        if (config.source_path.empty()) {
            return error_response(http::status::internal_server_error, version, "The active configuration path is unknown; setup cannot be persisted.");
        }
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            const auto engine = build_llama_config(body, config);
            auto candidate_engines = engines.configurations();
            for (auto& configured : candidate_engines) configured.default_for_chat = false;
            const auto found = std::find_if(candidate_engines.begin(), candidate_engines.end(), [](const EngineConfig& value) {
                return value.id == "llama-cpp";
            });
            if (found == candidate_engines.end()) candidate_engines.push_back(engine);
            else *found = engine;

            RuntimeConfig next_config = config;
            next_config.engines = candidate_engines;
            std::string error;
            const auto persist = [&](std::string& persist_error) {
                try {
                    save_config(config.source_path, next_config);
                    return true;
                } catch (const std::exception& ex) {
                    persist_error = std::string("Could not save engine configuration: ") + ex.what();
                    return false;
                }
            };
            if (!engines.replace_configs(std::move(candidate_engines), persist, error)) {
                return error_response(http::status::conflict, version, error);
            }
            return json_response(http::status::ok, version,
                "{\"id\":\"llama-cpp\",\"status\":\"configured\",\"backend\":" + json_quoted(body.get<std::string>("backend", "cpu")) +
                ",\"ready\":false,\"config_saved\":true}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version, std::string("Invalid JSON setup request: ") + ex.what());
        } catch (const std::exception& ex) {
            return error_response(http::status::unprocessable_entity, version, ex.what());
        }
    }

    if (request.method() == http::verb::post) {
        const auto start_id = engine_id_from_path(target, "start");
        if (!start_id.empty()) {
            if (downloads.has_active_jobs()) {
                return error_response(http::status::conflict, version, "A component installation is active. Wait for it to finish before starting an engine.");
            }
            std::string error;
            if (!engines.start(start_id, error)) return error_response(http::status::unprocessable_entity, version, error);
            return json_response(http::status::ok, version, "{\"id\":" + json_quoted(start_id) + ",\"status\":\"running\"}");
        }
        const auto stop_id = engine_id_from_path(target, "stop");
        if (!stop_id.empty()) {
            std::string error;
            if (!engines.stop(stop_id, error)) return error_response(http::status::unprocessable_entity, version, error);
            return json_response(http::status::ok, version, "{\"id\":" + json_quoted(stop_id) + ",\"status\":\"stopped\"}");
        }
        if (target == "/v1/chat/completions") {
            if (request.body().empty()) return error_response(http::status::bad_request, version, "Request body must contain a chat completion JSON object.");
            const auto engine = engines.default_chat_engine();
            if (!engine) return error_response(http::status::service_unavailable, version, "No chat engine is configured. Set upstream_base_url in config/engines.json.");
            const auto proxied = proxy_chat(*engine, request.body());
            http::response<http::string_body> response{proxied.status, version};
            response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
            response.set(http::field::content_type, proxied.content_type);
            response.set("X-Content-Type-Options", "nosniff");
            response.set("Cache-Control", "no-store");
            response.body() = proxied.body;
            response.keep_alive(false);
            response.prepare_payload();
            return response;
        }
    }
    if (request.method() == http::verb::options) {
        return error_response(http::status::method_not_allowed, version, "CORS preflight is not supported; use the same-origin local dashboard.");
    }
    return error_response(http::status::not_found, version, "Route not found.");
}

void serve_client(tcp::socket socket, RuntimeConfig config, EngineManager& engines,
                  DownloadManager& downloads, std::filesystem::path web_dir) {
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(std::chrono::seconds(130));
    beast::flat_buffer buffer;
    try {
        http::request_parser<http::string_body> parser;
        parser.body_limit(4U * 1024U * 1024U);
        http::read(stream, buffer, parser);
        auto request = parser.release();
        const bool is_shutdown_request = request.method() == http::verb::post &&
            request.target() == "/v1/runtime/shutdown";
        auto response = handle_request(request, config, engines, downloads, web_dir);
        const bool shutdown_after_response = is_shutdown_request && response.result() == http::status::accepted;
        http::write(stream, response);
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
        // Only request shutdown after an authorized acknowledgement has been
        // written. Rejected cross-origin requests must never stop the runtime.
        if (shutdown_after_response) request_shutdown(0);
    } catch (const boost::system::system_error& ex) {
        if (ex.code() == http::error::body_limit) {
            auto response = error_response(http::status::payload_too_large, 11, "Request body exceeds the 4 MiB limit.");
            beast::error_code ignored;
            http::write(stream, response, ignored);
        }
        // Malformed or disconnected clients are closed without reflecting untrusted bytes.
    } catch (const std::exception& ex) {
        log_diagnostic("ERROR", "http.request_failed", ex.what());
        std::cerr << "Request handling error: " << ex.what() << '\n';
    }
}

} // namespace

void run_http_server(const RuntimeConfig& config, EngineManager& engines,
                     const std::filesystem::path& web_dir) {
    std::signal(SIGINT, request_shutdown);
    std::signal(SIGTERM, request_shutdown);
    shutdown_requested = 0;
    asio::io_context ioc{1};
    const auto address = asio::ip::make_address(config.host == "localhost" ? "127.0.0.1" : config.host);
    const tcp::endpoint endpoint{address, config.port};
    tcp::acceptor acceptor{ioc};
    acceptor.open(endpoint.protocol());
#ifdef _WIN32
    // Windows SO_REUSEADDR semantics permit overlapping binds and can let a
    // duplicate runtime appear to start successfully, then hang the singleton
    // smoke test. Keep the port exclusive on Windows. Unix retains reuse so a
    // clean restart is not blocked by recently closed loopback connections.
    acceptor.set_option(asio::socket_base::reuse_address(false));
#else
    acceptor.set_option(asio::socket_base::reuse_address(true));
#endif
    acceptor.bind(endpoint);
    acceptor.listen(asio::socket_base::max_listen_connections);
    acceptor.non_blocking(true);
    DownloadManager downloads(config, engines);
    std::cout << "Local AI Runtime " << LAR_VERSION << " listening on http://127.0.0.1:"
              << config.port << "\n"
              << "Configured engines: " << config.engines.size() << "\n"
              << "Press Ctrl+C to stop.\n" << std::flush;
    log_diagnostic("INFO", "runtime.started", "bind=loopback port=" + std::to_string(config.port));
    while (shutdown_requested == 0) {
        tcp::socket socket{ioc};
        boost::system::error_code ec;
        acceptor.accept(socket, ec);
        if (ec == asio::error::would_block || ec == asio::error::try_again) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (ec == asio::error::interrupted && shutdown_requested != 0) break;
        if (ec) throw boost::system::system_error(ec, "accept");
        std::thread(serve_client, std::move(socket), config, std::ref(engines), std::ref(downloads), web_dir).detach();
    }
    beast::error_code ignored;
    acceptor.close(ignored);
    log_diagnostic("INFO", "runtime.stopped", "shutdown=normal");
}

} // namespace lar
