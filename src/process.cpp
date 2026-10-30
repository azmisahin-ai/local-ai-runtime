#include "lar/process.hpp"
#include "lar/diagnostics.hpp"
#include "lar/backend_evidence.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include <stdexcept>
#include <vector>
#include <utility>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>

extern char** environ;
#endif

namespace lar {
namespace {

std::string replace_model_token(std::string value, const std::string& model_path) {
    const std::string token = "{model_path}";
    std::size_t pos = 0;
    while ((pos = value.find(token, pos)) != std::string::npos) {
        value.replace(pos, token.size(), model_path);
        pos += model_path.size();
    }
    return value;
}

void rotate_engine_log_if_needed(const std::filesystem::path& log_path) {
    constexpr std::uintmax_t max_engine_log_bytes = 5U * 1024U * 1024U;
    std::error_code ec;
    const auto size = std::filesystem::file_size(log_path, ec);
    if (ec || size <= max_engine_log_bytes) return;
    auto backup = log_path;
    backup += ".1";
    ec.clear();
    std::filesystem::remove(backup, ec);
    ec.clear();
    std::filesystem::rename(log_path, backup, ec);
    // If an engine or scanner still holds the file, prefer continuing with the
    // existing log rather than preventing the engine from starting.
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (size <= 0) throw std::runtime_error("Invalid UTF-8 in process argument or path.");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), size);
    result.pop_back();
    return result;
}

std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring result = L"\"";
    unsigned int backslashes = 0;
    for (const wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(ch);
        }
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}
#endif

} // namespace

ManagedProcess::ManagedProcess(ManagedProcess&& other) noexcept {
#ifdef _WIN32
    process_handle_ = std::exchange(other.process_handle_, nullptr);
    pid_ = std::exchange(other.pid_, 0);
#else
    pid_ = std::exchange(other.pid_, -1);
#endif
    state_ = std::exchange(other.state_, ProcessState::stopped);
    exit_code_ = std::exchange(other.exit_code_, 0);
}

ManagedProcess& ManagedProcess::operator=(ManagedProcess&& other) noexcept {
    if (this == &other) return *this;
    std::string ignored;
    stop(ignored);
    close_handles();
#ifdef _WIN32
    process_handle_ = std::exchange(other.process_handle_, nullptr);
    pid_ = std::exchange(other.pid_, 0);
#else
    pid_ = std::exchange(other.pid_, -1);
#endif
    state_ = std::exchange(other.state_, ProcessState::stopped);
    exit_code_ = std::exchange(other.exit_code_, 0);
    return *this;
}

ManagedProcess::~ManagedProcess() {
    std::string ignored;
    stop(ignored);
    close_handles();
}

void ManagedProcess::close_handles() {
#ifdef _WIN32
    if (process_handle_ != nullptr) {
        CloseHandle(process_handle_);
        process_handle_ = nullptr;
    }
#endif
}

