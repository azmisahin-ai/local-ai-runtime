#include "lar/config.hpp"
#include "lar/catalog.hpp"
#include "lar/diagnostics.hpp"
#include "lar/hardware.hpp"
#include "lar/downloads.hpp"
#include "lar/activity_jobs.hpp"
#include "lar/process.hpp"
#include "lar/backend_evidence.hpp"
#include "lar/gpu_monitor.hpp"
#include "lar/resource_monitor.hpp"

#include <algorithm>
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

        stage("runtime update version ordering");
        require(lar::DownloadManager::is_newer_release_tag("v0.1.1-alpha", "0.1.0-alpha"),
                "a newer patch release should be allowed for update");
        require(lar::DownloadManager::is_newer_release_tag("v0.1.0-alpha", "0.1.0-dev"),
                "alpha release should sort after development preview");
        require(!lar::DownloadManager::is_newer_release_tag("v0.1.0-alpha", "0.1.0-alpha"),
                "the same release must not be reapplied as an update");
        require(!lar::DownloadManager::is_newer_release_tag("v0.0.9-alpha", "0.1.0-alpha"),
                "older staged release must not be applied");
        require(!lar::DownloadManager::is_newer_release_tag("latest", "0.1.0-alpha"),
                "malformed release tag must be rejected");

        stage("official CUDA catalog asset pairing");
        require(lar::catalog::is_supported_engine_archive(
                    "llama-b11982-bin-win-cuda-12.4-x64.zip", "cuda12", "windows", "x64"),
                "the official Windows CUDA 12 llama-server archive should match");
        require(!lar::catalog::is_supported_engine_archive(
                    "cudart-llama-bin-win-cuda-12.4-x64.zip", "cuda12", "windows", "x64"),
                "the CUDA dependency DLL archive must not be mistaken for the server binary");
        require(lar::catalog::is_supported_cuda_runtime_archive(
                    "cudart-llama-bin-win-cuda-12.4-x64.zip", "cuda12", "windows", "x64"),
                "the official Windows CUDA 12 runtime DLL archive should match as a dependency");
        require(!lar::catalog::is_supported_cuda_runtime_archive(
                    "cudart-llama-bin-win-cuda-13.4-x64.zip", "cuda12", "windows", "x64"),
                "CUDA 13 runtime DLLs must not be paired with a CUDA 12 server");
        require(lar::catalog::is_supported_engine_archive(
                    "llama-b11982-bin-win-cuda-13.4-x64.zip", "cuda13", "windows", "x64"),
                "the official Windows CUDA 13 llama-server archive should match");
        require(lar::catalog::is_supported_cuda_runtime_archive(
                    "cudart-llama-bin-win-cuda-13.4-x64.zip", "cuda13", "windows", "x64"),
                "the official Windows CUDA 13 runtime DLL archive should match");
        require(lar::catalog::is_supported_engine_archive(
                    "llama-b11962-bin-ubuntu-cuda-12.8-x64.tar.gz", "cuda12", "linux", "x64"),
                "the official Linux CUDA 12 server archive should match");
        require(lar::catalog::is_supported_cuda_runtime_archive(
                    "cudart-llama-b11962-bin-ubuntu-cuda-12.8-x64.tar.gz", "cuda12", "linux", "x64"),
                "the matching Linux CUDA 12 runtime archive should match");
        require(lar::catalog::is_supported_engine_archive(
                    "llama-b11962-bin-ubuntu-cuda-13.4-x64.tar.gz", "cuda13", "linux", "x64"),
                "the official Linux CUDA 13 server archive should match");
        require(lar::catalog::is_supported_cuda_runtime_archive(
                    "cudart-llama-b11962-bin-ubuntu-cuda-13.4-x64.tar.gz", "cuda13", "linux", "x64"),
                "the matching Linux CUDA 13 runtime archive should match");
        require(!lar::catalog::is_supported_engine_archive(
                    "llama-b11982-bin-win-cuda-12.4-x64.zip", "cuda12", "windows", "arm64"),
                "an x64 CUDA package must not be offered to ARM64");

        stage("live resource snapshot contract");
        const auto resource_snapshot = lar::collect_resource_snapshot({});
        require(!resource_snapshot.platform.empty(), "resource snapshot did not report its platform");
        require(resource_snapshot.logical_cpu_count >= 1, "resource snapshot reported an invalid CPU count");
        require(resource_snapshot.total_memory_bytes >= resource_snapshot.available_memory_bytes,
                "resource snapshot available memory exceeded total memory");
        require(resource_snapshot.memory_pressure == "normal" || resource_snapshot.memory_pressure == "constrained" ||
                resource_snapshot.memory_pressure == "critical" || resource_snapshot.memory_pressure == "unknown",
                "resource snapshot reported an unknown memory pressure state");
        const auto resource_json = lar::resource_snapshot_json(resource_snapshot);
        require(resource_json.find("\"schema_version\":1") != std::string::npos,
                "resource snapshot JSON omitted its schema version");
        require(resource_json.find("\"gpu_telemetry\"") != std::string::npos,
                "resource snapshot JSON omitted GPU telemetry status");

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

        stage("session-scoped GPU execution evidence");
        const std::string gpu_marker = std::string(lar::engine_log_session_marker) + "\n";
        const auto offloaded = lar::parse_gpu_execution_evidence(
            "old run: offloaded 33/33 layers to GPU\n" + gpu_marker +
            "ggml_cuda_init: found 1 CUDA devices\nllm_load_tensors: offloaded 18/33 layers to GPU\n");
        require(offloaded.state == "offload-reported", "positive GPU layer offload should be reported");
        require(offloaded.offload_count_found && offloaded.offloaded_layers == 18 && offloaded.total_layers == 33,
                "GPU evidence should report the offloaded/total layer counts");
        require(offloaded.cuda_runtime_reported, "CUDA initialization evidence should be retained");
        require(offloaded.summary.find("live GPU utilization") != std::string::npos,
                "GPU evidence must clarify that live utilization was not measured");

        const auto zero_offload = lar::parse_gpu_execution_evidence(
            gpu_marker + "llm_load_tensors: offloaded 0/33 layers to GPU\n");
        require(zero_offload.state == "no-layers-offloaded" && zero_offload.offloaded_layers == 0,
                "zero offloaded layers must not be reported as GPU inference");

        const auto cuda_only = lar::parse_gpu_execution_evidence(
            gpu_marker + "ggml_cuda_init: found 1 CUDA devices\n");
        require(cuda_only.state == "cuda-runtime-reported" && !cuda_only.offload_count_found,
                "CUDA initialization alone must not imply layer offload");

        const auto stale_only = lar::parse_gpu_execution_evidence(
            "old session: offloaded 33/33 layers to GPU\n" + gpu_marker + "starting new CPU session\n");
        require(stale_only.state == "not-verified" && !stale_only.offload_count_found,
                "previous-session GPU evidence must not leak into current status");

        const auto no_log = lar::parse_gpu_execution_evidence({}, false);
        require(!no_log.engine_log_available && no_log.state == "not-verified",
                "missing logs must fail closed to not-verified");

        stage("NVIDIA utilization snapshot parser");
        std::vector<lar::NvidiaGpuSample> nvidia_samples;
        require(lar::parse_nvidia_smi_snapshot("0, 37, 2048, 8192\n1, N/A, 512, 4096\n", nvidia_samples),
                "valid nvidia-smi CSV output should parse");
        require(nvidia_samples.size() == 2 && nvidia_samples[0].index == 0 &&
                nvidia_samples[0].utilization_percent == 37 && nvidia_samples[0].memory_used_mib == 2048 &&
                nvidia_samples[0].memory_total_mib == 8192,
                "NVIDIA usage and memory fields should be parsed correctly");
        require(nvidia_samples[1].index == 1 && nvidia_samples[1].utilization_percent == -1 &&
                nvidia_samples[1].memory_used_mib == 512,
                "unsupported metrics should remain unknown instead of being guessed");
        require(!lar::parse_nvidia_smi_snapshot("nvidia-smi failed\n", nvidia_samples),
                "non-CSV diagnostic output must not be accepted as GPU telemetry");
        require(!lar::parse_nvidia_smi_snapshot("0, N/A, N/A, N/A\n", nvidia_samples),
                "a CSV row with no usable metrics must not count as a measurement");

        stage("local hardware inventory");
        const auto hardware = lar::detect_hardware(std::filesystem::temp_directory_path());
        require(!hardware.platform.empty(), "hardware probe must report the platform");
        require(!hardware.architecture.empty(), "hardware probe must report the architecture");
        require(hardware.logical_cpu_count >= 1, "hardware probe must report at least one logical CPU");
        require(hardware.gpu_probe_status == "complete" || hardware.gpu_probe_status == "partial" ||
                hardware.gpu_probe_status == "unsupported", "hardware probe returned an unknown GPU evidence state");
        for (const auto& gpu : hardware.gpus) {
            require(!gpu.vendor.empty(), "detected graphics adapters must include a vendor label");
            require(!gpu.vendor_id.empty(), "detected graphics adapters must include a vendor identifier");
        }

        stage("example config loading");
        const auto config = lar::load_config(LAR_TEST_CONFIG);
        require(config.host == "127.0.0.1", "default host must bind to loopback");
        require(config.port == 8000, "example port should be 8000");
        require(config.engines.size() == 1, "example must provide one llama.cpp adapter example");
        require(config.engines.front().id == "llama-cpp", "unexpected example engine id");
        require(std::find(config.engines.front().args.begin(), config.engines.front().args.end(), "--cors-origins") != config.engines.front().args.end(),
                "example llama.cpp args should restrict CORS to localhost");
        require(std::find(config.engines.front().args.begin(), config.engines.front().args.end(), "--no-cors-credentials") != config.engines.front().args.end(),
                "example llama.cpp args should disable credentialed CORS");

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

        stage("managed llama.cpp CORS migration");
        auto legacy_config = config;
        auto& legacy_llama = legacy_config.engines.front();
        legacy_llama.executable_path = std::filesystem::temp_directory_path() /
            "LocalAIRuntime" / "engines" / "llama.cpp" / "b11969-cpu" /
            ("llama-server" + std::string(
#ifdef _WIN32
                ".exe"
#else
                ""
#endif
            ));
        legacy_llama.args = {"--host", "127.0.0.1", "--port", "8081", "--cors-origins", "*",
                             "--cors-credentials", "-m", "{model_path}"};
        const auto legacy_path = std::filesystem::temp_directory_path() / ("lar-legacy-config-" + suffix + ".json");
        lar::save_config(legacy_path, legacy_config);
        const auto migrated = lar::load_config(legacy_path);
        const auto& migrated_args = migrated.engines.front().args;
        const auto cors_flag = std::find(migrated_args.begin(), migrated_args.end(), "--cors-origins");
        require(cors_flag != migrated_args.end() && std::next(cors_flag) != migrated_args.end() &&
                *std::next(cors_flag) == "localhost", "managed llama.cpp legacy config should restrict CORS to localhost");
        require(std::find(migrated_args.begin(), migrated_args.end(), "--cors-credentials") == migrated_args.end(),
                "managed llama.cpp legacy config should remove credentialed CORS");
        require(std::find(migrated_args.begin(), migrated_args.end(), "--no-cors-credentials") != migrated_args.end(),
                "managed llama.cpp legacy config should disable credentialed CORS");
        std::filesystem::remove(legacy_path);
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

        stage("durable runtime operation lifecycle and restart recovery");
        {
            const auto operation_root = std::filesystem::temp_directory_path() /
                ("lar-operation-jobs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::string interrupted_id;
            std::string succeeded_id;
            {
                lar::ActivityJobManager operation_jobs(operation_root);
                interrupted_id = operation_jobs.begin("inference", "fake-engine", "Running local inference");
                require(!interrupted_id.empty(), "runtime operation should be persisted before execution");
                operation_jobs.report_progress(interrupted_id, "Streaming model output", 49152);
                succeeded_id = operation_jobs.begin("engine-start", "fake-engine", "Starting engine");
                require(!succeeded_id.empty(), "engine start operation should be persisted");
                operation_jobs.succeed(succeeded_id, "Engine process started");
                const auto active_record = operation_jobs.job_json(interrupted_id);
                require(active_record && active_record->find("\"state\":\"running\"") != std::string::npos,
                        "an in-flight operation should be visible as running");
            }
            {
                lar::ActivityJobManager restored_jobs(operation_root);
                const auto interrupted = restored_jobs.job_json(interrupted_id);
                const auto succeeded = restored_jobs.job_json(succeeded_id);
                require(interrupted && interrupted->find("\"state\":\"interrupted\"") != std::string::npos,
                        "an operation left running at shutdown must be marked interrupted");
                require(interrupted->find("\"error_code\":\"process_interrupted\"") != std::string::npos,
                        "interrupted runtime operations must expose a stable error code");
                require(interrupted->find("\"retryable\":false") != std::string::npos,
                        "engine and inference operations must not be replayed automatically");
                require(interrupted->find("\"progress_bytes\":49152") != std::string::npos,
                        "the last persisted inference output-byte progress must survive restart recovery");
                require(succeeded && succeeded->find("\"state\":\"succeeded\"") != std::string::npos,
                        "terminal operation history must survive runtime restart");
                require(succeeded->find("\"duration_ms\":") != std::string::npos,
                        "terminal operation history must record elapsed duration");
            }
            std::error_code ignored;
            std::filesystem::remove_all(operation_root, ignored);
        }

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
