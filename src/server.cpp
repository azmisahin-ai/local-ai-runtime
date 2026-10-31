#include "lar/server.hpp"
#include "lar/downloads.hpp"
#include "lar/activity_jobs.hpp"
#include "lar/diagnostics.hpp"
#include "lar/hardware.hpp"
#include "lar/backend_evidence.hpp"
#include "lar/gpu_monitor.hpp"
#include "lar/resource_monitor.hpp"
#include "lar/engine_adapter.hpp"

#ifndef LAR_VERSION
#define LAR_VERSION "0.1.0-dev"
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
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
    std::string id;
#ifdef _MSC_VER
    // Use the Microsoft secure CRT variant to avoid C4996 while keeping the
    // returned environment value owned and released by this function.
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, "LOCAL_AI_RUNTIME_DATA_ROOT_ID") != 0 || !value) {
        std::free(value);
        return {};
    }
    id.assign(value);
    std::free(value);
#else
    const char* value = std::getenv("LOCAL_AI_RUNTIME_DATA_ROOT_ID");
    if (!value) return {};
    id.assign(value);
#endif
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
        << ",\"adapter_id\":" << json_quoted(engine.adapter_id.empty() ? default_engine_adapter_id(engine.config.id) : engine.adapter_id)
        << ",\"adapter_name\":" << json_quoted(engine.adapter_name)
        << ",\"adapter_contract_version\":" << engine_adapter_contract_version
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

std::string merge_jobs_json(const std::string& download_jobs, const std::string& operation_jobs) {
    constexpr std::string_view prefix = "{\"data\":[";
    const auto extract = [](const std::string& value) -> std::optional<std::string> {
        constexpr std::string_view start = "{\"data\":[";
        if (value.size() < start.size() + 2 || value.compare(0, start.size(), start) != 0 ||
            value.compare(value.size() - 2, 2, "]}") != 0) return std::nullopt;
        return value.substr(start.size(), value.size() - start.size() - 2);
    };
    const auto downloads = extract(download_jobs);
    const auto operations = extract(operation_jobs);
    if (!downloads || !operations) return download_jobs;
    std::string merged(prefix);
    merged += *downloads;
    if (!downloads->empty() && !operations->empty()) merged.push_back(',');
    merged += *operations;
    merged += "]}";
    return merged;
}

