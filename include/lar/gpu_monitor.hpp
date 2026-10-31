#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lar {

struct NvidiaGpuCapability {
    int index{-1};
    std::string name;
    std::string driver_version;
    std::string compute_capability;
    int memory_total_mib{-1};
    int memory_free_mib{-1};
    int memory_used_mib{-1};
};

struct NvidiaGpuCapabilitySnapshot {
    std::string status{"unavailable"}; // available, partial, unavailable
    std::string message{"NVIDIA driver details were not available from nvidia-smi."};
    std::vector<NvidiaGpuCapability> devices;
};

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

// Parses the fixed identity/driver/VRAM CSV shape emitted by nvidia-smi.
// Memory values are MiB. Missing or unsupported values remain -1.
bool parse_nvidia_smi_capabilities(std::string_view output,
                                   std::vector<NvidiaGpuCapability>& devices);

// Parses a separate, best-effort index/compute_cap query. Keeping it separate
// prevents older drivers that do not expose compute_cap from hiding other data.
bool parse_nvidia_smi_compute_capabilities(
    std::string_view output, std::vector<std::pair<int, std::string>>& capabilities);

// Reads NVIDIA identity, driver, compute capability and VRAM details. The
// compute-capability query is independent and may be unavailable on older drivers.
NvidiaGpuCapabilitySnapshot read_nvidia_gpu_capabilities();

// Reads one current NVIDIA system-wide sample when nvidia-smi is available.
// Raw command output is discarded; only parsed numeric metrics are returned.
bool read_nvidia_gpu_snapshot(std::vector<NvidiaGpuSample>& samples);

// Executes a caller-supplied local task while sampling NVIDIA system-wide
// utilization/memory. The process is launched without a shell, has a timeout,
// and never returns raw command output. The task's text/result is not recorded.
NvidiaGpuObservation observe_nvidia_gpu_during(const std::function<bool()>& task,
                                                int sample_interval_ms = 300);

} // namespace lar
