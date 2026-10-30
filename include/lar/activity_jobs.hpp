#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lar {

// Durable lifecycle records for runtime operations that are not component
// downloads: engine process start/stop and local inference requests.
// DownloadManager remains the owner of catalog installation/retry workflows;
// both managers write the same jobs-v1 record shape and share the jobs API.
class ActivityJobManager {
public:
    explicit ActivityJobManager(std::filesystem::path jobs_directory);
    ActivityJobManager(const ActivityJobManager&) = delete;
    ActivityJobManager& operator=(const ActivityJobManager&) = delete;

    // Creates a durable queued record, then transitions it to running.
    // Returns an empty id when persistence fails.
    std::string begin(const std::string& kind, const std::string& item_id,
                      const std::string& phase, bool cancellable = false);
    bool request_cancel(const std::string& id, std::string& error);
    bool cancel_requested(const std::string& id) const;
    void report_progress(const std::string& id, const std::string& phase,
                         std::uint64_t progress_bytes);
    void succeed(const std::string& id, const std::string& phase);
    void fail(const std::string& id, const std::string& phase,
              const std::string& error, const std::string& error_code = "operation_failed");
    void cancel(const std::string& id, const std::string& phase,
                const std::string& message, const std::string& error_code = "client_canceled");

    std::string jobs_json() const;
    std::optional<std::string> job_json(const std::string& id) const;

private:
    struct Job {
        std::string id;
        std::string item_id;
        std::string kind;
        std::string state{"queued"};
        std::string phase;
        std::string error;
        std::string error_code;
        std::uint64_t created_at{0};
        std::uint64_t updated_at{0};
        std::uint64_t started_at{0};
        std::uint64_t finished_at{0};
        std::uint64_t duration_ms{0};
        std::uint64_t progress_bytes{0};
        std::uint64_t last_persisted_progress_bytes{0};
        std::chrono::steady_clock::time_point last_progress_persisted_at{};
        bool cancellable{false};
        bool cancel_requested{false};
    };

    void load_jobs();
    bool persist_locked(const Job& job) const;
    static std::string serialize(const Job& job);
    static bool valid_kind(const std::string& kind);
    static bool valid_id(const std::string& id);
    static std::uint64_t unix_time_millis();

    std::filesystem::path jobs_directory_;
    mutable std::mutex mutex_;
    std::vector<Job> jobs_;
    std::uint64_t next_job_number_{1};
};

} // namespace lar