std::string activity_snapshot_json(const std::vector<EngineStatus>& engines,
                                   const std::string& jobs_json) {
    std::ostringstream out;
    std::size_t active_jobs = 0;
    std::size_t ready_engines = 0;
    out << "{\"schema_version\":1,\"snapshot\":true,\"data\":[";
    bool first = true;
    try {
        std::istringstream input(jobs_json);
        boost::property_tree::ptree jobs_root;
        boost::property_tree::read_json(input, jobs_root);
        if (const auto jobs = jobs_root.get_child_optional("data")) {
            for (const auto& entry : *jobs) {
                const auto& job = entry.second;
                const auto state = job.get<std::string>("state", "unknown");
                const auto kind = job.get<std::string>("kind", "component");
                const bool component_cancellable = state == "queued" || state == "downloading" ||
                    state == "verifying" || state == "extracting" || state == "configuring";
                const bool operation_cancellable = state == "running" &&
                    job.get<bool>("cancellable", false);
                const bool cancellable = component_cancellable || operation_cancellable;
                const bool active = cancellable || state == "running";
                if (active) ++active_jobs;
                const bool runtime_operation = kind == "engine-start" || kind == "engine-stop" || kind == "inference";
                const std::string item_type = runtime_operation ? "operation" : (kind == "update" ? "update" : "component");
                if (!first) out << ',';
                first = false;
                out << "{\"id\":" << json_quoted("job:" + job.get<std::string>("id", "unknown"))
                    << ",\"type\":" << json_quoted(item_type)
                    << ",\"kind\":" << json_quoted(kind)
                    << ",\"component_id\":" << json_quoted(job.get<std::string>("item_id", ""))
                    << ",\"state\":" << json_quoted(state)
                    << ",\"phase\":" << json_quoted(job.get<std::string>("phase", state))
                    << ",\"message\":" << json_quoted(job.get<std::string>("error", ""))
                    << ",\"version\":" << json_quoted(job.get<std::string>("version", ""))
                    << ",\"progress_percent\":" << job.get<unsigned int>("progress_percent", 0U)
                    << ",\"completed_bytes\":" << job.get<std::uint64_t>("downloaded_bytes", 0ULL)
                    << ",\"total_bytes\":" << job.get<std::uint64_t>("total_bytes", 0ULL)
                    << ",\"progress_bytes\":" << job.get<std::uint64_t>("progress_bytes", 0ULL)
                    << ",\"cancellable\":" << (cancellable ? "true" : "false") << '}';
            }
        }
    } catch (const std::exception&) {
        // DownloadManager owns the canonical job serializer. An invalid internal
        // snapshot must not make the health/runtime status APIs unavailable.
    }

    for (const auto& engine : engines) {
        const bool chat_capable = std::find(engine.config.capabilities.begin(), engine.config.capabilities.end(), "chat") != engine.config.capabilities.end();
        const bool model_missing = chat_capable && engine.config.model_path.empty();
        const bool ready = !model_missing && upstream_is_ready(engine);
        std::string state = "stopped";
        std::string phase = "stopped";
        std::string message = "Engine is stopped.";
        if (engine.config.executable_path.empty()) {
            state = "not-configured";
            phase = "configuration";
            message = "Engine executable is not configured.";
        } else if (model_missing) {
            state = "not-configured";
            phase = "model-configuration";
            message = "A model file is not configured for this chat engine.";
        } else if (engine.state == ProcessState::failed) {
            state = "failed";
            phase = "process";
            message = engine.last_error;
        } else if (engine.state == ProcessState::running && ready) {
            state = "ready";
            phase = "health-check";
            message = "The local model server is healthy and ready.";
            ++ready_engines;
        } else if (engine.state == ProcessState::running) {
            state = "loading";
            phase = "health-check";
            message = "The process is running; waiting for the local model server to become ready.";
        }
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << json_quoted("engine:" + engine.config.id)
            << ",\"type\":\"engine\",\"kind\":\"engine\""
            << ",\"component_id\":" << json_quoted(engine.config.id)
            << ",\"state\":" << json_quoted(state)
            << ",\"phase\":" << json_quoted(phase)
            << ",\"message\":" << json_quoted(message)
            << ",\"version\":\"\",\"progress_percent\":null"
            << ",\"completed_bytes\":0,\"total_bytes\":0,\"progress_bytes\":0,\"cancellable\":false}";
    }
    out << "],\"summary\":{\"active_jobs\":" << active_jobs
        << ",\"ready_engines\":" << ready_engines
        << ",\"engine_count\":" << engines.size() << "}}";
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
    std::string adapter_error;
    const auto adapter = create_engine_adapter(engine.config, adapter_error);
    if (!adapter) return false;
    unsigned int status_code = 0;
    if (!probe_status(port_text, std::string(adapter->readiness_path()), status_code)) return false;
    if (status_code == 200) return true;
    const auto fallback = adapter->readiness_fallback_path();
    // Adapters may define a protocol-specific fallback for older servers.
    if (status_code == 404 && !fallback.empty() &&
        probe_status(port_text, std::string(fallback), status_code)) return status_code == 200;
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
    engine.adapter_id = "llama.cpp";
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
    std::string adapter_error;
    const auto adapter = create_engine_adapter(engine.config, adapter_error);
    if (!adapter) {
        ProxyResult result;
        result.status = http::status::service_unavailable;
        result.body = "{\"error\":{\"message\":" + json_quoted("Engine adapter is unavailable: " + adapter_error) + "}}";
        return result;
    }
    return proxy_local_request(engine, http::verb::post, std::string(adapter->chat_completions_path()), body);
}

bool request_wants_chat_stream(const http::request<http::string_body>& request) {
    if (request.method() != http::verb::post || request.target() != "/v1/chat/completions") return false;
    try {
        std::istringstream input(request.body());
        boost::property_tree::ptree payload;
        boost::property_tree::read_json(input, payload);
        return payload.get<bool>("stream", false);
    } catch (...) {
        return false;
    }
}

