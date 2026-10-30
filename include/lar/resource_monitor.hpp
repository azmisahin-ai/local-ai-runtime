#pragma once

#include "lar/process.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lar {

struct ResourceGpuDevice {
    int index{-1};
    std::optional<int> utilization_percent;
    std::optional<std::uint64_t> memory_used_bytes;
    std::optional<std::uint64_t> memory_total_bytes;
};

struct EngineResourceUsage {
    std::string engine_id;
    std::string state;
    std::optional<double> cpu_percent;
    std::optional<std::uint64_t> resident_memory_bytes;
};

struct ResourceSnapshot {
    std::string platform;
    std::uint64_t captured_at{0};
    unsigned logical_cpu_count{1};
    std::optional<double> cpu_utilization_percent;
    std::uint64_t total_memory_bytes{0};
    std::uint64_t available_memory_bytes{0};
    std::uint64_t used_memory_bytes{0};
    std::optional<double> memory_utilization_percent;
    std::string memory_pressure{"unknown"};
    std::string gpu_telemetry_status{"unavailable"};
    std::string gpu_telemetry_message;
    std::vector<ResourceGpuDevice> gpus;
    std::vector<EngineResourceUsage> engines;
};

// Samples local system and managed-engine resource use. Percentages are
// derived from consecutive samples; the first CPU sample is intentionally null.
// GPU telemetry is system-wide and is reported only when a supported NVIDIA
// query utility is available. No prompt, response, path, or process command line
// is collected or persisted.
ResourceSnapshot collect_resource_snapshot(const std::vector<EngineStatus>& engines);
std::string resource_snapshot_json(const ResourceSnapshot& snapshot);

} // namespace lar
