#include "lar/gpu_monitor.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace lar {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t max_snapshot_bytes = 16U * 1024U;
constexpr auto command_timeout = std::chrono::milliseconds(1200);

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

int parse_metric(std::string_view value) {
    const auto cleaned = trim(value);
    if (cleaned.empty() || cleaned == "N/A" || cleaned == "Not Supported") return -1;
    int result = -1;
    const auto* begin = cleaned.data();
    const auto* end = cleaned.data() + cleaned.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end || result < 0) return -1;
    return result;
}

std::vector<std::string_view> comma_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const auto comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    return fields;
}

#ifdef _WIN32
std::optional<std::string> run_nvidia_smi() {
    std::wstring executable(32768, L'\0');
    const DWORD path_length = SearchPathW(nullptr, L"nvidia-smi.exe", nullptr,
                                           static_cast<DWORD>(executable.size()), executable.data(), nullptr);
    if (path_length == 0 || path_length >= executable.size()) return std::nullopt;
    executable.resize(path_length);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) return std::nullopt;
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return std::nullopt;
    }
    HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (null_input == INVALID_HANDLE_VALUE) null_input = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring command = L"\"" + executable + L"\" --query-gpu=index,utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = null_input;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (null_input && null_input != INVALID_HANDLE_VALUE && null_input != GetStdHandle(STD_INPUT_HANDLE)) CloseHandle(null_input);
    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        return std::nullopt;
    }

    std::string output;
    output.reserve(512);
    const auto deadline = Clock::now() + command_timeout;
    bool exited = false;
    while (Clock::now() < deadline) {
        DWORD available = 0;
        if (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            char buffer[2048];
            const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(available, sizeof(buffer)));
            DWORD read_count = 0;
            if (ReadFile(read_pipe, buffer, wanted, &read_count, nullptr) && read_count > 0) {
                if (output.size() + read_count > max_snapshot_bytes) {
                    TerminateProcess(process.hProcess, 1);
                    break;
                }
                output.append(buffer, buffer + read_count);
            }
        }
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
            exited = true;
            DWORD remaining = 0;
            if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &remaining, nullptr) || remaining == 0) break;
        }
        Sleep(10);
    }
    if (!exited && WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 200);
    }
    DWORD exit_code = 1;
    (void)GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_pipe);
    if (!exited || exit_code != 0) return std::nullopt;
    return output;
}
#else
std::optional<std::string> run_nvidia_smi() {
    int descriptors[2]{};
    if (pipe(descriptors) != 0) return std::nullopt;

    posix_spawn_file_actions_t actions{};
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return std::nullopt;
    }
    int setup_status = posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
    if (setup_status == 0) setup_status = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (setup_status == 0) setup_status = posix_spawn_file_actions_addclose(&actions, descriptors[0]);
    if (setup_status == 0) setup_status = posix_spawn_file_actions_addclose(&actions, descriptors[1]);
    if (setup_status != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(descriptors[0]);
        close(descriptors[1]);
        return std::nullopt;
    }

    char executable[] = "nvidia-smi";
    char query[] = "--query-gpu=index,utilization.gpu,memory.used,memory.total";
    char format[] = "--format=csv,noheader,nounits";
    char* arguments[] = {executable, query, format, nullptr};
    pid_t child = -1;
    const int spawn_status = posix_spawnp(&child, executable, &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (spawn_status != 0) {
        close(descriptors[0]);
        return std::nullopt;
    }

    const int original_flags = fcntl(descriptors[0], F_GETFL, 0);
    if (original_flags < 0 || fcntl(descriptors[0], F_SETFL, original_flags | O_NONBLOCK) < 0) {
        (void)kill(child, SIGKILL);
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        close(descriptors[0]);
        return std::nullopt;
    }
    std::string output;
    output.reserve(512);
    const auto deadline = Clock::now() + command_timeout;
    int child_status = 0;
    bool exited = false;
    while (Clock::now() < deadline) {
        char buffer[2048];
        for (;;) {
            const auto count = read(descriptors[0], buffer, sizeof(buffer));
            if (count > 0) {
                if (output.size() + static_cast<std::size_t>(count) > max_snapshot_bytes) {
                    (void)kill(child, SIGKILL);
                    break;
                }
                output.append(buffer, static_cast<std::size_t>(count));
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            break;
        }
        const pid_t waited = waitpid(child, &child_status, WNOHANG);
        if (waited == child) { exited = true; break; }
        pollfd descriptor{descriptors[0], POLLIN, 0};
        (void)poll(&descriptor, 1, 20);
    }
    if (!exited) {
        (void)kill(child, SIGKILL);
        while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {}
    }
    close(descriptors[0]);
    if (!exited || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) return std::nullopt;
    return output;
}
#endif

std::optional<std::vector<NvidiaGpuSample>> current_snapshot() {
    const auto output = run_nvidia_smi();
    if (!output) return std::nullopt;
    std::vector<NvidiaGpuSample> samples;
    if (!parse_nvidia_smi_snapshot(*output, samples)) return std::nullopt;
    return samples;
}

struct DeviceAggregate {
    NvidiaGpuObservationDevice observation;
};

void apply_baseline(std::map<int, DeviceAggregate>& aggregate, const std::vector<NvidiaGpuSample>& samples) {
    for (const auto& sample : samples) {
        auto& item = aggregate[sample.index].observation;
        item.index = sample.index;
        if (item.memory_before_mib < 0 && sample.memory_used_mib >= 0) item.memory_before_mib = sample.memory_used_mib;
        if (sample.memory_total_mib >= 0) item.memory_total_mib = sample.memory_total_mib;
    }
}

void apply_active_sample(std::map<int, DeviceAggregate>& aggregate, const std::vector<NvidiaGpuSample>& samples) {
    for (const auto& sample : samples) {
        auto& item = aggregate[sample.index].observation;
        item.index = sample.index;
        ++item.samples_during_request;
        if (sample.utilization_percent >= 0) {
            item.peak_utilization_percent = std::max(item.peak_utilization_percent, sample.utilization_percent);
        }
        if (sample.memory_used_mib >= 0) item.memory_peak_mib = std::max(item.memory_peak_mib, sample.memory_used_mib);
        if (sample.memory_total_mib >= 0) item.memory_total_mib = sample.memory_total_mib;
    }
}

void apply_after(std::map<int, DeviceAggregate>& aggregate, const std::vector<NvidiaGpuSample>& samples) {
    for (const auto& sample : samples) {
        auto& item = aggregate[sample.index].observation;
        item.index = sample.index;
        if (sample.memory_used_mib >= 0) item.memory_after_mib = sample.memory_used_mib;
        if (sample.memory_total_mib >= 0) item.memory_total_mib = sample.memory_total_mib;
        if (item.memory_peak_mib < 0 && sample.memory_used_mib >= 0) item.memory_peak_mib = sample.memory_used_mib;
    }
}
} // namespace

