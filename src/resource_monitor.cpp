#include "lar/resource_monitor.hpp"

#include "lar/gpu_monitor.hpp"
#include "lar/hardware.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <limits>
#include <thread>
#include <vector>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <psapi.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/proc_info.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace lar {
namespace {
using Clock = std::chrono::steady_clock;

struct CpuCounters {
    std::uint64_t total{0};
    std::uint64_t idle{0};
};

struct ProcessCounters {
    std::uint64_t cpu_time{0};
    std::uint64_t resident_bytes{0};
};

struct PreviousProcessSample {
    std::uint64_t cpu_time{0};
    Clock::time_point sampled_at{};
};

std::uint64_t unix_time_millis() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::optional<double> percent_delta(std::uint64_t total_delta, std::uint64_t idle_delta) {
    if (total_delta == 0 || idle_delta > total_delta) return std::nullopt;
    const double busy = static_cast<double>(total_delta - idle_delta);
    return std::clamp((busy / static_cast<double>(total_delta)) * 100.0, 0.0, 100.0);
}

std::optional<double> process_percent(std::uint64_t cpu_delta,
                                       std::chrono::steady_clock::duration elapsed,
                                       unsigned logical_cpus,
                                       std::uint64_t ticks_per_second) {
    const auto elapsed_seconds = std::chrono::duration<double>(elapsed).count();
    if (elapsed_seconds <= 0.0 || ticks_per_second == 0 || logical_cpus == 0) return std::nullopt;
    const double cpu_seconds = static_cast<double>(cpu_delta) / static_cast<double>(ticks_per_second);
    const double normalized = (cpu_seconds / elapsed_seconds) * 100.0 / static_cast<double>(logical_cpus);
    return std::clamp(normalized, 0.0, 100.0);
}

#ifdef _WIN32
std::uint64_t filetime_value(const FILETIME& value) {
    ULARGE_INTEGER number{};
    number.LowPart = value.dwLowDateTime;
    number.HighPart = value.dwHighDateTime;
    return number.QuadPart;
}

std::optional<CpuCounters> read_system_cpu() {
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return std::nullopt;
    const auto idle_value = filetime_value(idle);
    const auto kernel_value = filetime_value(kernel);
    const auto user_value = filetime_value(user);
    return CpuCounters{kernel_value + user_value, idle_value};
}

bool read_system_memory(std::uint64_t& total, std::uint64_t& available) {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) return false;
    total = memory.ullTotalPhys;
    available = memory.ullAvailPhys;
    return total > 0;
}

std::optional<ProcessCounters> read_process_counters(std::int64_t pid) {
    if (pid <= 0 || static_cast<unsigned long long>(pid) > MAXDWORD) return std::nullopt;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (!process) return std::nullopt;
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool times_ok = GetProcessTimes(process, &created, &exited, &kernel, &user) != 0;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    const bool memory_ok = GetProcessMemoryInfo(process,
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), static_cast<DWORD>(sizeof(memory))) != 0;
    CloseHandle(process);
    if (!times_ok && !memory_ok) return std::nullopt;
    ProcessCounters result;
    if (times_ok) result.cpu_time = filetime_value(kernel) + filetime_value(user);
    if (memory_ok) result.resident_bytes = static_cast<std::uint64_t>(memory.WorkingSetSize);
    return result;
}
#elif defined(__APPLE__)
std::optional<CpuCounters> read_system_cpu() {
    host_cpu_load_info_data_t info{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    const auto host = mach_host_self();
    const auto status = host_statistics(host, HOST_CPU_LOAD_INFO,
        reinterpret_cast<host_info_t>(&info), &count);
    mach_port_deallocate(mach_task_self(), host);
    if (status != KERN_SUCCESS) return std::nullopt;
    std::uint64_t total = 0;
    for (const auto value : info.cpu_ticks) total += static_cast<std::uint64_t>(value);
    const auto idle = static_cast<std::uint64_t>(info.cpu_ticks[CPU_STATE_IDLE]);
    return CpuCounters{total, idle};
}

bool read_system_memory(std::uint64_t& total, std::uint64_t& available) {
    std::size_t length = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &length, nullptr, 0) != 0 || total == 0) return false;
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    vm_statistics64_data_t stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const bool ok = host_page_size(host, &page_size) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) == KERN_SUCCESS;
    mach_port_deallocate(mach_task_self(), host);
    if (!ok) return false;
    const auto free_pages = static_cast<std::uint64_t>(stats.free_count) + static_cast<std::uint64_t>(stats.inactive_count);
    available = free_pages * static_cast<std::uint64_t>(page_size);
    return true;
}

