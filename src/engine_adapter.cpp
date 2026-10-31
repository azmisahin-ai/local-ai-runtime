#include "lar/engine_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <utility>

namespace lar {
namespace {

std::string effective_adapter_id(const EngineConfig& config) {
    if (!config.adapter_id.empty()) return config.adapter_id;
    return default_engine_adapter_id(config.id);
}

std::string lowercase_filename(const std::filesystem::path& path) {
    auto value = path.filename().string();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

class OpenAiCompatibleProcessAdapter : public EngineAdapter {
public:
    std::string_view id() const noexcept override { return "openai-compatible-process"; }
    std::string_view display_name() const noexcept override { return "OpenAI-compatible local process"; }

    bool validate_config(const EngineConfig&, std::string& error) const override {
        error.clear();
        return true;
    }

    std::string_view readiness_path() const noexcept override { return "/v1/models"; }
    std::string_view readiness_fallback_path() const noexcept override { return {}; }
    std::string_view chat_completions_path() const noexcept override { return "/v1/chat/completions"; }
};

class LlamaCppAdapter final : public OpenAiCompatibleProcessAdapter {
public:
    std::string_view id() const noexcept override { return "llama.cpp"; }
    std::string_view display_name() const noexcept override { return "llama.cpp"; }

    bool validate_config(const EngineConfig& config, std::string& error) const override {
        if (config.id != "llama-cpp") {
            error = "The llama.cpp adapter requires engine id 'llama-cpp'.";
            return false;
        }
        if (!config.executable_path.empty()) {
            const auto filename = lowercase_filename(config.executable_path);
#ifdef _WIN32
            if (filename != "llama-server.exe" && filename != "llama-server") {
#else
            if (filename != "llama-server") {
#endif
                error = "The llama.cpp adapter only starts the official llama-server executable.";
                return false;
            }
        }
        if (!config.model_path.empty()) {
            auto extension = config.model_path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (extension != ".gguf") {
                error = "The llama.cpp adapter requires a GGUF model file.";
                return false;
            }
            std::error_code file_error;
            if (std::filesystem::exists(config.model_path, file_error) && !file_error) {
                if (!std::filesystem::is_regular_file(config.model_path, file_error) || file_error ||
                    std::filesystem::file_size(config.model_path, file_error) < 4U || file_error) {
                    error = "The configured GGUF model is not a readable regular file with a complete header.";
                    return false;
                }
                std::ifstream input(config.model_path, std::ios::binary);
                char magic[4]{};
                input.read(magic, sizeof(magic));
                if (input.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
                    std::string_view(magic, sizeof(magic)) != "GGUF") {
                    error = "The configured model does not contain the GGUF file signature.";
                    return false;
                }
            }
            if (file_error) {
                error = "Could not inspect the configured GGUF model file.";
                return false;
            }
        }
        error.clear();
        return true;
    }

    std::string_view readiness_path() const noexcept override { return "/health"; }
    std::string_view readiness_fallback_path() const noexcept override { return "/v1/models"; }
};

} // namespace

bool EngineAdapter::start(ManagedProcess& process, const EngineConfig& config, std::string& error) const {
    if (!validate_config(config, error)) return false;
    return process.start(config, error);
}

bool EngineAdapter::stop(ManagedProcess& process, std::string& error) const {
    return process.stop(error);
}

ProcessState EngineAdapter::state(ManagedProcess& process) const {
    return process.state();
}

std::int64_t EngineAdapter::pid(const ManagedProcess& process) const {
    return process.pid();
}

int EngineAdapter::exit_code(const ManagedProcess& process) const {
    return process.exit_code();
}

std::shared_ptr<EngineAdapter> create_engine_adapter(const EngineConfig& config, std::string& error) {
    const auto adapter_id = effective_adapter_id(config);
    std::shared_ptr<EngineAdapter> adapter;
    if (adapter_id == "llama.cpp") {
        adapter = std::make_shared<LlamaCppAdapter>();
    } else if (adapter_id == "openai-compatible-process") {
        adapter = std::make_shared<OpenAiCompatibleProcessAdapter>();
    } else {
        error = "Unknown engine adapter '" + adapter_id + "'. Supported adapters: llama.cpp, openai-compatible-process.";
        return {};
    }
    if (!adapter->validate_config(config, error)) return {};
    error.clear();
    return adapter;
}

} // namespace lar
