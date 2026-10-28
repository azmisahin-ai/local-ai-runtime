#include "lar/config.hpp"
#include "lar/process.hpp"
#include "lar/server.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef LAR_VERSION
#define LAR_VERSION "0.1.0"
#endif
#ifndef LAR_DEFAULT_CONFIG
#define LAR_DEFAULT_CONFIG "config/engines.json"
#endif
#ifndef LAR_EXAMPLE_CONFIG
#define LAR_EXAMPLE_CONFIG "config/engines.example.json"
#endif
#ifndef LAR_DEFAULT_WEB_DIR
#define LAR_DEFAULT_WEB_DIR "web"
#endif

int main(int argc, char** argv) {
    try {
        std::filesystem::path config_path = LAR_DEFAULT_CONFIG;
        std::filesystem::path web_dir = LAR_DEFAULT_WEB_DIR;
        bool print_version = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
            else if (arg == "--web-dir" && i + 1 < argc) web_dir = argv[++i];
            else if (arg == "--version") print_version = true;
            else if (arg == "--help" || arg == "-h") {
                std::cout << "Local AI Runtime " << LAR_VERSION << "\n"
                          << "Usage: local-ai-runtime [--config PATH] [--web-dir PATH] [--version]\n\n"
                          << "Defaults to config/engines.json, falling back to config/engines.example.json.\n"
                          << "The API binds to loopback only in this release.\n";
                return 0;
            } else {
                std::cerr << "Unknown argument: " << arg << "\nUse --help for usage.\n";
                return 2;
            }
        }
        if (print_version) {
            std::cout << LAR_VERSION << '\n';
            return 0;
        }
        if (!std::filesystem::exists(config_path)) {
            config_path = LAR_EXAMPLE_CONFIG;
            std::cerr << "No user config found; using example config: " << config_path.string() << "\n"
                      << "Copy it to config/engines.json and edit your local paths to configure a real engine.\n";
        }
        const auto config = lar::load_config(config_path);
        lar::EngineManager engines(config.engines);
        std::cout << "Local AI Runtime " << LAR_VERSION << " listening on http://127.0.0.1:" << config.port << "\n"
                  << "Configured engines: " << config.engines.size() << "\n"
                  << "Press Ctrl+C to stop.\n";
        lar::run_http_server(config, engines, web_dir);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << '\n';
        return 1;
    }
}