bool ManagedProcess::start(const EngineConfig& config, std::string& error) {
    refresh_state();
    if (state_ == ProcessState::running) return true;
    close_handles();

    if (config.executable_path.empty()) {
        error = "executable_path is empty. Edit config/engines.json first.";
        state_ = ProcessState::failed;
        return false;
    }
    std::error_code fs_error;
    auto executable = std::filesystem::absolute(config.executable_path, fs_error);
    if (fs_error || !std::filesystem::is_regular_file(executable, fs_error)) {
        error = "Engine executable was not found: " + config.executable_path.string();
        state_ = ProcessState::failed;
        return false;
    }
    if (!config.model_path.empty() && !std::filesystem::is_regular_file(config.model_path, fs_error)) {
        error = "Model file was not found: " + config.model_path.string();
        state_ = ProcessState::failed;
        return false;
    }

    const auto args = expand_arguments(config);
    const auto cwd = config.working_directory.empty()
        ? executable.parent_path()
        : std::filesystem::absolute(config.working_directory, fs_error);
    if (!std::filesystem::is_directory(cwd, fs_error)) {
        error = "Engine working directory does not exist: " + cwd.string();
        state_ = ProcessState::failed;
        return false;
    }

    const std::filesystem::path log_dir = runtime_logs_directory();
    std::filesystem::create_directories(log_dir, fs_error);
    if (fs_error) {
        error = "Cannot create log directory: " + log_dir.string();
        state_ = ProcessState::failed;
        return false;
    }
    const auto log_path = log_dir / (config.id + ".log");
    rotate_engine_log_if_needed(log_path);

#ifdef _WIN32
    try {
        std::wstring command = quote_windows_arg(executable.wstring());
        for (const auto& arg : args) {
            command.push_back(L' ');
            command += quote_windows_arg(utf8_to_wide(arg));
        }
        std::vector<wchar_t> command_buffer(command.begin(), command.end());
        command_buffer.push_back(L'\0');

        SECURITY_ATTRIBUTES security_attributes{};
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = TRUE;
        HANDLE log_handle = CreateFileW(log_path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log_handle == INVALID_HANDLE_VALUE) {
            error = "Cannot open engine log file: " + log_path.string();
            state_ = ProcessState::failed;
            return false;
        }
        constexpr std::string_view session_marker = "\n=== LAR_ENGINE_SESSION_BEGIN ===\n";
        DWORD marker_written = 0;
        if (!WriteFile(log_handle, session_marker.data(), static_cast<DWORD>(session_marker.size()), &marker_written, nullptr) ||
            marker_written != static_cast<DWORD>(session_marker.size())) {
            log_diagnostic("WARN", "engine.log_session_marker_failed", "Could not mark current engine log session");
        }
        HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = null_input;
        startup.hStdOutput = log_handle;
        startup.hStdError = log_handle;
        PROCESS_INFORMATION process{};
        const auto cwd_wide = cwd.wstring();
        const BOOL ok = CreateProcessW(executable.c_str(), command_buffer.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, cwd_wide.c_str(), &startup, &process);
        CloseHandle(log_handle);
        if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
        if (!ok) {
            error = "CreateProcessW failed with Windows error " + std::to_string(GetLastError());
            state_ = ProcessState::failed;
            return false;
        }
        CloseHandle(process.hThread);
        process_handle_ = process.hProcess;
        pid_ = process.dwProcessId;
        state_ = ProcessState::running;
        exit_code_ = 0;
        error.clear();
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        state_ = ProcessState::failed;
        return false;
    }
#else
    // This method is called by an HTTP request thread. After fork(), a multithreaded
    // process may safely call only async-signal-safe functions before exec(). The old
    // child path used std::filesystem, std::string, and std::vector after fork; on
    // macOS those operations can deadlock on locks inherited from other threads.
    // posix_spawn performs redirection, process-group setup, and chdir through its
    // defined spawn actions without running C++ code in a post-fork child.
    const int log_fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        error = "Cannot open engine log file: " + log_path.string();
        state_ = ProcessState::failed;
        return false;
    }
    constexpr std::string_view session_marker = "\n=== LAR_ENGINE_SESSION_BEGIN ===\n";
    const auto marker_written = ::write(log_fd, session_marker.data(), session_marker.size());
    if (marker_written != static_cast<ssize_t>(session_marker.size())) {
        log_diagnostic("WARN", "engine.log_session_marker_failed", "Could not mark current engine log session");
    }

    std::vector<std::string> storage;
    storage.reserve(args.size() + 1);
    storage.push_back(executable.string());
    for (const auto& arg : args) storage.push_back(replace_model_token(arg, config.model_path.string()));
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& item : storage) argv.push_back(item.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t file_actions{};
    int spawn_error = ::posix_spawn_file_actions_init(&file_actions);
    if (spawn_error != 0) {
        ::close(log_fd);
        error = "posix_spawn_file_actions_init failed: " + std::string(std::strerror(spawn_error));
        state_ = ProcessState::failed;
        return false;
    }

    auto fail_file_action = [&](const char* operation, int code) {
        (void)::posix_spawn_file_actions_destroy(&file_actions);
        ::close(log_fd);
        error = std::string(operation) + " failed: " + std::strerror(code);
        state_ = ProcessState::failed;
        return false;
    };

    // Change directory in the spawn action, not in a forked child.
    spawn_error = ::posix_spawn_file_actions_addchdir_np(&file_actions, cwd.c_str());
    if (spawn_error != 0) return fail_file_action("posix_spawn chdir action", spawn_error);

    spawn_error = ::posix_spawn_file_actions_addopen(
        &file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (spawn_error != 0) return fail_file_action("posix_spawn stdin action", spawn_error);

    spawn_error = ::posix_spawn_file_actions_adddup2(&file_actions, log_fd, STDOUT_FILENO);
    if (spawn_error != 0) return fail_file_action("posix_spawn stdout action", spawn_error);
    spawn_error = ::posix_spawn_file_actions_adddup2(&file_actions, log_fd, STDERR_FILENO);
    if (spawn_error != 0) return fail_file_action("posix_spawn stderr action", spawn_error);
    if (log_fd > STDERR_FILENO) {
        spawn_error = ::posix_spawn_file_actions_addclose(&file_actions, log_fd);
        if (spawn_error != 0) return fail_file_action("posix_spawn log close action", spawn_error);
    }

    posix_spawnattr_t attributes{};
    spawn_error = ::posix_spawnattr_init(&attributes);
    if (spawn_error != 0) {
        (void)::posix_spawn_file_actions_destroy(&file_actions);
        ::close(log_fd);
        error = "posix_spawnattr_init failed: " + std::string(std::strerror(spawn_error));
        state_ = ProcessState::failed;
        return false;
    }
    spawn_error = ::posix_spawnattr_setpgroup(&attributes, 0);
    if (spawn_error == 0) {
        spawn_error = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    }
    if (spawn_error != 0) {
        (void)::posix_spawnattr_destroy(&attributes);
        (void)::posix_spawn_file_actions_destroy(&file_actions);
        ::close(log_fd);
        error = "posix_spawn process-group setup failed: " + std::string(std::strerror(spawn_error));
        state_ = ProcessState::failed;
        return false;
    }

    pid_t child = -1;
    spawn_error = ::posix_spawn(&child, executable.c_str(), &file_actions, &attributes,
                                argv.data(), environ);
    (void)::posix_spawnattr_destroy(&attributes);
    (void)::posix_spawn_file_actions_destroy(&file_actions);
    ::close(log_fd);
    if (spawn_error != 0) {
        error = "posix_spawn failed for '" + executable.string() + "': " + std::strerror(spawn_error);
        state_ = ProcessState::failed;
        return false;
    }

    pid_ = static_cast<int>(child);
    state_ = ProcessState::running;
    exit_code_ = 0;
    error.clear();
    return true;
#endif
}

void ManagedProcess::refresh_state() {
#ifdef _WIN32
    if (process_handle_ == nullptr) return;
    DWORD code = STILL_ACTIVE;
    if (GetExitCodeProcess(process_handle_, &code) && code != STILL_ACTIVE) {
        exit_code_ = static_cast<int>(code);
        state_ = code == 0 ? ProcessState::stopped : ProcessState::failed;
        close_handles();
        pid_ = 0;
    }
#else
    if (pid_ <= 0 || state_ != ProcessState::running) return;
    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (result == 0) return;
    if (result == static_cast<pid_t>(pid_)) {
        if (WIFEXITED(status)) {
            exit_code_ = WEXITSTATUS(status);
            state_ = exit_code_ == 0 ? ProcessState::stopped : ProcessState::failed;
        } else if (WIFSIGNALED(status)) {
            exit_code_ = 128 + WTERMSIG(status);
            state_ = ProcessState::failed;
        }
        pid_ = -1;
    } else if (result < 0 && errno == ECHILD) {
        state_ = ProcessState::stopped;
        pid_ = -1;
    }
#endif
}

bool ManagedProcess::stop(std::string& error) {
    refresh_state();
    if (state_ != ProcessState::running) {
        error.clear();
        return true;
    }
#ifdef _WIN32
    if (process_handle_ == nullptr) {
        state_ = ProcessState::stopped;
        pid_ = 0;
        return true;
    }
    if (!TerminateProcess(process_handle_, 0)) {
        const DWORD code = GetLastError();
        DWORD observed_exit_code = STILL_ACTIVE;
        if (GetExitCodeProcess(process_handle_, &observed_exit_code) && observed_exit_code != STILL_ACTIVE) {
            exit_code_ = static_cast<int>(observed_exit_code);
            state_ = observed_exit_code == 0 ? ProcessState::stopped : ProcessState::failed;
            pid_ = 0;
            close_handles();
            error.clear();
            return true;
        }
        error = "TerminateProcess failed with Windows error " + std::to_string(code);
        return false;
    }
    const DWORD wait_result = WaitForSingleObject(process_handle_, 5000);
    if (wait_result != WAIT_OBJECT_0) {
        if (wait_result == WAIT_TIMEOUT) {
            error = "Timed out waiting for the engine process to exit after termination.";
        } else {
            error = "WaitForSingleObject failed with Windows error " + std::to_string(GetLastError());
        }
        return false;
    }
    DWORD observed_exit_code = STILL_ACTIVE;
    if (!GetExitCodeProcess(process_handle_, &observed_exit_code) || observed_exit_code == STILL_ACTIVE) {
        error = "The engine process handle signaled, but its exit state could not be confirmed.";
        return false;
    }
    exit_code_ = static_cast<int>(observed_exit_code);
    state_ = ProcessState::stopped;
    pid_ = 0;
    close_handles();
    error.clear();
    return true;
#else
    const pid_t target = static_cast<pid_t>(pid_);
    // The child is placed into its own process group so helper processes are also stopped.
    if (::kill(-target, SIGTERM) != 0 && errno != ESRCH) {
        error = "SIGTERM failed with errno " + std::to_string(errno);
        return false;
    }
    for (int i = 0; i < 30; ++i) {
        refresh_state();
        if (state_ != ProcessState::running) {
            // A signal caused by an explicit stop request is an expected shutdown, not a crash.
            state_ = ProcessState::stopped;
            exit_code_ = 0;
            pid_ = -1;
            error.clear();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (::kill(-target, SIGKILL) != 0 && errno != ESRCH) {
        error = "SIGKILL failed with errno " + std::to_string(errno);
        return false;
    }
    for (int i = 0; i < 10; ++i) {
        refresh_state();
        if (state_ != ProcessState::running) {
            state_ = ProcessState::stopped;
            exit_code_ = 0;
            pid_ = -1;
            error.clear();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    error = "Process did not exit after SIGKILL; state could not be confirmed.";
    return false;
#endif
}

ProcessState ManagedProcess::state() {
    refresh_state();
    return state_;
}

std::int64_t ManagedProcess::pid() const {
#ifdef _WIN32
    return static_cast<std::int64_t>(pid_);
#else
    return static_cast<std::int64_t>(pid_ > 0 ? pid_ : 0);
#endif
}

int ManagedProcess::exit_code() const { return exit_code_; }

EngineManager::EngineManager(std::vector<EngineConfig> configs) {
    engines_.reserve(configs.size());
    for (auto& config : configs) {
        ManagedEngine engine;
        engine.config = std::move(config);
        engines_.push_back(std::move(engine));
    }
}

EngineManager::~EngineManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& engine : engines_) {
        std::string ignored;
        (void)engine.process.stop(ignored);
    }
}

bool EngineManager::start(const std::string& id, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& engine : engines_) {
        if (engine.config.id == id) {
            const bool ok = engine.process.start(engine.config, error);
            engine.last_error = ok ? std::string{} : error;
            return ok;
        }
    }
    error = "No engine with id '" + id + "' is configured.";
    return false;
}

bool EngineManager::stop(const std::string& id, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& engine : engines_) {
        if (engine.config.id == id) {
            const bool ok = engine.process.stop(error);
            engine.last_error = ok ? std::string{} : error;
            return ok;
        }
    }
    error = "No engine with id '" + id + "' is configured.";
    return false;
}

std::vector<EngineStatus> EngineManager::statuses() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EngineStatus> result;
    result.reserve(engines_.size());
    for (auto& engine : engines_) {
        result.push_back(EngineStatus{engine.config, engine.process.state(), engine.process.pid(),
                                      engine.process.exit_code(), engine.last_error});
    }
    return result;
}

