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
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
void stage(const char* name) {
    std::cerr << "[core-tests] " << name << std::endl;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void set_test_data_root(const std::filesystem::path& root) {
#ifdef _WIN32
    if (!SetEnvironmentVariableW(L"LOCAL_AI_RUNTIME_DATA_DIR", root.c_str())) {
        throw std::runtime_error("Could not set LOCAL_AI_RUNTIME_DATA_DIR for the install queue test.");
    }
#else
    if (setenv("LOCAL_AI_RUNTIME_DATA_DIR", root.string().c_str(), 1) != 0) {
        throw std::runtime_error("Could not set LOCAL_AI_RUNTIME_DATA_DIR for the install queue test.");
    }
#endif
}
}

int main() {
    try {
        stage("engine id validation");
        require(lar::is_valid_engine_id("llama_cpp-1"), "valid engine id rejected");
        require(!lar::is_valid_engine_id("../llama"), "path traversal engine id accepted");
        require(!lar::is_valid_engine_id(""), "empty engine id accepted");

        stage("compiled reviewed component manifest");
        const auto& manifest_components = lar::catalog::components();
        require(manifest_components.size() == 4, "compiled catalog manifest should contain exactly four reviewed components");
        const auto* cpu_catalog_item = lar::catalog::find_component("llama.cpp-cpu");
        const auto* cuda12_catalog_item = lar::catalog::find_component("llama.cpp-cuda12");
        const auto* cuda13_catalog_item = lar::catalog::find_component("llama.cpp-cuda13");
        const auto* starter_model = lar::catalog::default_model_component();
        require(cpu_catalog_item && cpu_catalog_item->installer == "llama-release" && cpu_catalog_item->variant == "cpu",
                "CPU engine should resolve through the compiled manifest");
        require(cuda12_catalog_item && cuda12_catalog_item->installer == "llama-release" && cuda12_catalog_item->variant == "cuda12",
                "CUDA 12 engine should resolve through the compiled manifest");
        require(cuda13_catalog_item && cuda13_catalog_item->installer == "llama-release" && cuda13_catalog_item->variant == "cuda13",
                "CUDA 13 engine should resolve through the compiled manifest");
        require(cpu_catalog_item->dependencies.empty(), "CPU llama.cpp must not declare a CUDA runtime dependency");
        require(cuda12_catalog_item->dependencies.size() == 1 &&
                cuda12_catalog_item->dependencies.front().id == "llama.cpp-cudart-cuda12",
                "CUDA 12 must declare its matching runtime archive dependency");
        require(cuda13_catalog_item->dependencies.size() == 1 &&
                cuda13_catalog_item->dependencies.front().id == "llama.cpp-cudart-cuda13",
                "CUDA 13 must declare its matching runtime archive dependency");
        require(starter_model && starter_model->installer == "pinned-gguf" && starter_model->filename == "qwen2.5-1.5b-instruct-q4_k_m.gguf",
                "the default GGUF model should resolve from the manifest");
        require(lar::catalog::supports_target(*cpu_catalog_item, "windows", "x64"),
                "manifest should allow the reviewed Windows x64 CPU engine target");
        require(!lar::catalog::supports_target(*cuda12_catalog_item, "macos", "arm64"),
                "manifest should reject unsupported macOS CUDA targets");
        require(!lar::catalog::find_component("https://example.invalid/payload"),
                "arbitrary URLs must never resolve as installable catalog IDs");
        require(lar::catalog::is_known_engine_variant("cuda13") && !lar::catalog::is_known_engine_variant("unreviewed"),
                "engine release resolver must only accept manifest-defined variants");

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

        stage("persistent sequential install queue and duplicate protection");
        {
            const auto queue_root = std::filesystem::temp_directory_path() /
                ("lar-install-queue-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(queue_root);
            set_test_data_root(queue_root);
            lar::EngineManager queue_engines({});
            lar::RuntimeConfig queue_config;
            std::mutex queue_mutex;
            std::condition_variable queue_cv;
            std::vector<std::string> execution_order;
            std::atomic<int> active_workers{0};
            std::atomic<int> maximum_active_workers{0};
            bool first_started = false;
            bool release_first = false;
            auto executor = [&](const std::string& item_id, const std::function<bool()>& canceled) {
                const int current = active_workers.fetch_add(1) + 1;
                int observed = maximum_active_workers.load();
                while (current > observed && !maximum_active_workers.compare_exchange_weak(observed, current)) {}
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    execution_order.push_back(item_id);
                    if (item_id == "llama.cpp-cpu") first_started = true;
                }
                queue_cv.notify_all();
                if (item_id == "llama.cpp-cpu") {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    // Poll cancellation as well as waiting for the test to release the
                    // first job. DownloadManager shutdown sets the cancellation flag,
                    // but cannot notify this test-owned condition variable. A bounded
                    // wait keeps failure cleanup from hanging the core-test process.
                    while (!release_first && !canceled()) {
                        queue_cv.wait_for(lock, std::chrono::milliseconds(10));
                    }
                }
                active_workers.fetch_sub(1);
                if (canceled()) throw std::runtime_error("Download canceled by the user.");
            };
            {
                lar::DownloadManager queue_manager(queue_config, queue_engines, executor);
                std::string first_job, duplicate_job, second_job, requeued_job, queue_error;
                require(queue_manager.start("llama.cpp-cpu", true, first_job, queue_error),
                        (std::string("first catalog install should queue: ") + queue_error).c_str());
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    require(queue_cv.wait_for(lock, std::chrono::seconds(2), [&] { return first_started; }),
                            "the first queued install did not start");
                }
                stage("install queue first worker started");
                require(queue_manager.start("llama.cpp-cpu", true, duplicate_job, queue_error),
                        "a duplicate install request should reuse the queued/active job");
                require(duplicate_job == first_job, "duplicate item clicks created a second install job");
                stage("install queue duplicate request reused");
                require(queue_manager.start("qwen2.5-1.5b-q4km", true, second_job, queue_error),
                        (std::string("second catalog install should join the queue: ") + queue_error).c_str());
                require(second_job != first_job, "a distinct component must receive its own queue job");
                const auto second_snapshot = queue_manager.job_json(second_job);
                require(second_snapshot && second_snapshot->find("\"state\":\"queued\"") != std::string::npos &&
                        second_snapshot->find("\"queue_position\":1") != std::string::npos,
                        "the second install should remain queued at position one while the first is active");
                // Do not use a CUDA-only component in this generic queue test:
                // macOS has no CUDA catalog targets. Cancel the queued model and
                // enqueue it again to verify cancellation/retry semantics on every OS.
                require(queue_manager.cancel(second_job, queue_error),
                        "a not-yet-started install should be cancelable immediately");
                const auto canceled_snapshot = queue_manager.job_json(second_job);
                require(canceled_snapshot && canceled_snapshot->find("\"state\":\"canceled\"") != std::string::npos,
                        "canceling a queued install should mark it terminal without interrupting the active install");
                require(queue_manager.start("qwen2.5-1.5b-q4km", true, requeued_job, queue_error),
                        (std::string("a canceled catalog item should be queueable again: ") + queue_error).c_str());
                require(requeued_job != second_job && requeued_job != first_job,
                        "re-queuing a canceled item should create a new job attempt");
                const auto requeued_snapshot = queue_manager.job_json(requeued_job);
                require(requeued_snapshot && requeued_snapshot->find("\"state\":\"queued\"") != std::string::npos &&
                        requeued_snapshot->find("\"queue_position\":1") != std::string::npos,
                        "the re-queued model should return to queue position one");
                stage("install queue canceled item and re-queued replacement");
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    release_first = true;
                }
                queue_cv.notify_all();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                bool completed = false;
                while (std::chrono::steady_clock::now() < deadline) {
                    const auto snapshot = queue_manager.job_json(requeued_job);
                    if (snapshot && snapshot->find("\"state\":\"succeeded\"") != std::string::npos) {
                        completed = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                require(completed, "the re-queued install did not run after the first completed");
                stage("install queue FIFO worker completed");
                const auto original_canceled_snapshot = queue_manager.job_json(second_job);
                require(original_canceled_snapshot &&
                        original_canceled_snapshot->find("\"state\":\"canceled\"") != std::string::npos,
                        "the canceled attempt must not be executed after re-queueing");
                require(maximum_active_workers.load() == 1, "more than one install worker ran concurrently");
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    require(execution_order.size() == 2 && execution_order[0] == "llama.cpp-cpu" &&
                            execution_order[1] == "qwen2.5-1.5b-q4km",
                            "the install queue did not preserve FIFO order or ran a canceled queued item");
                }
            }
            std::error_code ignored;
            std::filesystem::remove_all(queue_root, ignored);
        }

        stage("queued installs survive runtime restart; active install is not replayed");
        {
            const auto queue_root = std::filesystem::temp_directory_path() /
                ("lar-install-queue-restart-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(queue_root);
            set_test_data_root(queue_root);
            lar::EngineManager queue_engines({});
            lar::RuntimeConfig queue_config;
            std::mutex queue_mutex;
            std::condition_variable queue_cv;
            bool active_started = false;
            std::string active_job_id;
            std::string pending_job_id;
            auto blocking_executor = [&](const std::string&, const std::function<bool()>& canceled) {
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    active_started = true;
                }
                queue_cv.notify_all();
                while (!canceled()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                throw std::runtime_error("Download canceled by the user.");
            };
            {
                lar::DownloadManager first_runtime(queue_config, queue_engines, blocking_executor);
                std::string queue_error;
                require(first_runtime.start("llama.cpp-cpu", true, active_job_id, queue_error),
                        "active install should start before simulated restart");
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    require(queue_cv.wait_for(lock, std::chrono::seconds(2), [&] { return active_started; }),
                            "active install did not enter its worker before simulated restart");
                }
                require(first_runtime.start("qwen2.5-1.5b-q4km", true, pending_job_id, queue_error),
                        "the second install should persist as pending before simulated restart");
            }
            const auto pending_record = queue_root / "jobs" / (pending_job_id + ".json");
            require(std::filesystem::is_regular_file(pending_record), "queued install record was not persisted");
            {
                std::ifstream input(pending_record, std::ios::binary);
                const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                require(content.find("\"state\":\"queued\"") != std::string::npos,
                        "shutdown should preserve not-yet-started jobs in queued state");
            }
            std::atomic<int> resumed_count{0};
            auto resumed_executor = [&](const std::string& item_id, const std::function<bool()>& canceled) {
                require(item_id == "qwen2.5-1.5b-q4km", "runtime restart replayed the active install instead of the pending queue item");
                if (canceled()) throw std::runtime_error("Download canceled by the user.");
                ++resumed_count;
            };
            {
                lar::DownloadManager restored_runtime(queue_config, queue_engines, resumed_executor);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                bool resumed = false;
                while (std::chrono::steady_clock::now() < deadline) {
                    const auto snapshot = restored_runtime.job_json(pending_job_id);
                    if (snapshot && snapshot->find("\"state\":\"succeeded\"") != std::string::npos) {
                        resumed = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                require(resumed && resumed_count.load() == 1,
                        "the explicitly queued install did not resume after runtime restart");
                const auto interrupted = restored_runtime.job_json(active_job_id);
                require(interrupted && interrupted->find("\"state\":\"canceled\"") != std::string::npos,
                        "the in-flight install should stop safely and must not be replayed after restart");
            }
            std::error_code ignored;
            std::filesystem::remove_all(queue_root, ignored);
        }


        stage("retry a failed install behind an active queued install");
        {
            const auto queue_root = std::filesystem::temp_directory_path() /
                ("lar-install-queue-retry-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(queue_root);
            set_test_data_root(queue_root);
            lar::EngineManager queue_engines({});
            lar::RuntimeConfig queue_config;
            std::mutex queue_mutex;
            std::condition_variable queue_cv;
            std::atomic<int> model_attempts{0};
            std::vector<std::string> execution_order;
            bool engine_started = false;
            bool release_engine = false;
            auto retry_executor = [&](const std::string& item_id, const std::function<bool()>& canceled) {
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    execution_order.push_back(item_id);
                    if (item_id == "llama.cpp-cpu") engine_started = true;
                }
                queue_cv.notify_all();
                if (item_id == "qwen2.5-1.5b-q4km" && model_attempts.fetch_add(1) == 0) {
                    throw std::runtime_error("Simulated transient model download failure.");
                }
                if (item_id == "llama.cpp-cpu") {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    // Keep teardown bounded if a preceding assertion fails before
                    // the test releases this executor. DownloadManager cancellation
                    // does not notify this test-owned condition variable.
                    while (!release_engine && !canceled()) {
                        queue_cv.wait_for(lock, std::chrono::milliseconds(10));
                    }
                }
                if (canceled()) throw std::runtime_error("Download canceled by the user.");
            };
            {
                lar::DownloadManager queue_manager(queue_config, queue_engines, retry_executor);
                std::string model_job, engine_job, queue_error;
                require(queue_manager.start("qwen2.5-1.5b-q4km", true, model_job, queue_error),
                        "the initial model install should enter the queue");
                const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                bool failed = false;
                while (std::chrono::steady_clock::now() < failure_deadline) {
                    const auto snapshot = queue_manager.job_json(model_job);
                    if (snapshot && snapshot->find("\"state\":\"failed\"") != std::string::npos) {
                        failed = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                require(failed, "the simulated first model attempt did not fail");
                require(queue_manager.start("llama.cpp-cpu", true, engine_job, queue_error),
                        "a different engine install should start after the model failure");
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    require(queue_cv.wait_for(lock, std::chrono::seconds(2), [&] { return engine_started; }),
                            "the engine install did not become active before model retry");
                }
                require(queue_manager.retry(model_job, queue_error),
                        (std::string("a failed model retry should queue behind the active engine install: ") + queue_error).c_str());
                const auto retry_snapshot = queue_manager.job_json(model_job);
                require(retry_snapshot && retry_snapshot->find("\"state\":\"queued\"") != std::string::npos &&
                        retry_snapshot->find("\"attempt\":2") != std::string::npos &&
                        retry_snapshot->find("\"queue_position\":1") != std::string::npos,
                        "retry should increment the attempt and wait behind the active install");
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    release_engine = true;
                }
                queue_cv.notify_all();
                const auto success_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                bool succeeded = false;
                while (std::chrono::steady_clock::now() < success_deadline) {
                    const auto snapshot = queue_manager.job_json(model_job);
                    if (snapshot && snapshot->find("\"state\":\"succeeded\"") != std::string::npos) {
                        succeeded = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                require(succeeded && model_attempts.load() == 2,
                        "the queued retry did not execute and succeed after the active install finished");
                stage("install queue failed attempt successfully retried");
                std::lock_guard<std::mutex> lock(queue_mutex);
                require(execution_order.size() == 3 && execution_order[0] == "qwen2.5-1.5b-q4km" &&
                        execution_order[1] == "llama.cpp-cpu" && execution_order[2] == "qwen2.5-1.5b-q4km",
                        "the failed job retry did not preserve FIFO queue order");
            }
            std::error_code ignored;
            std::filesystem::remove_all(queue_root, ignored);
        }

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
        roundtrip.runtime_profile.engine_component_id = "llama.cpp-cuda12";
        roundtrip.runtime_profile.model_component_id = "qwen2.5-1.5b-q4km";
        roundtrip.runtime_profile.context_size = 8192;
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
        require(loaded_roundtrip.runtime_profile.engine_component_id == "llama.cpp-cuda12",
                "runtime profile engine component ID with dots did not round-trip");
        require(loaded_roundtrip.runtime_profile.model_component_id == "qwen2.5-1.5b-q4km",
                "runtime profile model component ID with dots did not round-trip");
        require(loaded_roundtrip.runtime_profile.context_size == 8192,
                "runtime profile context size did not round-trip");
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
