#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace lar {

// Diagnostics are local-only. The runtime never transmits these records.
void initialize_diagnostic_log(const std::filesystem::path& user_data_root,
                               std::string_view runtime_version);
void log_diagnostic(std::string_view level, std::string_view event,
                    std::string_view detail = {}) noexcept;
std::filesystem::path runtime_logs_directory();
std::string build_diagnostic_report(std::string_view runtime_version,
                                    std::string_view download_jobs_json,
                                    std::string_view engine_statuses_json,
                                    std::string_view engine_logs_text = {});
std::string sanitize_diagnostic_text(std::string_view text);

} // namespace lar