bool parse_nvidia_smi_snapshot(std::string_view output, std::vector<NvidiaGpuSample>& samples) {
    samples.clear();
    if (output.size() > max_snapshot_bytes) return false;
    std::istringstream lines{std::string(output)};
    std::string line;
    while (std::getline(lines, line)) {
        if (trim(line).empty()) continue;
        const auto fields = comma_fields(line);
        if (fields.size() != 4) continue;
        NvidiaGpuSample sample;
        sample.index = parse_metric(fields[0]);
        if (sample.index < 0) continue;
        sample.utilization_percent = parse_metric(fields[1]);
        sample.memory_used_mib = parse_metric(fields[2]);
        sample.memory_total_mib = parse_metric(fields[3]);
        if (sample.utilization_percent > 100) sample.utilization_percent = -1;
        if (sample.utilization_percent < 0 && sample.memory_used_mib < 0 && sample.memory_total_mib < 0) continue;
        samples.push_back(sample);
        if (samples.size() >= 32) break;
    }
    return !samples.empty();
}

NvidiaGpuObservation observe_nvidia_gpu_during(const std::function<bool()>& task, int sample_interval_ms) {
    NvidiaGpuObservation result;
    const auto baseline = current_snapshot();
    result.telemetry_available = baseline.has_value();
    if (!task) {
        result.status = "task-failed";
        result.message = "No local inference task was supplied.";
        return result;
    }

    std::atomic_bool stop_sampling{false};
    std::atomic_bool task_active{false};
    std::mutex samples_mutex;
    std::vector<std::vector<NvidiaGpuSample>> active_samples;
    std::thread sampler;
    const int interval = std::clamp(sample_interval_ms, 200, 2000);
    if (baseline) {
        sampler = std::thread([&] {
            auto next_sample = Clock::now();
            while (!stop_sampling.load(std::memory_order_relaxed)) {
                auto sample = current_snapshot();
                if (sample && task_active.load(std::memory_order_relaxed)) {
                    std::lock_guard<std::mutex> lock(samples_mutex);
                    active_samples.push_back(std::move(*sample));
                }
                next_sample += std::chrono::milliseconds(interval);
                const auto now = Clock::now();
                if (next_sample < now) next_sample = now + std::chrono::milliseconds(interval);
                while (!stop_sampling.load(std::memory_order_relaxed) && Clock::now() < next_sample) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        });
    }

    const auto started = Clock::now();
    task_active.store(true, std::memory_order_relaxed);
    try {
        result.task_succeeded = task();
    } catch (...) {
        result.task_succeeded = false;
    }
    task_active.store(false, std::memory_order_relaxed);
    result.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
    stop_sampling.store(true, std::memory_order_relaxed);
    if (sampler.joinable()) sampler.join();

    std::vector<std::vector<NvidiaGpuSample>> during;
    {
        std::lock_guard<std::mutex> lock(samples_mutex);
        during = std::move(active_samples);
    }
    result.samples_during_request = static_cast<int>(during.size());
    std::optional<std::vector<NvidiaGpuSample>> after;
    if (baseline) after = current_snapshot();
    result.telemetry_available = result.telemetry_available || !during.empty() || after.has_value();

    std::map<int, DeviceAggregate> aggregate;
    if (baseline) apply_baseline(aggregate, *baseline);
    for (const auto& sample : during) apply_active_sample(aggregate, sample);
    if (after) apply_after(aggregate, *after);
    for (auto& [index, entry] : aggregate) {
        (void)index;
        result.devices.push_back(entry.observation);
    }

    if (!result.task_succeeded) {
        result.status = "task-failed";
        result.message = "The local inference request did not return a valid chat response. GPU measurements, if any, are still system-wide observations; review local engine diagnostics.";
    } else if (!result.telemetry_available) {
        result.status = "telemetry-unavailable";
        result.message = "The local inference request completed, but nvidia-smi telemetry was unavailable. GPU activity was not measured.";
    } else if (result.samples_during_request == 0) {
        result.status = "request-too-short";
        result.message = "The local inference request completed before a GPU telemetry sample could be collected. Repeat with a longer generation to observe activity.";
    } else {
        result.status = "sampled";
        result.message = "NVIDIA system telemetry was sampled during a real local inference request. Available utilization/memory values are system-wide and may include other applications; correlate them with the current engine's GPU offload evidence.";
    }
    return result;
}

} // namespace lar
