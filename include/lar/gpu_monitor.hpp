#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lar {

struct NvidiaGpuSample {
    int index{-1};
    int utilization_percent{-1};
    int memory_used_mib{-1};
    int memory_total_mib{-1};
};

struct NvidiaGpuObservationDevice {
    int index{-1};
    int samples_during_request{0};
    int peak_utilization_percent{-1};
    int memory_before_mib{-1};
    int memory_peak_mib{-1};
    int memory_after_mib{-1};
    int memory_total_mib{-1};
};

struct NvidiaGpuObservation {
    bool task_succeeded{false};
    bool telemetry_available{false};
    std::string status{"unavailable"};
    std::string message{"NVIDIA telemetry was not available; GPU activity was not measured."};
    int elapsed_ms{0};
    int samples_during_request{0};
    std::vector<NvidiaGpuObservationDevice> devices;
};

// Parses the fixed CSV shape emitted by nvidia-smi. "N/A" metric values are
// represented as -1; malformed output is rejected rather than guessed.
bool parse_nvidia_smi_snapshot(std::string_view output, std::vector<NvidiaGpuSample>& samples);

// Executes a caller-supplied local task while sampling NVIDIA system-wide
// utilization/memory. The process is launched without a shell, has a timeout,
// and never returns raw command output. The task's text/result is not recorded.
NvidiaGpuObservation observe_nvidia_gpu_during(const std::function<bool()>& task,
                                                int sample_interval_ms = 300);

} // namespace lar
