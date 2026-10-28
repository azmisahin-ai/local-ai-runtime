#pragma once

#include "lar/config.hpp"
#include "lar/process.hpp"

#include <filesystem>

namespace lar {
void run_http_server(const RuntimeConfig& config, EngineManager& engines,
                     const std::filesystem::path& web_dir);
}
