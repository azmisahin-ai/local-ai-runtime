#include "lar/activity_jobs.hpp"
#include "lar/diagnostics.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
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
namespace {
namespace pt = boost::property_tree;

std::string quote_json(std::string_view value) {
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

bool terminal_state(const std::string& state) {
    return state == "succeeded" || state == "failed" || state == "canceled" || state == "interrupted";
}

} // namespace

ActivityJobManager::ActivityJobManager(std::filesystem::path jobs_directory)
    : jobs_directory_(std::move(jobs_directory)) {
    load_jobs();
}

std::uint64_t ActivityJobManager::unix_time_millis() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool ActivityJobManager::valid_kind(const std::string& kind) {
    return kind == "engine-start" || kind == "engine-stop" || kind == "inference";
}

bool ActivityJobManager::valid_id(const std::string& id) {
    return !id.empty() && id.size() <= 96 &&
        std::all_of(id.begin(), id.end(), [](unsigned char ch) {
            return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
        });
}

std::string ActivityJobManager::serialize(const Job& job) {
    const auto progress = job.state == "succeeded" ? 100 : 0;
    std::ostringstream out;
    out << "{\"schema_version\":1"
        << ",\"id\":" << quote_json(job.id)
        << ",\"item_id\":" << quote_json(job.item_id)
        << ",\"kind\":" << quote_json(job.kind)
        << ",\"state\":" << quote_json(job.state)
        << ",\"phase\":" << quote_json(job.phase)
        << ",\"version\":\"\""
        << ",\"downloaded_bytes\":0,\"total_bytes\":0"
        << ",\"progress_percent\":" << progress
        << ",\"installed_path\":\"\""
        << ",\"error\":" << quote_json(job.error)
        << ",\"last_error\":\"\""
        << ",\"error_code\":" << quote_json(job.error_code)
        << ",\"created_at\":" << job.created_at
        << ",\"updated_at\":" << job.updated_at
        << ",\"started_at\":" << job.started_at
        << ",\"finished_at\":" << job.finished_at
        << ",\"attempt\":1"
        << ",\"cancel_requested\":" << (job.cancel_requested ? "true" : "false")
        << ",\"retryable\":false"
        << ",\"cancellable\":" << (job.cancellable ? "true" : "false")
        << ",\"duration_ms\":" << job.duration_ms
        << ",\"progress_bytes\":" << job.progress_bytes
        << "}";
    return out.str();
}

bool ActivityJobManager::persist_locked(const Job& job) const {
    if (!valid_id(job.id)) return false;
    std::error_code fs_error;
    std::filesystem::create_directories(jobs_directory_, fs_error);
    if (fs_error) {
        log_diagnostic("WARN", "activity_jobs.persistence_directory_failed",
            "Could not create the persistent job history directory: " + fs_error.message());
        return false;
    }

    const auto destination = jobs_directory_ / (job.id + ".json");
    auto temporary = destination;
    temporary += ".tmp";
    const auto value = serialize(job);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        output.put('\n');
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, fs_error);
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto code = GetLastError();
        std::filesystem::remove(temporary, fs_error);
        log_diagnostic("WARN", "activity_jobs.persistence_replace_failed",
            "Could not atomically replace an operation job record; Windows error=" + std::to_string(code));
        return false;
    }
#else
    std::filesystem::rename(temporary, destination, fs_error);
    if (fs_error) {
        std::filesystem::remove(temporary, fs_error);
        log_diagnostic("WARN", "activity_jobs.persistence_replace_failed",
            "Could not atomically replace an operation job record.");
        return false;
    }
#endif
    return true;
}

