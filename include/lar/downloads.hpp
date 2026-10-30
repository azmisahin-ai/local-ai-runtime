#pragma once

#include "lar/config.hpp"
#include "lar/process.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace lar {

struct DownloadJobSnapshot {
    std::string id;
    std::string item_id;
    std::string kind;
    std::string state;
    std::string phase;
    std::string version;
    std::string installed_path;
    std::string error;
    std::string last_error;
    std::string error_code;
    std::uint64_t downloaded_bytes{0};
    std::uint64_t total_bytes{0};
    std::uint64_t created_at{0};
    std::uint64_t updated_at{0};
    std::uint64_t started_at{0};
    std::uint64_t finished_at{0};
    std::uint64_t attempt{1};
    bool cancel_requested{false};
    bool retryable{false};
};

// Manages downloads only from the built-in, reviewed catalog. Arbitrary URL
// downloads are deliberately not exposed by the local HTTP API.
class DownloadManager {
public:
    // Internal shared state is public only so C-style transfer callbacks can safely reference it.
    struct Job;
    struct Worker {
        std::string job_id;
        std::thread thread;
    };
    DownloadManager(RuntimeConfig config, EngineManager& engines);
    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;
    ~DownloadManager();

    std::string catalog_json() const;
    std::string runtime_profile_json() const;
    bool apply_runtime_profile(const std::string& engine_component_id,
                               const std::string& model_component_id,
                               unsigned int context_size, std::string& error);
    std::string jobs_json() const;
    std::optional<std::string> job_json(const std::string& id) const;
    bool retry(const std::string& job_id, std::string& error);
    bool start(const std::string& item_id, bool accept_license,
               std::string& job_id, std::string& error);
    bool start_update(const std::string& current_version, const std::string& tag_name,
                      const std::string& asset_name, const std::string& download_url,
                      const std::string& digest, std::uint64_t expected_size,
                      std::string& job_id, std::string& error);
    static std::string runtime_update_asset_name();
    static bool is_newer_release_tag(const std::string& candidate_tag, const std::string& current_version);
    bool cancel(const std::string& job_id, std::string& error);
    bool has_active_jobs() const;

    static std::filesystem::path user_data_directory();
    static bool is_safe_archive_entry_path(const std::string& entry_path);
    static std::string sha256_file(const std::filesystem::path& path);

private:
    void load_jobs();
    bool persist_job(const std::shared_ptr<Job>& job) const;
    void remove_persisted_job(const std::string& job_id) const;
    void run_job(const std::shared_ptr<Job>& job);
    void reap_finished_workers_locked();
    void download_model(const std::shared_ptr<Job>& job);
    void download_engine(const std::shared_ptr<Job>& job, const std::string& variant);
    void download_runtime_update(const std::shared_ptr<Job>& job);
    void configure_llama_engine(const std::filesystem::path& executable,
                                const std::filesystem::path& model,
                                const std::string& variant);

    RuntimeConfig config_;
    EngineManager& engines_;
    mutable std::mutex mutex_;
    mutable std::mutex persistence_mutex_;
    std::vector<std::shared_ptr<Job>> jobs_;
    std::vector<Worker> workers_;
    std::uint64_t next_job_number_{1};
};

} // namespace lar
