#include "lar/hardware.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <thread>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace lar {
namespace {

std::string architecture_name() {
#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

std::string vendor_name(std::uint32_t id) {
    switch (id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x10005: return "Apple";
        default: return "Other";
    }
}

std::string hex_id(std::uint32_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(6, '0');
    result[0] = '0';
    result[1] = 'x';
    for (int i = 5; i >= 2; --i) {
        result[static_cast<std::size_t>(i)] = digits[value & 0x0FU];
        value >>= 4U;
    }
    while (result.size() > 4 && result[2] == '0') result.erase(2, 1);
    return result;
}

#ifdef _WIN32
std::string wide_to_utf8(const wchar_t* value) {
    if (!value || *value == L'\0') return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        std::string fallback;
        for (const wchar_t ch : std::wstring_view(value)) {
            if (ch == L'\0') break;
            fallback.push_back(ch >= 0x20 && ch <= 0x7e ? static_cast<char>(ch) : '?');
        }
        return fallback;
    }
    std::string output(static_cast<std::size_t>(needed), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                             output.data(), needed, nullptr, nullptr);
    if (written <= 0) return {};
    output.resize(static_cast<std::size_t>(written - 1));
    return output;
}
#endif

#ifdef __linux__
std::string read_one_line(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string result;
    if (input) std::getline(input, result);
    return result;
}

std::uint64_t read_hex_file(const std::filesystem::path& path) {
    const auto raw = read_one_line(path);
    if (raw.empty()) return 0;
    std::string value = raw;
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) value.erase(0, 2);
    try {
        std::size_t parsed = 0;
        const auto number = std::stoull(value, &parsed, 16);
        return parsed == value.size() ? number : 0;
    } catch (...) { return 0; }
}

std::uint64_t read_decimal_file(const std::filesystem::path& path) {
    try {
        const auto raw = read_one_line(path);
        if (raw.empty()) return 0;
        return std::stoull(raw);
    } catch (...) { return 0; }
}
#endif

} // namespace

HardwareInfo detect_hardware(const std::filesystem::path& storage_path) {
    HardwareInfo result;
#ifdef _WIN32
    result.platform = "Windows";
#elif defined(__APPLE__)
    result.platform = "macOS";
#elif defined(__linux__)
    result.platform = "Linux";
#else
    result.platform = "Unknown";
#endif
    result.architecture = architecture_name();
    result.logical_cpu_count = std::max(1U, std::thread::hardware_concurrency());

#ifdef _WIN32
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result.total_memory_bytes = memory.ullTotalPhys;
        result.available_memory_bytes = memory.ullAvailPhys;
    }
    IDXGIFactory1* factory = nullptr;
    const HRESULT factory_status = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(factory_status) && factory) {
        result.gpu_probe_status = "complete";
        for (UINT index = 0; ; ++index) {
            IDXGIAdapter1* adapter = nullptr;
            const HRESULT status = factory->EnumAdapters1(index, &adapter);
            if (status == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(status) || !adapter) {
                result.gpu_probe_status = "partial";
                result.gpu_probe_message = "Windows could not enumerate every graphics adapter.";
                break;
            }
            DXGI_ADAPTER_DESC1 description{};
            const HRESULT description_status = adapter->GetDesc1(&description);
            if (SUCCEEDED(description_status)) {
                if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                    GpuAdapterInfo gpu;
                    gpu.name = wide_to_utf8(description.Description);
                    gpu.vendor = vendor_name(description.VendorId);
                    gpu.vendor_id = hex_id(description.VendorId);
                    gpu.device_id = hex_id(description.DeviceId);
                    gpu.dedicated_memory_bytes = static_cast<std::uint64_t>(description.DedicatedVideoMemory);
                    result.gpus.push_back(std::move(gpu));
                }
            } else {
                result.gpu_probe_status = "partial";
                result.gpu_probe_message = "Windows enumerated an adapter but could not read its details.";
            }
            adapter->Release();
        }
        factory->Release();
    } else {
        result.gpu_probe_status = "partial";
        result.gpu_probe_message = "Windows graphics adapter enumeration was unavailable; a GPU may still be present.";
    }