std::optional<ProcessCounters> read_process_counters(std::int64_t pid) {
    if (pid <= 0 || pid > std::numeric_limits<pid_t>::max()) return std::nullopt;
    proc_taskinfo info{};
    const int bytes = proc_pidinfo(static_cast<int>(pid), PROC_PIDTASKINFO, 0, &info, sizeof(info));
    if (bytes != static_cast<int>(sizeof(info))) return std::nullopt;
    return ProcessCounters{info.pti_total_user + info.pti_total_system, info.pti_resident_size};
}
#elif defined(__linux__)
std::optional<CpuCounters> read_system_cpu() {
    std::ifstream input("/proc/stat");
    std::string label;
    std::uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
    std::uint64_t irq = 0, softirq = 0, steal = 0;
    if (!(input >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) || label != "cpu") return std::nullopt;
    const auto total = user + nice + system + idle + iowait + irq + softirq + steal;
    return CpuCounters{total, idle + iowait};
}

bool read_system_memory(std::uint64_t& total, std::uint64_t& available) {
    std::ifstream input("/proc/meminfo");
    std::string label;
    std::uint64_t value = 0;
    std::string unit;
    std::uint64_t total_kib = 0;
    std::uint64_t available_kib = 0;
    while (input >> label >> value >> unit) {
        if (label == "MemTotal:") total_kib = value;
        else if (label == "MemAvailable:") available_kib = value;
        if (total_kib > 0 && available_kib > 0) break;
    }
    if (total_kib == 0) {
        struct sysinfo info{};
        if (sysinfo(&info) != 0) return false;
        const auto unit_size = static_cast<std::uint64_t>(info.mem_unit);
        total = static_cast<std::uint64_t>(info.totalram) * unit_size;
        available = (static_cast<std::uint64_t>(info.freeram) + static_cast<std::uint64_t>(info.bufferram)) * unit_size;
        return total > 0;
    }
    total = total_kib * 1024ULL;
    available = available_kib * 1024ULL;
    return total > 0;
}

std::optional<ProcessCounters> read_process_counters(std::int64_t pid) {
    if (pid <= 0 || pid > std::numeric_limits<pid_t>::max()) return std::nullopt;
    std::ifstream stat_file("/proc/" + std::to_string(pid) + "/stat");
    std::string stat_line;
    if (!std::getline(stat_file, stat_line)) return std::nullopt;
    const auto command_end = stat_line.rfind(')');
    if (command_end == std::string::npos || command_end + 2 >= stat_line.size()) return std::nullopt;
    std::istringstream fields(stat_line.substr(command_end + 2));
    std::vector<std::string> values;
    std::string value;
    while (fields >> value) values.push_back(std::move(value));
    // The first value is field 3 (state); utime/stime are fields 14 and 15.
    if (values.size() <= 12) return std::nullopt;
    std::uint64_t user_ticks = 0;
    std::uint64_t system_ticks = 0;
    try {
        user_ticks = std::stoull(values[11]);
        system_ticks = std::stoull(values[12]);
    } catch (...) { return std::nullopt; }

    std::uint64_t resident_bytes = 0;
    std::ifstream status_file("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("VmRSS:", 0) != 0) continue;
        std::istringstream rss(line.substr(6));
        std::uint64_t kib = 0;
        rss >> kib;
        resident_bytes = kib * 1024ULL;
        break;
    }
    return ProcessCounters{user_ticks + system_ticks, resident_bytes};
}
#else
std::optional<CpuCounters> read_system_cpu() { return std::nullopt; }
bool read_system_memory(std::uint64_t&, std::uint64_t&) { return false; }
std::optional<ProcessCounters> read_process_counters(std::int64_t) { return std::nullopt; }
#endif

std::uint64_t process_ticks_per_second() {
#ifdef _WIN32
    return 10'000'000ULL; // GetProcessTimes reports 100 ns units.
#elif defined(__APPLE__)
    return 1'000'000'000ULL; // proc_pidinfo reports nanoseconds.
#elif defined(__linux__)
    const long value = ::sysconf(_SC_CLK_TCK);
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
#else
    return 0;
#endif
}

