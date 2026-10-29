#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lar {

struct GpuAdapterInfo {
    std::string name;
    std::string vendor;
    std::string vendor_id;
    std::string device_id;
    std::uint64_t dedicated_memory_bytes{0};
};

struct HardwareInfo {
    std::string platform;
    std::string architecture;
    unsigned logical_cpu_count{1};
    std::uint64_t total_memory_bytes{0};
    std::uint64_t available_memory_bytes{0};
    std::uint64_t available_disk_bytes{0};
    // "complete" means the native adapter list was queried. "partial" means
    // the platform probe could not enumerate all GPU information. "unsupported"
    // means this build does not yet implement native GPU enumeration here.
    std::string gpu_probe_status{"unsupported"};
    std::string gpu_probe_message;
    std::vector<GpuAdapterInfo> gpus;
};

// Enumerates only local, non-sensitive device capabilities. The caller decides
// how to present the result; this function never sends data over the network.
HardwareInfo detect_hardware(const std::filesystem::path& storage_path);

} // namespace lar