void write_http_chunk(beast::tcp_stream& stream, const char* data, std::size_t size) {
    if (size == 0) return;
    std::ostringstream prefix;
    prefix << std::hex << size << "\r\n";
    asio::write(stream, asio::buffer(prefix.str()));
    asio::write(stream, asio::buffer(data, size));
    asio::write(stream, asio::buffer("\r\n", 2));
}

bool downstream_client_disconnected(tcp::socket& socket) {
    boost::system::error_code mode_error;
    const bool was_non_blocking = socket.non_blocking();
    socket.non_blocking(true, mode_error);
    if (mode_error) return false;
    std::array<char, 1> probe{};
    boost::system::error_code receive_error;
    const auto received = socket.receive(asio::buffer(probe), tcp::socket::message_peek, receive_error);
    boost::system::error_code restore_error;
    socket.non_blocking(was_non_blocking, restore_error);
    if (receive_error == asio::error::eof || receive_error == asio::error::connection_reset ||
        receive_error == asio::error::connection_aborted) return true;
    return !receive_error && received == 0;
}

void proxy_chat_stream(beast::tcp_stream& client_stream,
                       const http::request<http::string_body>& client_request,
                       const RuntimeConfig& config, EngineManager& engines,
                       ActivityJobManager& activity_jobs) {
    const auto version = client_request.version();
    const std::string host(client_request[http::field::host].data(), client_request[http::field::host].size());
    if (!is_loopback_host(host, config.port)) {
        auto response = error_response(http::status::forbidden, version, "Host header is not allowed.");
        http::write(client_stream, response);
        return;
    }
    const std::string origin(client_request[http::field::origin].data(), client_request[http::field::origin].size());
    if (!origin_is_same_loopback(origin, config.port)) {
        auto response = error_response(http::status::forbidden, version, "Cross-origin requests are disabled. Use the local dashboard origin.");
        http::write(client_stream, response);
        return;
    }
    if (client_request.body().empty()) {
        auto response = error_response(http::status::bad_request, version, "Request body must contain a chat completion JSON object.");
        http::write(client_stream, response);
        return;
    }
    const auto engine = engines.default_chat_engine();
    if (!engine) {
        auto response = error_response(http::status::service_unavailable, version, "No chat engine is configured. Set upstream_base_url in config/engines.json.");
        http::write(client_stream, response);
        return;
    }
    const auto operation_id = activity_jobs.begin("inference", engine->config.id, "Streaming local chat completion", true);
    if (operation_id.empty()) {
        auto response = error_response(http::status::internal_server_error, version, "Could not persist the streaming inference operation.");
        http::write(client_stream, response);
        return;
    }
    const auto fail_before_stream = [&](http::status status, const std::string& title, const std::string& detail) {
        activity_jobs.fail(operation_id, title, detail, "inference_failed");
        auto response = error_response(status, version, detail);
        http::write(client_stream, response);
    };
    if (engine->config.model_path.empty()) {
        fail_before_stream(http::status::service_unavailable, "Chat model is not selected",
            "No model is selected for Chat. Install a model, choose it in Models → Active runtime profile, and apply the profile.");
        return;
    }
    if (engine->state != ProcessState::running) {
        fail_before_stream(http::status::service_unavailable, "Chat engine is stopped",
            "Configured engine is not running. Start it from the dashboard first.");
        return;
    }
    if (!upstream_is_ready(*engine)) {
        fail_before_stream(http::status::service_unavailable, "Chat engine is not ready",
            "The selected engine is still loading or failed its readiness check. Wait until Activity reports Ready, then retry your message.");
        return;
    }
    const auto port_text = upstream_port(engine->config.upstream_base_url);
    if (port_text.empty()) {
        fail_before_stream(http::status::bad_gateway, "Chat engine upstream is invalid",
            "No valid local upstream_base_url is configured for this engine.");
        return;
    }
    bool downstream_headers_sent = false;
    try {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream upstream(ioc);
        upstream.expires_after(std::chrono::minutes(15));
        const auto endpoints = resolver.resolve("127.0.0.1", port_text);
        upstream.connect(endpoints);

        std::string adapter_error;
        const auto adapter = create_engine_adapter(engine->config, adapter_error);
        if (!adapter) {
            fail_before_stream(http::status::service_unavailable, "Chat adapter is unavailable", adapter_error);
            return;
        }
        http::request<http::string_body> upstream_request{http::verb::post, std::string(adapter->chat_completions_path()), 11};
        upstream_request.set(http::field::host, "127.0.0.1:" + port_text);
        upstream_request.set(http::field::user_agent, "local-ai-runtime/" LAR_VERSION);
        upstream_request.set(http::field::content_type, "application/json");
        upstream_request.keep_alive(false);
        upstream_request.body() = client_request.body();
        upstream_request.prepare_payload();
        http::write(upstream, upstream_request);

        beast::flat_buffer upstream_buffer;
        http::response_parser<http::buffer_body> parser;
        parser.body_limit(64U * 1024U * 1024U);
        // Wait for the upstream response headers asynchronously so explicit
        // Activity cancellation also works during a slow model prefill, before
        // the first SSE body bytes are available.
        upstream.expires_after(std::chrono::minutes(15));
        asio::steady_timer cancellation_poll(ioc);
        bool header_read_done = false;
        bool downstream_disconnected = false;
        boost::system::error_code header_error;
        std::function<void()> poll_cancellation;
        poll_cancellation = [&]() {
            cancellation_poll.expires_after(std::chrono::milliseconds(100));
            cancellation_poll.async_wait([&](const boost::system::error_code& timer_error) {
                if (timer_error || header_read_done) return;
                if (activity_jobs.cancel_requested(operation_id)) {
                    boost::system::error_code ignored;
                    upstream.socket().cancel(ignored);
                    return;
                }
                if (downstream_client_disconnected(client_stream.socket())) {
                    downstream_disconnected = true;
                    boost::system::error_code ignored;
                    upstream.socket().cancel(ignored);
                    return;
                }
                poll_cancellation();
            });
        };
        http::async_read_header(upstream, upstream_buffer, parser,
            [&](const boost::system::error_code& error, std::size_t) {
                header_error = error;
                header_read_done = true;
                // Timer::cancel() without an error_code parameter is
                // available across the Boost.Asio versions used by all CI
                // platforms (older Windows and newer macOS packages differ
                // in whether the error_code overload is exposed).
                cancellation_poll.cancel();
            });
        poll_cancellation();
        ioc.run();
        if (activity_jobs.cancel_requested(operation_id)) {
            throw std::runtime_error("The inference job was canceled by the user.");
        }
        if (downstream_disconnected || downstream_client_disconnected(client_stream.socket())) {
            throw std::runtime_error("The chat client canceled generation.");
        }
        if (header_error) throw boost::system::system_error(header_error, "read local model response header");

        http::response<http::empty_body> downstream_response{parser.get().result(), version};
        downstream_response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
        const auto upstream_content_type = parser.get()[http::field::content_type];
        if (!upstream_content_type.empty()) {
            downstream_response.set(http::field::content_type,
                std::string(upstream_content_type.data(), upstream_content_type.size()));
        } else {
            downstream_response.set(http::field::content_type, "text/event-stream; charset=utf-8");
        }
        downstream_response.set("X-Content-Type-Options", "nosniff");
        downstream_response.set("Cache-Control", "no-store");
        downstream_response.set("X-Accel-Buffering", "no");
        downstream_response.keep_alive(false);
        downstream_response.chunked(true);
        http::response_serializer<http::empty_body> downstream_serializer{downstream_response};
        http::write_header(client_stream, downstream_serializer);
        downstream_headers_sent = true;

        std::array<char, 16U * 1024U> body_buffer{};
        std::uint64_t streamed_response_bytes = 0;
        const auto stream_deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
        while (!parser.is_done()) {
            if (activity_jobs.cancel_requested(operation_id)) {
                throw std::runtime_error("The inference job was canceled by the user.");
            }
            if (downstream_client_disconnected(client_stream.socket())) {
                throw std::runtime_error("The chat client canceled generation.");
            }
            parser.get().body().data = body_buffer.data();
            parser.get().body().size = body_buffer.size();
            boost::system::error_code error;
            const auto remaining = stream_deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::steady_clock::duration::zero()) {
                throw std::runtime_error("The local model stream exceeded the 15-minute request limit.");
            }
            const auto poll_window = std::min(remaining, std::chrono::steady_clock::duration(std::chrono::milliseconds(300)));
            upstream.expires_after(std::chrono::duration_cast<std::chrono::steady_clock::duration>(poll_window));
            http::read_some(upstream, upstream_buffer, parser, error);
            if (activity_jobs.cancel_requested(operation_id)) {
                throw std::runtime_error("The inference job was canceled by the user.");
            }
            const auto bytes_read = body_buffer.size() - parser.get().body().size;
            if (bytes_read > 0) {
                streamed_response_bytes += static_cast<std::uint64_t>(bytes_read);
                activity_jobs.report_progress(operation_id, "Streaming model output", streamed_response_bytes);
                write_http_chunk(client_stream, body_buffer.data(), bytes_read);
                if (downstream_client_disconnected(client_stream.socket())) {
                    throw std::runtime_error("The chat client canceled generation.");
                }
            }
            if (error == beast::error::timeout) {
                if (downstream_client_disconnected(client_stream.socket())) {
                    throw std::runtime_error("The chat client canceled generation.");
                }
                continue;
            }
            if (error && error != http::error::need_buffer) {
                if (error == asio::error::eof && parser.is_done()) break;
                throw boost::system::system_error(error, "read streamed model response");
            }
            if (parser.is_done()) break;
        }
        asio::write(client_stream, asio::buffer("0\r\n\r\n", 5));
        activity_jobs.succeed(operation_id, "Streaming inference completed");
    } catch (const std::exception& error) {
        const std::string error_text = error.what();
        const bool user_requested_cancel = error_text.find("canceled by the user") != std::string::npos;
        const bool client_disconnected = error_text.find("canceled generation") != std::string::npos ||
            downstream_client_disconnected(client_stream.socket());
        if (user_requested_cancel) {
            activity_jobs.cancel(operation_id, "Inference canceled",
                "The user requested cancellation for this inference operation.", "user_canceled");
        } else if (client_disconnected) {
            activity_jobs.cancel(operation_id, "Inference canceled after client disconnect",
                "The chat client disconnected before generation completed.", "client_disconnected");
        } else {
            activity_jobs.fail(operation_id, "Streaming inference failed", error_text, "inference_stream_failed");
        }
        log_diagnostic("WARN", "chat.stream_failed", error.what());
        if (!downstream_headers_sent) {
            auto response = error_response(http::status::bad_gateway, version,
                std::string("Could not stream response from the local model server: ") + error.what());
            beast::error_code ignored;
            http::write(client_stream, response, ignored);
        }
        // If the browser canceled generation, its socket closes. A failed write
        // exits this loop and releases the upstream connection, stopping work
        // at the model server as soon as it observes the disconnect.
    }
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
                                                  ActivityJobManager& activity_jobs,
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
    if (request.method() == http::verb::get &&
        (target == "/locales/en.json" || target == "/locales/tr.json")) {
        const auto locale_file = target == "/locales/tr.json" ? "tr.json" : "en.json";
        const auto body = read_text_file(web_dir / "locales" / locale_file);
        if (body.empty()) return error_response(http::status::not_found, version, "Language resource not found.");
        http::response<http::string_body> response{http::status::ok, version};
        response.set(http::field::content_type, "application/json; charset=utf-8");
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
        const auto report = build_diagnostic_report(LAR_VERSION,
            merge_jobs_json(downloads.jobs_json(), activity_jobs.jobs_json()), engine_json.str(), engine_logs);
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
    if (request.method() == http::verb::get && target == "/v1/system/resources") {
        const auto snapshot = collect_resource_snapshot(engines.statuses());
        return json_response(http::status::ok, version, resource_snapshot_json(snapshot));
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
    if (request.method() == http::verb::get && target == "/v1/runtime/profile") {
        return json_response(http::status::ok, version, downloads.runtime_profile_json());
    }
    if (request.method() == http::verb::post && target == "/v1/runtime/profile") {
        if (downloads.has_active_jobs()) {
            return error_response(http::status::conflict, version, "Finish or cancel the active component installation before changing the runtime profile.");
        }
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            const auto engine_component_id = body.get<std::string>("engine_component_id", "");
            const auto model_component_id = body.get<std::string>("model_component_id", "");
            const auto context_size = body.get<unsigned int>("context_size", 2048U);
            std::string error;
            if (!downloads.apply_runtime_profile(engine_component_id, model_component_id, context_size, error)) {
                const auto status = error.find("Stop all running engines") != std::string::npos
                    ? http::status::conflict : http::status::unprocessable_entity;
                return error_response(status, version, error);
            }
            return json_response(http::status::ok, version,
                "{\"status\":\"applied\",\"profile\":" + downloads.runtime_profile_json() + "}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version, std::string("Invalid runtime profile JSON: ") + ex.what());
        } catch (const std::exception& ex) {
            return error_response(http::status::unprocessable_entity, version, ex.what());
        }
    }
    if (request.method() == http::verb::get && target == "/v1/jobs") {
        return json_response(http::status::ok, version,
            merge_jobs_json(downloads.jobs_json(), activity_jobs.jobs_json()));
    }
    if (request.method() == http::verb::post && target == "/v1/jobs") {
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            const auto item_id = body.get<std::string>("item_id", "");
            const bool accept_license = body.get<bool>("accept_license", false);
            if (item_id.empty()) return error_response(http::status::unprocessable_entity, version, "A catalog item_id is required.");
            std::string job_id;
            std::string error;
            if (!downloads.start(item_id, accept_license, job_id, error)) {
                const auto status = error.find("already being installed") != std::string::npos ||
                    error.find("active in job") != std::string::npos || error.find("running engines") != std::string::npos ||
                    error.find("queue is full") != std::string::npos || error.find("shutting down") != std::string::npos
                    ? http::status::conflict : http::status::unprocessable_entity;
                return error_response(status, version, error);
            }
            return json_response(http::status::accepted, version,
                "{\"job_id\":" + json_quoted(job_id) + ",\"item_id\":" + json_quoted(item_id) + ",\"status\":\"queued\"}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version, std::string("Invalid job request JSON: ") + ex.what());
        }
    }
    if (request.method() == http::verb::get && target == "/v1/downloads") {
        return json_response(http::status::ok, version, downloads.jobs_json());
    }
    if (request.method() == http::verb::get && target == "/v1/activity") {
        return json_response(http::status::ok, version,
            activity_snapshot_json(engines.statuses(),
                merge_jobs_json(downloads.jobs_json(), activity_jobs.jobs_json())));
    }
    constexpr std::string_view jobs_prefix = "/v1/jobs/";
    constexpr std::string_view downloads_prefix = "/v1/downloads/";
    const bool jobs_route = target.rfind(jobs_prefix, 0) == 0;
    const bool downloads_route = target.rfind(downloads_prefix, 0) == 0;
    if ((jobs_route || downloads_route) && target.size() > (jobs_route ? jobs_prefix.size() : downloads_prefix.size())) {
        const auto prefix = jobs_route ? jobs_prefix : downloads_prefix;
        const auto route_id = target.substr(prefix.size());
        if (jobs_route && request.method() == http::verb::post && route_id.size() > 6 && route_id.ends_with("/retry")) {
            const auto job_id = route_id.substr(0, route_id.size() - 6);
            if (job_id.find('/') != std::string::npos) return error_response(http::status::not_found, version, "Job route was not found.");
            std::string error;
            if (!downloads.retry(std::string(job_id), error)) {
                if (error == "Unknown job id.") {
                    if (activity_jobs.job_json(std::string(job_id))) {
                        return error_response(http::status::conflict, version,
                            "Runtime operations are not replayed. Start a new engine action or submit a new inference request.");
                    }
                    return error_response(http::status::not_found, version, error);
                }
                const auto status = error.find("active") != std::string::npos ||
                    error.find("running engines") != std::string::npos ||
                    error.find("queue is full") != std::string::npos ||
                    error.find("shutting down") != std::string::npos
                    ? http::status::conflict : http::status::unprocessable_entity;
                return error_response(status, version, error);
            }
            const auto job = downloads.job_json(std::string(job_id));
            return json_response(http::status::accepted, version,
                job.value_or("{\"id\":" + json_quoted(job_id) + ",\"status\":\"queued\"}"));
        }
        if (route_id.find('/') == std::string::npos && request.method() == http::verb::get) {
            auto job = downloads.job_json(std::string(route_id));
            if (!job && jobs_route) job = activity_jobs.job_json(std::string(route_id));
            if (!job) return error_response(http::status::not_found, version, "Job was not found.");
            return json_response(http::status::ok, version, *job);
        }
        if (route_id.find('/') == std::string::npos && request.method() == http::verb::delete_) {
            std::string error;
            if (!downloads.cancel(std::string(route_id), error)) {
                if (error == "Unknown download job id.") {
                    if (jobs_route && activity_jobs.job_json(std::string(route_id))) {
                        std::string operation_error;
                        if (!activity_jobs.request_cancel(std::string(route_id), operation_error)) {
                            return error_response(http::status::conflict, version, operation_error);
                        }
                        return json_response(http::status::accepted, version,
                            "{\"id\":" + json_quoted(route_id) + ",\"status\":\"cancel_requested\"}");
                    }
                    return error_response(http::status::not_found, version, error);
                }
                return error_response(http::status::conflict, version, error);
            }
            return json_response(http::status::accepted, version,
                "{\"id\":" + json_quoted(route_id) + ",\"status\":" + json_quoted(jobs_route ? "cancel_requested" : "canceling") + "}");
        }
        if (!jobs_route && route_id.find('/') == std::string::npos && request.method() == http::verb::post) {
            try {
                std::istringstream input(request.body());
                boost::property_tree::ptree body;
                boost::property_tree::read_json(input, body);
                const bool accept_license = body.get<bool>("accept_license", false);
                std::string job_id;
                std::string error;
                if (!downloads.start(std::string(route_id), accept_license, job_id, error)) {
                    const auto status = error.find("already being installed") != std::string::npos ||
                        error.find("running engines") != std::string::npos ||
                        error.find("queue is full") != std::string::npos ||
                        error.find("shutting down") != std::string::npos
                        ? http::status::conflict : http::status::unprocessable_entity;
                    return error_response(status, version, error);
                }
                return json_response(http::status::accepted, version,
                    "{\"job_id\":" + json_quoted(job_id) + ",\"item_id\":" + json_quoted(route_id) + ",\"status\":\"queued\"}");
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
            next_config.runtime_profile.engine_component_id.clear();
            next_config.runtime_profile.model_component_id.clear();
            next_config.runtime_profile.context_size = body.get<unsigned int>("context_size", 2048U);
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
            const auto configured = engines.configurations();
            const auto selected_engine = std::find_if(configured.begin(), configured.end(), [&](const EngineConfig& item) {
                return item.id == start_id;
            });
            if (selected_engine != configured.end() &&
                std::find(selected_engine->capabilities.begin(), selected_engine->capabilities.end(), "chat") != selected_engine->capabilities.end() &&
                selected_engine->model_path.empty()) {
                return error_response(http::status::conflict, version,
                    "No model is selected for this chat engine. Choose an installed model in Models → Active runtime profile before starting it.");
            }
            const auto operation_id = activity_jobs.begin("engine-start", start_id, "Starting engine process");
            if (operation_id.empty()) return error_response(http::status::internal_server_error, version, "Could not persist the engine start operation.");
            std::string error;
            if (!engines.start(start_id, error)) {
                activity_jobs.fail(operation_id, "Engine start failed", error, "engine_start_failed");
                return error_response(http::status::unprocessable_entity, version, error);
            }
            activity_jobs.succeed(operation_id, "Engine process started; health readiness is monitored separately");
            return json_response(http::status::ok, version, "{\"id\":" + json_quoted(start_id) + ",\"status\":\"running\",\"job_id\":" + json_quoted(operation_id) + "}");
        }
        const auto stop_id = engine_id_from_path(target, "stop");
        if (!stop_id.empty()) {
            const auto operation_id = activity_jobs.begin("engine-stop", stop_id, "Stopping engine process");
            if (operation_id.empty()) return error_response(http::status::internal_server_error, version, "Could not persist the engine stop operation.");
            std::string error;
            if (!engines.stop(stop_id, error)) {
                activity_jobs.fail(operation_id, "Engine stop failed", error, "engine_stop_failed");
                return error_response(http::status::unprocessable_entity, version, error);
            }
            activity_jobs.succeed(operation_id, "Engine process stopped");
            return json_response(http::status::ok, version, "{\"id\":" + json_quoted(stop_id) + ",\"status\":\"stopped\",\"job_id\":" + json_quoted(operation_id) + "}");
        }
        if (target == "/v1/chat/completions") {
            if (request.body().empty()) return error_response(http::status::bad_request, version, "Request body must contain a chat completion JSON object.");
            const auto engine = engines.default_chat_engine();
            if (!engine) return error_response(http::status::service_unavailable, version, "No chat engine is configured. Set upstream_base_url in config/engines.json.");
            const auto operation_id = activity_jobs.begin("inference", engine->config.id, "Running local chat completion");
            if (operation_id.empty()) return error_response(http::status::internal_server_error, version, "Could not persist the inference operation.");
            if (engine->config.model_path.empty()) {
                const std::string detail = "No model is selected for Chat. Install a model, choose it in Models → Active runtime profile, and apply the profile.";
                activity_jobs.fail(operation_id, "Chat model is not selected", detail, "inference_failed");
                return error_response(http::status::service_unavailable, version, detail);
            }
            if (!upstream_is_ready(*engine)) {
                const std::string detail = "The selected engine is not ready yet. Start it from Activity and wait until its status is Ready before sending a message.";
                activity_jobs.fail(operation_id, "Chat engine is not ready", detail, "inference_failed");
                return error_response(http::status::service_unavailable, version, detail);
            }
            const auto proxied = proxy_chat(*engine, request.body());
            activity_jobs.report_progress(operation_id, "Validating local inference response",
                static_cast<std::uint64_t>(proxied.body.size()));
            if (proxied.status == http::status::ok && has_nonempty_chat_choices(proxied.body)) {
                activity_jobs.succeed(operation_id, "Local inference completed");
            } else {
                activity_jobs.fail(operation_id, "Local inference failed",
                    "The local model server did not return a successful chat completion.", "inference_failed");
            }
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
                  DownloadManager& downloads, ActivityJobManager& activity_jobs,
                  std::filesystem::path web_dir) {
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(std::chrono::seconds(130));
    beast::flat_buffer buffer;
    try {
        http::request_parser<http::string_body> parser;
        parser.body_limit(4U * 1024U * 1024U);
        http::read(stream, buffer, parser);
        auto request = parser.release();
        if (request_wants_chat_stream(request)) {
            // Streaming completions use a chunked response so tokens are flushed
            // as the local model server produces them instead of buffering the
            // complete generation in memory.
            stream.expires_after(std::chrono::minutes(15));
            proxy_chat_stream(stream, request, config, engines, activity_jobs);
            beast::error_code ignored;
            stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
            return;
        }
        const bool is_shutdown_request = request.method() == http::verb::post &&
            request.target() == "/v1/runtime/shutdown";
        auto response = handle_request(request, config, engines, downloads, activity_jobs, web_dir);
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
    ActivityJobManager activity_jobs(DownloadManager::user_data_directory() / "jobs");
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
        std::thread(serve_client, std::move(socket), config, std::ref(engines),
            std::ref(downloads), std::ref(activity_jobs), web_dir).detach();
    }
    beast::error_code ignored;
    acceptor.close(ignored);
    log_diagnostic("INFO", "runtime.stopped", "shutdown=normal");
}

} // namespace lar