std::string escape_json(std::string_view value) {
    std::ostringstream out;
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
                if (ch < 0x20U) {
                    constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(ch >> 4U) & 0x0fU] << hex[ch & 0x0fU];
                } else out << static_cast<char>(ch);
        }
    }
    return out.str();
}

void append_optional_number(std::ostringstream& out, const std::optional<double>& value) {
    if (value && std::isfinite(*value)) out << std::fixed << std::setprecision(2) << *value;
    else out << "null";
}

void append_optional_integer(std::ostringstream& out, const std::optional<std::uint64_t>& value) {
    if (value) out << *value;
    else out << "null";
}

class ResourceSampler {
public:
    ResourceSnapshot collect(const std::vector<EngineStatus>& engines) {
        std::lock_guard<std::mutex> lock(mutex_);
        ResourceSnapshot snapshot;
        snapshot.captured_at = unix_time_millis();
        snapshot.logical_cpu_count = std::max(1U, std::thread::hardware_concurrency());
#ifdef _WIN32
        snapshot.platform = "Windows";
#elif defined(__APPLE__)
        snapshot.platform = "macOS";
#elif defined(__linux__)
        snapshot.platform = "Linux";
#else
        snapshot.platform = "Unknown";
#endif

        const auto now = Clock::now();
        const auto cpu = read_system_cpu();
        if (cpu) {
            if (previous_cpu_) {
                const auto total_delta = cpu->total >= previous_cpu_->total ? cpu->total - previous_cpu_->total : 0;
                const auto idle_delta = cpu->idle >= previous_cpu_->idle ? cpu->idle - previous_cpu_->idle : total_delta + 1;
                snapshot.cpu_utilization_percent = percent_delta(total_delta, idle_delta);
            }
            previous_cpu_ = cpu;
        }

        (void)read_system_memory(snapshot.total_memory_bytes, snapshot.available_memory_bytes);
        snapshot.available_memory_bytes = std::min(snapshot.available_memory_bytes, snapshot.total_memory_bytes);
        if (snapshot.total_memory_bytes > 0) {
            snapshot.used_memory_bytes = snapshot.total_memory_bytes - snapshot.available_memory_bytes;
            snapshot.memory_utilization_percent = (static_cast<double>(snapshot.used_memory_bytes) /
                static_cast<double>(snapshot.total_memory_bytes)) * 100.0;
            const auto available_percent = static_cast<double>(snapshot.available_memory_bytes) /
                static_cast<double>(snapshot.total_memory_bytes) * 100.0;
            constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
            if (snapshot.available_memory_bytes < gib || available_percent <= 5.0) snapshot.memory_pressure = "critical";
            else if (snapshot.available_memory_bytes < 2ULL * gib || available_percent <= 12.5) snapshot.memory_pressure = "constrained";
            else snapshot.memory_pressure = "normal";
        }

        const auto ticks_per_second = process_ticks_per_second();
        std::map<std::int64_t, PreviousProcessSample> active_processes;
        for (const auto& engine : engines) {
            EngineResourceUsage usage;
            usage.engine_id = engine.config.id;
            usage.state = to_string(engine.state);
            if (engine.state == ProcessState::running && engine.pid > 0) {
                const auto process = read_process_counters(engine.pid);
                if (process) {
                    usage.resident_memory_bytes = process->resident_bytes;
                    const auto previous = previous_processes_.find(engine.pid);
                    if (previous != previous_processes_.end()) {
                        const auto delta = process->cpu_time >= previous->second.cpu_time
                            ? process->cpu_time - previous->second.cpu_time : 0;
                        usage.cpu_percent = process_percent(delta, now - previous->second.sampled_at,
                                                            snapshot.logical_cpu_count, ticks_per_second);
                    }
                    active_processes[engine.pid] = PreviousProcessSample{process->cpu_time, now};
                }
            }
            snapshot.engines.push_back(std::move(usage));
        }
        previous_processes_ = std::move(active_processes);

        const auto hardware = detect_hardware({});
        const bool nvidia_detected = std::any_of(hardware.gpus.begin(), hardware.gpus.end(), [](const GpuAdapterInfo& gpu) {
            return gpu.vendor == "NVIDIA";
        });
        if (nvidia_detected) {
            std::vector<NvidiaGpuSample> samples;
            if (read_nvidia_gpu_snapshot(samples)) {
                snapshot.gpu_telemetry_status = "available";
                snapshot.gpu_telemetry_message = "NVIDIA metrics are system-wide and may include activity from other applications.";
                for (const auto& sample : samples) {
                    ResourceGpuDevice gpu;
                    gpu.index = sample.index;
                    if (sample.utilization_percent >= 0) gpu.utilization_percent = sample.utilization_percent;
                    if (sample.memory_used_mib >= 0) gpu.memory_used_bytes = static_cast<std::uint64_t>(sample.memory_used_mib) * 1024ULL * 1024ULL;
                    if (sample.memory_total_mib >= 0) gpu.memory_total_bytes = static_cast<std::uint64_t>(sample.memory_total_mib) * 1024ULL * 1024ULL;
                    snapshot.gpus.push_back(std::move(gpu));
                }
            } else {
                snapshot.gpu_telemetry_message = "An NVIDIA adapter was detected, but live metrics were unavailable from nvidia-smi.";
            }
        } else {
            snapshot.gpu_telemetry_message = hardware.gpu_probe_status == "unsupported"
                ? "Live GPU utilization is not implemented for this adapter or platform; no value is inferred."
                : "No NVIDIA adapter was detected for the current live telemetry provider.";
        }
        return snapshot;
    }

private:
    std::mutex mutex_;
    std::optional<CpuCounters> previous_cpu_;
    std::map<std::int64_t, PreviousProcessSample> previous_processes_;
};

} // namespace