void ActivityJobManager::load_jobs() {
    std::error_code fs_error;
    if (!std::filesystem::exists(jobs_directory_, fs_error)) return;
    if (fs_error || !std::filesystem::is_directory(jobs_directory_, fs_error)) return;

    for (std::filesystem::directory_iterator it(jobs_directory_, fs_error), end;
         !fs_error && it != end; it.increment(fs_error)) {
        if (!it->is_regular_file(fs_error) || fs_error || it->path().extension() != ".json") {
            fs_error.clear();
            continue;
        }
        const auto filename = it->path().filename().string();
        if (filename.rfind("operation-", 0) != 0) continue;
        try {
            std::ifstream input(it->path(), std::ios::binary);
            if (!input) continue;
            pt::ptree record;
            pt::read_json(input, record);
            Job job;
            job.id = record.get<std::string>("id", "");
            if (!valid_id(job.id) || filename != job.id + ".json") continue;
            if (job.id.rfind("operation-", 0) != 0) continue;
            job.item_id = record.get<std::string>("item_id", "");
            job.kind = record.get<std::string>("kind", "");
            if (job.item_id.empty() || job.item_id.size() > 256 || !valid_kind(job.kind)) continue;
            job.state = record.get<std::string>("state", "interrupted");
            if (job.state != "queued" && job.state != "running" && !terminal_state(job.state)) continue;
            job.phase = record.get<std::string>("phase", "Restored operation history");
            job.error = record.get<std::string>("error", "");
            job.error_code = record.get<std::string>("error_code", "");
            job.created_at = record.get<std::uint64_t>("created_at", 0);
            job.updated_at = record.get<std::uint64_t>("updated_at", job.created_at);
            job.started_at = record.get<std::uint64_t>("started_at", 0);
            job.finished_at = record.get<std::uint64_t>("finished_at", 0);
            job.duration_ms = record.get<std::uint64_t>("duration_ms", 0);
            job.progress_bytes = record.get<std::uint64_t>("progress_bytes", 0);
            job.last_persisted_progress_bytes = job.progress_bytes;
            job.last_progress_persisted_at = std::chrono::steady_clock::now();
            job.cancellable = record.get<bool>("cancellable", false);
            job.cancel_requested = record.get<bool>("cancel_requested", false);
            if (job.created_at == 0) job.created_at = unix_time_millis();
            if (job.updated_at == 0) job.updated_at = job.created_at;
            if (!terminal_state(job.state)) {
                job.state = "interrupted";
                job.phase = "Interrupted when Local AI Runtime was not running";
                job.error = "The runtime stopped before this operation finished. It was not replayed automatically.";
                job.error_code = "process_interrupted";
                job.cancellable = false;
                job.cancel_requested = false;
                job.finished_at = unix_time_millis();
                job.updated_at = job.finished_at;
                if (job.started_at > 0 && job.finished_at >= job.started_at)
                    job.duration_ms = job.finished_at - job.started_at;
            }
            if (job.id.rfind("operation-", 0) == 0) {
                try {
                    const auto number = std::stoull(job.id.substr(10));
                    next_job_number_ = std::max(next_job_number_, static_cast<std::uint64_t>(number + 1ULL));
                } catch (...) {}
            }
            jobs_.push_back(std::move(job));
        } catch (const std::exception& ex) {
            log_diagnostic("WARN", "activity_jobs.record_skipped",
                "An operation history record could not be read: " + std::string(ex.what()));
        }
    }
    std::sort(jobs_.begin(), jobs_.end(), [](const Job& left, const Job& right) {
        return left.created_at < right.created_at;
    });
    if (jobs_.size() > 64) {
        const auto remove_count = jobs_.size() - 64;
        for (std::size_t i = 0; i < remove_count; ++i) {
            std::error_code ignored;
            std::filesystem::remove(jobs_directory_ / (jobs_[i].id + ".json"), ignored);
        }
        jobs_.erase(jobs_.begin(), jobs_.begin() + static_cast<std::ptrdiff_t>(remove_count));
    }
    for (const auto& job : jobs_) {
        if (job.state == "interrupted") (void)persist_locked(job);
    }
}

std::string ActivityJobManager::begin(const std::string& kind, const std::string& item_id,
                                      const std::string& phase, bool cancellable) {
    if (!valid_kind(kind) || item_id.empty() || item_id.size() > 256) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    Job job;
    job.id = "operation-" + std::to_string(next_job_number_++);
    job.item_id = item_id;
    job.kind = kind;
    job.state = "queued";
    job.phase = phase.empty() ? "Queued" : phase;
    job.cancellable = cancellable && kind == "inference";
    job.created_at = unix_time_millis();
    job.updated_at = job.created_at;
    if (!persist_locked(job)) return {};

    job.state = "running";
    job.phase = phase.empty() ? "Running" : phase;
    job.started_at = unix_time_millis();
    job.updated_at = job.started_at;
    if (!persist_locked(job)) {
        job.state = "failed";
        job.phase = "Could not persist operation start";
        job.error = "The operation could not be recorded in local job history.";
        job.error_code = "persistence_failed";
        job.finished_at = unix_time_millis();
        job.updated_at = job.finished_at;
        (void)persist_locked(job);
        return {};
    }
    jobs_.push_back(job);
    if (jobs_.size() > 64) {
        const auto remove_count = jobs_.size() - 64;
        for (std::size_t i = 0; i < remove_count; ++i) {
            std::error_code ignored;
            std::filesystem::remove(jobs_directory_ / (jobs_[i].id + ".json"), ignored);
        }
        jobs_.erase(jobs_.begin(), jobs_.begin() + static_cast<std::ptrdiff_t>(remove_count));
    }
    return job.id;
}

