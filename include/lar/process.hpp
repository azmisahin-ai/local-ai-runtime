#pragma once

#include "lar/config.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
// Keep the Winsock 2 headers ahead of windows.h. Including windows.h first
// can pull in legacy WinSock.h and break Boost.Asio/Beast compilation with
// C1189: "WinSock.h has already been included" on MSVC.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#endif

namespace lar {

enum class ProcessState { stopped, running, failed };

struct EngineStatus {
    EngineConfig config;
    ProcessState state{ProcessState::stopped};
    std::int64_t pid{0};
    int exit_code{0};
    std::string last_error;
};

class ManagedProcess {
public:
    ManagedProcess() = default;
    ManagedProcess(const ManagedProcess&) = delete;
    ManagedProcess& operator=(const ManagedProcess&) = delete;
    ManagedProcess(ManagedProcess&& other) noexcept;
    ManagedProcess& operator=(ManagedProcess&& other) noexcept;
    ~ManagedProcess();

    bool start(const EngineConfig& config, std::string& error);
    bool stop(std::string& error);
    ProcessState state();
    std::int64_t pid() const;
    int exit_code() const;

private:
    void refresh_state();
    void close_handles();

#ifdef _WIN32
    HANDLE process_handle_{nullptr};
    DWORD pid_{0};
#else
    int pid_{-1};
#endif
    ProcessState state_{ProcessState::stopped};
    int exit_code_{0};
};

class EngineManager {
public:
    explicit EngineManager(std::vector<EngineConfig> configs);
    ~EngineManager();
    EngineManager(const EngineManager&) = delete;
    EngineManager& operator=(const EngineManager&) = delete;

    bool start(const std::string& id, std::string& error);
    bool stop(const std::string& id, std::string& error);
    std::vector<EngineStatus> statuses();
    std::vector<EngineConfig> configurations();
    // Replaces the complete config only while all managed engines are stopped.
    // The persistence callback executes under the manager lock before in-memory state changes.
    bool replace_configs(std::vector<EngineConfig> configs,
                         const std::function<bool(std::string&)>& persist,
                         std::string& error);
    std::optional<EngineStatus> default_chat_engine();

private:
    struct ManagedEngine {
        EngineConfig config;
        ManagedProcess process;
        std::string last_error;
    };
    std::mutex mutex_;
    std::vector<ManagedEngine> engines_;
};

std::string to_string(ProcessState state);

} // namespace lar
