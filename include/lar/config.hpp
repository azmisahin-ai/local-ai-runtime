#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lar {

struct EngineConfig {
    std::string id;
    // Adapter implementation responsible for lifecycle and protocol behavior.
    // Empty values from legacy configs are migrated from the engine ID.
    std::string adapter_id;
    std::string display_name;
    std::filesystem::path executable_path;
    std::filesystem::path model_path;
    std::filesystem::path working_directory;
    std::vector<std::string> args;
    std::string upstream_base_url;
    std::vector<std::string> capabilities;
    bool default_for_chat{false};
};

struct RuntimeProfileConfig {
    std::string engine_component_id;
    std::string model_component_id;
    unsigned int context_size{2048};
};

struct RuntimeConfig {
    std::string host{"127.0.0.1"};
    unsigned short port{8000};
    std::vector<EngineConfig> engines;
    // Absolute path to the user-owned configuration file (not serialized).
    std::filesystem::path source_path;
    RuntimeProfileConfig runtime_profile;
};

RuntimeConfig load_config(const std::filesystem::path& path);
void save_config(const std::filesystem::path& path, const RuntimeConfig& config);
std::vector<std::string> expand_arguments(const EngineConfig& engine);
bool is_valid_engine_id(const std::string& id);
bool is_valid_engine_adapter_id(const std::string& id);
std::string default_engine_adapter_id(const std::string& engine_id);

} // namespace lar