bool ActivityJobManager::request_cancel(const std::string& id, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    if (found == jobs_.end()) {
        error = "Unknown runtime operation id.";
        return false;
    }
    if (found->state != "running" || !found->cancellable) {
        error = "This runtime operation is not cancellable.";
        return false;
    }
    found->cancel_requested = true;
    found->updated_at = unix_time_millis();
    if (!persist_locked(*found)) {
        error = "Could not persist the cancellation request.";
        return false;
    }
    error.clear();
    return true;
}

bool ActivityJobManager::cancel_requested(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    return found != jobs_.end() && found->state == "running" && found->cancel_requested;
}

void ActivityJobManager::report_progress(const std::string& id, const std::string& phase,
                                         std::uint64_t progress_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    if (found == jobs_.end() || found->state != "running") return;
    found->progress_bytes = std::max(found->progress_bytes, progress_bytes);
    if (!phase.empty()) found->phase = phase;
    const auto now = std::chrono::steady_clock::now();
    const bool enough_bytes = found->progress_bytes - found->last_persisted_progress_bytes >= 32U * 1024U;
    const bool enough_time = found->last_progress_persisted_at.time_since_epoch().count() == 0 ||
        now - found->last_progress_persisted_at >= std::chrono::milliseconds(500);
    if (!enough_bytes && !enough_time) return;
    found->updated_at = unix_time_millis();
    if (persist_locked(*found)) {
        found->last_persisted_progress_bytes = found->progress_bytes;
        found->last_progress_persisted_at = now;
    }
}

void ActivityJobManager::succeed(const std::string& id, const std::string& phase) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    if (found == jobs_.end() || terminal_state(found->state)) return;
    if (found->cancel_requested) {
        found->state = "canceled";
        found->phase = "Cancellation accepted before operation completion";
        found->error = "The operation was canceled at the user's request.";
        found->error_code = "user_canceled";
        found->cancellable = false;
    } else {
        found->state = "succeeded";
        found->phase = phase.empty() ? "Completed" : phase;
        found->error.clear();
        found->error_code.clear();
        found->cancellable = false;
    }
    found->cancel_requested = found->state == "canceled";
    found->finished_at = unix_time_millis();
    found->updated_at = found->finished_at;
    found->duration_ms = found->started_at > 0 && found->finished_at >= found->started_at
        ? found->finished_at - found->started_at : 0;
    (void)persist_locked(*found);
}

void ActivityJobManager::fail(const std::string& id, const std::string& phase,
                              const std::string& error, const std::string& error_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    if (found == jobs_.end() || terminal_state(found->state)) return;
    found->state = "failed";
    found->phase = phase.empty() ? "Failed" : phase;
    found->error = error;
    found->error_code = error_code;
    found->cancellable = false;
    found->finished_at = unix_time_millis();
    found->updated_at = found->finished_at;
    found->duration_ms = found->started_at > 0 && found->finished_at >= found->started_at
        ? found->finished_at - found->started_at : 0;
    (void)persist_locked(*found);
}

void ActivityJobManager::cancel(const std::string& id, const std::string& phase,
                                const std::string& message, const std::string& error_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    if (found == jobs_.end() || terminal_state(found->state)) return;
    found->state = "canceled";
    found->phase = phase.empty() ? "Canceled" : phase;
    found->error = message;
    found->error_code = error_code;
    found->cancellable = false;
    found->cancel_requested = true;
    found->finished_at = unix_time_millis();
    found->updated_at = found->finished_at;
    found->duration_ms = found->started_at > 0 && found->finished_at >= found->started_at
        ? found->finished_at - found->started_at : 0;
    (void)persist_locked(*found);
}

std::string ActivityJobManager::jobs_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out << "{\"data\":[";
    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        if (i) out << ',';
        out << serialize(jobs_[i]);
    }
    out << "]}";
    return out.str();
}

std::optional<std::string> ActivityJobManager::job_json(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    if (found == jobs_.end()) return std::nullopt;
    return serialize(*found);
}

} // namespace lar
