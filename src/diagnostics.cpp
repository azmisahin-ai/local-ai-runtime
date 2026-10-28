#include "lar/diagnostics.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace lar {
namespace {
constexpr std::uintmax_t kMaxLogBytes = 2U * 1024U * 1024U;
constexpr std::size_t kMaxReportLines = 500;
std::mutex g_mutex;
std::filesystem::path g_root;
std::filesystem::path g_log_file;
std::string g_runtime_version{"unknown"};

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    });
    return value;
}

std::string replace_case_insensitive(std::string value, const std::string& needle,
                                     const std::string& replacement) {
    if (needle.empty()) return value;
    auto haystack_lower = lower_ascii(value);
    const auto needle_lower = lower_ascii(needle);
    std::size_t position = 0;
    while ((position = haystack_lower.find(needle_lower, position)) != std::string::npos) {
        value.replace(position, needle.size(), replacement);
        haystack_lower.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
    return value;
}

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string platform_name() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown OS";
#endif
}

std::string architecture_name() {
#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

std::string user_home() {
#ifdef _WIN32
    if (const char* value = std::getenv("USERPROFILE")) return value;
#else
    if (const char* value = std::getenv("HOME")) return value;
#endif
    return {};
}

std::string json_escape_path(std::string path) {
    std::string escaped;
    escaped.reserve(path.size() * 2);
    for (const char ch : path) {
        if (ch == '\\') escaped.push_back('\\');
        escaped.push_back(ch);
    }
    return escaped;
}

std::string sanitize_locked(std::string_view text) {
    std::string value(text);
    if (!g_root.empty()) {
        const auto native = path_to_utf8(g_root.lexically_normal());
        value = replace_case_insensitive(std::move(value), json_escape_path(native), "<LOCAL_DATA>");
        value = replace_case_insensitive(std::move(value), native, "<LOCAL_DATA>");
        const auto generic = g_root.generic_string();
        value = replace_case_insensitive(std::move(value), generic, "<LOCAL_DATA>");
    }
    const auto home = user_home();
    if (!home.empty()) {
        value = replace_case_insensitive(std::move(value), json_escape_path(home), "<USER_HOME>");
        value = replace_case_insensitive(std::move(value), home, "<USER_HOME>");
    }
    // Keep each log event to one safe, readable line.
    for (char& ch : value) {
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        else if (static_cast<unsigned char>(ch) < 0x20U) ch = '?';
    }
    return value;
}

void rotate_if_needed_locked() {
    std::error_code ec;
    const auto size = std::filesystem::file_size(g_log_file, ec);
    if (ec || size <= kMaxLogBytes) return;
    auto rotated = g_log_file;
    rotated += ".1";
    ec.clear();
    std::filesystem::remove(rotated, ec);
    ec.clear();
    std::filesystem::rename(g_log_file, rotated, ec);
}

std::vector<std::string> read_recent_lines_locked() {
    std::vector<std::string> lines;
    std::ifstream input(g_log_file, std::ios::binary);
    if (!input) return lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(std::move(line));
        if (lines.size() > kMaxReportLines) lines.erase(lines.begin());
    }
    return lines;
}
} // namespace

void initialize_diagnostic_log(const std::filesystem::path& user_data_root,
                               std::string_view runtime_version) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_root = user_data_root.lexically_normal();
    g_log_file = g_root / "logs" / "runtime.log";
    g_runtime_version = std::string(runtime_version);
    std::error_code ec;
    std::filesystem::create_directories(g_log_file.parent_path(), ec);
    if (ec) return;
    rotate_if_needed_locked();
    std::ofstream output(g_log_file, std::ios::binary | std::ios::app);
    if (output) output << utc_timestamp() << " INFO diagnostics.initialized version="
                       << sanitize_locked(g_runtime_version) << " storage=local-only\n";
}

std::filesystem::path runtime_logs_directory() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_log_file.empty()) return g_log_file.parent_path();
    return std::filesystem::current_path() / "logs";
}

void log_diagnostic(std::string_view level, std::string_view event,
                    std::string_view detail) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_log_file.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(g_log_file.parent_path(), ec);
        if (ec) return;
        rotate_if_needed_locked();
        std::ofstream output(g_log_file, std::ios::binary | std::ios::app);
        if (!output) return;
        output << utc_timestamp() << ' ' << sanitize_locked(level) << ' '
               << sanitize_locked(event) << ' ' << sanitize_locked(detail) << '\n';
    } catch (...) {
        // Diagnostics must never be able to break the runtime.
    }
}

std::string sanitize_diagnostic_text(std::string_view text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return sanitize_locked(text);
}

std::string build_diagnostic_report(std::string_view runtime_version,
                                    std::string_view download_jobs_json,
                                    std::string_view engine_statuses_json,
                                    std::string_view engine_logs_text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostringstream report;
    const auto effective_version = runtime_version.empty() ? std::string_view(g_runtime_version) : runtime_version;
    report << "Local AI Runtime diagnostic report\n"
           << "Generated UTC: " << utc_timestamp() << "\n"
           << "Runtime version: " << sanitize_locked(effective_version) << "\n"
           << "Platform: " << platform_name() << ' ' << architecture_name() << "\n"
           << "Data directory: <LOCAL_DATA>\n"
           << "Remote telemetry: disabled\n"
           << "Chat prompts and model responses are not included by the runtime event logger.\n"
           << (engine_logs_text.empty() ? "Engine process logs: not included.\n"
                                        : "Engine process logs: included by explicit user choice; upstream logs may contain prompts or local paths.\n")
           << "Review this file before sharing it; error messages and optional engine logs may contain details about your setup.\n\n"
           << "== Recent runtime log (up to " << kMaxReportLines << " lines) ==\n";
    const auto lines = read_recent_lines_locked();
    if (lines.empty()) report << "(No runtime log entries have been recorded yet.)\n";
    for (const auto& line : lines) report << sanitize_locked(line) << '\n';
    report << "\n== Current download jobs (JSON) ==\n"
           << sanitize_locked(download_jobs_json) << "\n"
           << "\n== Current engine status (JSON) ==\n"
           << sanitize_locked(engine_statuses_json) << "\n";
    if (!engine_logs_text.empty()) {
        report << "\n== Optional engine process logs (recent lines) ==\n"
               << sanitize_locked(engine_logs_text) << "\n";
    }
    return report.str();
}

} // namespace lar
