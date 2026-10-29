#include "lar/backend_evidence.hpp"

#include "lar/config.hpp"
#include "lar/diagnostics.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <utility>
#include <system_error>

namespace lar {
namespace {
constexpr std::uintmax_t max_log_bytes_to_inspect = 8U * 1024U * 1024U;
constexpr std::string_view session_marker_line = engine_log_session_marker;

std::string lower_ascii(std::string_view value) {
    std::string output(value);
    std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch) {
        return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    });
    return output;
}

bool reports_cuda_initialization(std::string_view text) {
    const auto lower = lower_ascii(text);
    return lower.find("ggml_cuda_init") != std::string::npos ||
           lower.find("cuda devices") != std::string::npos ||
           lower.find("cuda0") != std::string::npos ||
           lower.find("cuda backend") != std::string::npos ||
           lower.find("using cuda") != std::string::npos;
}

GpuExecutionEvidence unverified(std::string summary, bool available, bool session_found = false) {
    GpuExecutionEvidence result;
    result.summary = std::move(summary);
    result.engine_log_available = available;
    result.current_session_found = session_found;
    return result;
}
} // namespace

GpuExecutionEvidence parse_gpu_execution_evidence(std::string_view log_text,
                                                   bool engine_log_available) {
    if (!engine_log_available) {
        return unverified("No engine process log is available yet.", false);
    }

    const auto marker = log_text.rfind(session_marker_line);
    if (marker == std::string_view::npos) {
        return unverified("The engine log has no current-session marker; previous entries are ignored to avoid stale GPU evidence.", true);
    }
    auto current_session = log_text.substr(marker + session_marker_line.size());
    const bool cuda_reported = reports_cuda_initialization(current_session);

    // llama.cpp currently writes messages such as "offloaded 28/33 layers to GPU".
    // Parse only explicit counts and only from this process session.
    static const std::regex offload_pattern(
        R"(offloaded\s+([0-9]+)\s*/\s*([0-9]+)\s+layers?\s+to\s+gpu)",
        std::regex_constants::icase);
    const std::string session_text(current_session);
    std::int64_t offloaded = -1;
    std::int64_t total = -1;
    for (auto it = std::sregex_iterator(session_text.begin(), session_text.end(), offload_pattern);
         it != std::sregex_iterator(); ++it) {
        try {
            offloaded = std::stoll((*it)[1].str());
            total = std::stoll((*it)[2].str());
        } catch (...) {
            offloaded = -1;
            total = -1;
        }
    }

    GpuExecutionEvidence result;
    result.engine_log_available = true;
    result.current_session_found = true;
    result.cuda_runtime_reported = cuda_reported;
    result.offload_count_found = offloaded >= 0 && total >= 0;
    result.offloaded_layers = offloaded;
    result.total_layers = total;

    if (result.offload_count_found && offloaded > 0) {
        result.state = "offload-reported";
        result.summary = "The current engine log reports " + std::to_string(offloaded) + "/" +
            std::to_string(total) + " layers offloaded to GPU. This is startup-log evidence only; live GPU utilization during a task has not been measured.";
    } else if (result.offload_count_found) {
        result.state = "no-layers-offloaded";
        result.summary = "The current engine log reports 0/" + std::to_string(total) +
            " layers offloaded to GPU. GPU inference is not verified; check the selected package, driver, model, and available VRAM.";
    } else if (cuda_reported) {
        result.state = "cuda-runtime-reported";
        result.summary = "The current engine log reports CUDA initialization, but no offloaded-layer count was found. GPU inference is not verified.";
    } else {
        result.state = "not-verified";
        result.summary = "No CUDA initialization or GPU offload evidence was found in the current engine session log.";
    }
    return result;
}

GpuExecutionEvidence inspect_gpu_execution_evidence(std::string_view engine_id) {
    if (!is_valid_engine_id(std::string(engine_id))) {
        return unverified("GPU evidence is unavailable because the engine identifier is invalid.", false);
    }
    const auto path = runtime_logs_directory() / (std::string(engine_id) + ".log");
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return unverified("No engine process log is available yet.", false);
    ec.clear();
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) return unverified("The engine process log timestamp could not be read.", false);

    struct CacheEntry {
        std::uintmax_t size{0};
        std::filesystem::file_time_type modified{};
        GpuExecutionEvidence evidence;
    };
    static std::mutex cache_mutex;
    static std::map<std::string, CacheEntry> cache;
    const std::string key(engine_id);
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        const auto found = cache.find(key);
        if (found != cache.end() && found->second.size == size && found->second.modified == modified) {
            return found->second.evidence;
        }
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) return unverified("The engine process log could not be opened.", false);
    if (size > max_log_bytes_to_inspect) {
        input.seekg(static_cast<std::streamoff>(size - max_log_bytes_to_inspect), std::ios::beg);
        std::string discard_partial_line;
        std::getline(input, discard_partial_line);
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto evidence = parse_gpu_execution_evidence(text, true);
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[key] = CacheEntry{size, modified, evidence};
    }
    return evidence;
}

} // namespace lar
