#include "lar/config.hpp"
#include "lar/diagnostics.hpp"
#include "lar/downloads.hpp"
#include "lar/process.hpp"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
void stage(const char* name) {
    std::cerr << "[core-tests] " << name << std::endl;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        stage("engine id validation");
        require(lar::is_valid_engine_id("llama_cpp-1"), "valid engine id rejected");
        require(!lar::is_valid_engine_id("../llama"), "path traversal engine id accepted");
        require(!lar::is_valid_engine_id(""), "empty engine id accepted");

        stage("archive path safety");
        // Engine archives are untrusted inputs; extraction must reject paths that
        // could escape the installation staging root on Windows or Unix.
        require(lar::DownloadManager::is_safe_archive_entry_path("bin/llama-server"), "safe archive path rejected");
        require(!lar::DownloadManager::is_safe_archive_entry_path("../outside"), "parent traversal archive path accepted");
        require(!lar::DownloadManager::is_safe_archive_entry_path("/absolute/path"), "absolute archive path accepted");
        require(!lar::DownloadManager::is_safe_archive_entry_path("C:\\outside.exe"), "Windows drive path accepted");
        require(!lar::DownloadManager::is_safe_archive_entry_path("folder\\..\\outside"), "Windows separator traversal accepted");

        stage("SHA-256 file streaming");
        const auto digest_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto digest_path = std::filesystem::temp_directory_path() / ("lar-sha256-" + digest_suffix + ".txt");
        {
            std::ofstream digest_file(digest_path, std::ios::binary);
            digest_file << "abc";
        }
        require(lar::DownloadManager::sha256_file(digest_path) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "SHA-256 verifier returned incorrect result");
        std::filesystem::remove(digest_path);

        stage("example config loading");
        const auto config = lar::load_config(LAR_TEST_CONFIG);
        require(config.host == "127.0.0.1", "default host must bind to loopback");
        require(config.port == 8000, "example port should be 8000");
        require(config.engines.size() == 1, "example must provide one llama.cpp adapter example");
        require(config.engines.front().id == "llama-cpp", "unexpected example engine id");

        auto engine = config.engines.front();
        engine.model_path = std::filesystem::path("models/test model.gguf");
        const auto args = lar::expand_arguments(engine);
        bool saw_model = false;
        for (const auto& arg : args) {
            require(arg.find("{model_path}") == std::string::npos, "model token was not expanded");
            if (arg == "models/test model.gguf") saw_model = true;
        }
        require(saw_model, "model path should be one argument (spaces preserved)");

        stage("UTF-8 config round-trip");
        // Configuration persistence must preserve path arguments and valid JSON.
        auto roundtrip = config;
        roundtrip.engines.front().executable_path = std::filesystem::current_path() / "tools" / "llama-server";
        roundtrip.engines.front().model_path = std::filesystem::current_path() / "models" / std::filesystem::path(u8"kullanıcı modeli üğşöçı.gguf");
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto roundtrip_path = std::filesystem::temp_directory_path() / ("lar-config-roundtrip-" + suffix + ".json");
        lar::save_config(roundtrip_path, roundtrip);
        const auto loaded_roundtrip = lar::load_config(roundtrip_path);
        require(loaded_roundtrip.engines.size() == roundtrip.engines.size(), "saved config engine count changed");
        require(loaded_roundtrip.engines.front().executable_path == roundtrip.engines.front().executable_path,
                "saved executable path did not round-trip");
        require(loaded_roundtrip.engines.front().model_path == roundtrip.engines.front().model_path,
                "saved Unicode model path did not round-trip");
        const auto roundtrip_args = lar::expand_arguments(loaded_roundtrip.engines.front());
        bool unicode_model_argument = false;
        for (const auto& argument : roundtrip_args) {
            if (argument.find("kullanıcı modeli üğşöçı.gguf") != std::string::npos) unicode_model_argument = true;
        }
        require(unicode_model_argument, "model argument did not preserve UTF-8 path encoding");
        std::filesystem::remove(roundtrip_path);

        stage("unknown engine handling");
        {
            std::vector<lar::EngineConfig> invalid_configs;
            lar::EngineManager manager(std::move(invalid_configs));
            std::string error;
            require(!manager.start("not-configured", error), "unknown engine should not start");
            require(!error.empty(), "unknown engine should return an actionable error");
        }

        stage("child process start/stop");
        lar::EngineConfig fake;
        fake.id = "fake-engine";
        fake.display_name = "Fake process for integration tests";
        fake.executable_path = LAR_FAKE_ENGINE;
        fake.working_directory = std::filesystem::current_path();
        lar::EngineManager manager({fake});
        std::string error;
        require(manager.start("fake-engine", error), (std::string("fake engine should start: ") + error).c_str());
        auto statuses = manager.statuses();
        require(statuses.size() == 1 && statuses[0].state == lar::ProcessState::running,
                "started child process should be reported as running");
        require(statuses[0].pid > 0, "running child should have a PID");
        require(manager.stop("fake-engine", error), (std::string("fake engine should stop: ") + error).c_str());
        statuses = manager.statuses();
        require(statuses.size() == 1 && statuses[0].state == lar::ProcessState::stopped,
                "stopped child should be reported as stopped");

        stage("local diagnostics and path redaction");
        const auto diagnostic_root = std::filesystem::temp_directory_path() / ("lar-diagnostics-" + suffix);
        lar::initialize_diagnostic_log(diagnostic_root, "test-version");
        lar::log_diagnostic("ERROR", "test.redaction", "path=" + diagnostic_root.string() + "/private-model.gguf");
        auto json_path = diagnostic_root.string();
#ifdef _WIN32
        std::string escaped_json_path;
        for (const char ch : json_path) { if (ch == '\\') escaped_json_path.push_back('\\'); escaped_json_path.push_back(ch); }
        json_path = std::move(escaped_json_path);
#endif
        const auto diagnostic_jobs = "{\"data\":[{\"installed_path\":\"" + json_path + "\\private-model.gguf\"}]}";
        const auto diagnostic_report = lar::build_diagnostic_report("test-version", diagnostic_jobs, "{\"data\":[]}");
        require(diagnostic_report.find("test.redaction") != std::string::npos, "diagnostic report should include recent local events");
        require(diagnostic_report.find("<LOCAL_DATA>/private-model.gguf") != std::string::npos, "diagnostics should redact the local data directory path");
        require(diagnostic_report.find(diagnostic_root.string()) == std::string::npos, "diagnostic report must not expose the raw user-data path");
        require(diagnostic_report.find("Remote telemetry: disabled") != std::string::npos, "diagnostics must state that data is not sent automatically");
        std::filesystem::remove_all(diagnostic_root);

        std::cout << "All core and process integration tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return 1;
    }
}