std::vector<EngineConfig> EngineManager::configurations() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EngineConfig> result;
    result.reserve(engines_.size());
    for (const auto& engine : engines_) result.push_back(engine.config);
    return result;
}

bool EngineManager::replace_configs(std::vector<EngineConfig> configs,
                                    const std::function<bool(std::string&)>& persist,
                                    std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& engine : engines_) {
        if (engine.process.state() == ProcessState::running) {
            error = "Stop all running engines before changing engine configuration.";
            return false;
        }
    }

    std::vector<std::string> ids;
    ids.reserve(configs.size());
    unsigned int default_count = 0;
    for (const auto& config : configs) {
        if (!is_valid_engine_id(config.id)) {
            error = "Invalid engine id in new configuration: " + config.id;
            return false;
        }
        if (std::find(ids.begin(), ids.end(), config.id) != ids.end()) {
            error = "Duplicate engine id in new configuration: " + config.id;
            return false;
        }
        ids.push_back(config.id);
        if (config.default_for_chat) ++default_count;
    }
    if (default_count > 1) {
        error = "Only one engine may be the default chat engine.";
        return false;
    }

    if (persist && !persist(error)) return false;

    std::vector<ManagedEngine> replacement;
    replacement.reserve(configs.size());
    for (auto& config : configs) {
        ManagedEngine engine;
        engine.config = std::move(config);
        replacement.push_back(std::move(engine));
    }
    engines_ = std::move(replacement);
    error.clear();
    return true;
}

std::optional<EngineStatus> EngineManager::default_chat_engine() {
    std::lock_guard<std::mutex> lock(mutex_);
    ManagedEngine* selected = nullptr;
    for (auto& engine : engines_) {
        if (engine.config.default_for_chat) { selected = &engine; break; }
        if (selected == nullptr && !engine.config.upstream_base_url.empty()) selected = &engine;
    }
    if (selected == nullptr) return std::nullopt;
    return EngineStatus{selected->config, selected->process.state(), selected->process.pid(),
                        selected->process.exit_code(), selected->last_error};
}

std::string to_string(ProcessState state) {
    switch (state) {
        case ProcessState::stopped: return "stopped";
        case ProcessState::running: return "running";
        case ProcessState::failed: return "failed";
    }
    return "unknown";
}

} // namespace lar