#elif defined(__linux__)
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long page_size = ::sysconf(_SC_PAGESIZE);
    const long available_pages = ::sysconf(_SC_AVPHYS_PAGES);
    if (pages > 0 && page_size > 0) result.total_memory_bytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
    if (available_pages > 0 && page_size > 0) result.available_memory_bytes = static_cast<std::uint64_t>(available_pages) * static_cast<std::uint64_t>(page_size);

    const std::filesystem::path drm_root{ "/sys/class/drm" };
    std::error_code iterator_error;
    if (std::filesystem::is_directory(drm_root, iterator_error) && !iterator_error) {
        result.gpu_probe_status = "complete";
        for (std::filesystem::directory_iterator it(drm_root, iterator_error), end; !iterator_error && it != end; it.increment(iterator_error)) {
            const auto name = it->path().filename().string();
            if (name.rfind("card", 0) != 0 || name.size() < 5 ||
                !std::all_of(name.begin() + 4, name.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) continue;
            const auto device = it->path() / "device";
            const auto vendor_value = read_hex_file(device / "vendor");
            if (vendor_value == 0) continue;
            GpuAdapterInfo gpu;
            gpu.vendor = vendor_name(static_cast<std::uint32_t>(vendor_value));
            gpu.vendor_id = hex_id(static_cast<std::uint32_t>(vendor_value));
            gpu.device_id = hex_id(static_cast<std::uint32_t>(read_hex_file(device / "device")));
            gpu.dedicated_memory_bytes = read_decimal_file(device / "mem_info_vram_total");
            const auto product_name = read_one_line(device / "product_name");
            gpu.name = product_name.empty() ? (gpu.vendor + " graphics adapter") : product_name;
            if (std::none_of(result.gpus.begin(), result.gpus.end(), [&](const GpuAdapterInfo& existing) {
                    return existing.vendor_id == gpu.vendor_id && existing.device_id == gpu.device_id;
                })) result.gpus.push_back(std::move(gpu));
        }
        if (iterator_error) {
            result.gpu_probe_status = "partial";
            result.gpu_probe_message = "The Linux DRM adapter list could not be read completely.";
        }
    } else {
        result.gpu_probe_status = "partial";
        result.gpu_probe_message = "The Linux DRM adapter list is unavailable; a GPU may still be present.";
    }
#elif defined(__APPLE__)
    std::uint64_t total_memory = 0;
    std::size_t length = sizeof(total_memory);
    if (sysctlbyname("hw.memsize", &total_memory, &length, nullptr, 0) == 0) result.total_memory_bytes = total_memory;
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    vm_statistics64_data_t vm_stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_page_size(host, &page_size) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm_stats), &count) == KERN_SUCCESS) {
        const auto free_pages = static_cast<std::uint64_t>(vm_stats.free_count) + static_cast<std::uint64_t>(vm_stats.inactive_count);
        result.available_memory_bytes = free_pages * static_cast<std::uint64_t>(page_size);
    }
    mach_port_deallocate(mach_task_self(), host);
    result.gpu_probe_status = "unsupported";
    result.gpu_probe_message = "Apple GPU details are not yet enumerated by this build; unified memory is included in system RAM.";
#else
    result.gpu_probe_status = "unsupported";
    result.gpu_probe_message = "Native GPU enumeration is not implemented on this platform.";
#endif

    std::error_code disk_error;
    if (!storage_path.empty()) {
        const auto storage = std::filesystem::space(storage_path, disk_error);
        if (!disk_error) result.available_disk_bytes = storage.available;
    }
    if (result.gpu_probe_status == "complete" && result.gpus.empty()) {
        result.gpu_probe_message = "No graphics adapter was returned by the operating-system adapter list.";
    }
    if (result.gpu_probe_status == "complete" && !result.gpus.empty()) {
        result.gpu_probe_message = "Graphics adapters detected locally. Adapter presence does not prove a compatible AI backend or GPU inference.";
    }
    return result;
}

} // namespace lar
