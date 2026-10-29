#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lar {

// Evidence parsed from the current managed-engine log session. This reports
// backend initialization/offload messages only; it is not a live utilization
// measurement and must never be presented as proof of GPU activity during a task.
struct GpuExecutionEvidence {
    std::string state{"not-verified"};
    std::string summary{"GPU execution has not been verified from the current engine log."};
    bool engine_log_available{false};
    bool current_session_found{false};
    bool cuda_runtime_reported{false};
    bool offload_count_found{false};
    std::int64_t offloaded_layers{-1};
    std::int64_t total_layers{-1};
};

inline constexpr std::string_view engine_log_session_marker = "=== LAR_ENGINE_SESSION_BEGIN ===";

// Pure parser used by tests. Only data after the last session marker is
// considered, so logs from a previous CPU/CUDA run cannot leak into current
// status. Missing markers fail closed to "not-verified".
GpuExecutionEvidence parse_gpu_execution_evidence(std::string_view log_text,
                                                   bool engine_log_available = true);

// Reads the managed engine's own log by validated engine ID. The raw log is
// never returned by this API; only a small set of evidence fields is exposed.
GpuExecutionEvidence inspect_gpu_execution_evidence(std::string_view engine_id);

} // namespace lar