ResourceSnapshot collect_resource_snapshot(const std::vector<EngineStatus>& engines) {
    static ResourceSampler sampler;
    return sampler.collect(engines);
}

std::string resource_snapshot_json(const ResourceSnapshot& snapshot) {
    std::ostringstream out;
    out << "{\"schema_version\":1"
        << ",\"platform\":\"" << escape_json(snapshot.platform) << '\"'
        << ",\"captured_at\":" << snapshot.captured_at
        << ",\"logical_cpu_count\":" << snapshot.logical_cpu_count
        << ",\"cpu_utilization_percent\":";
    append_optional_number(out, snapshot.cpu_utilization_percent);
    out << ",\"total_memory_bytes\":" << snapshot.total_memory_bytes
        << ",\"available_memory_bytes\":" << snapshot.available_memory_bytes
        << ",\"used_memory_bytes\":" << snapshot.used_memory_bytes
        << ",\"memory_utilization_percent\":";
    append_optional_number(out, snapshot.memory_utilization_percent);
    out << ",\"memory_pressure\":\"" << escape_json(snapshot.memory_pressure) << '\"'
        << ",\"gpu_telemetry\":{\"status\":\"" << escape_json(snapshot.gpu_telemetry_status)
        << "\",\"message\":\"" << escape_json(snapshot.gpu_telemetry_message) << "\",\"scope\":\"system-wide\",\"devices\":[";
    for (std::size_t i = 0; i < snapshot.gpus.size(); ++i) {
        if (i) out << ',';
        const auto& gpu = snapshot.gpus[i];
        out << "{\"index\":" << gpu.index << ",\"utilization_percent\":";
        if (gpu.utilization_percent) out << *gpu.utilization_percent;
        else out << "null";
        out << ",\"memory_used_bytes\":";
        append_optional_integer(out, gpu.memory_used_bytes);
        out << ",\"memory_total_bytes\":";
        append_optional_integer(out, gpu.memory_total_bytes);
        out << '}';
    }
    out << "]},\"engines\":[";
    for (std::size_t i = 0; i < snapshot.engines.size(); ++i) {
        if (i) out << ',';
        const auto& engine = snapshot.engines[i];
        out << "{\"engine_id\":\"" << escape_json(engine.engine_id)
            << "\",\"state\":\"" << escape_json(engine.state) << "\",\"cpu_percent\":";
        append_optional_number(out, engine.cpu_percent);
        out << ",\"resident_memory_bytes\":";
        append_optional_integer(out, engine.resident_memory_bytes);
        out << '}';
    }
    out << "]}";
    return out.str();
}

} // namespace lar
