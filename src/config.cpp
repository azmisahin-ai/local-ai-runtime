#include "lar/config.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace lar {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool is_valid_component_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (const unsigned char ch : id) {
        if (!(std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.')) return false;
    }
    return id.front() != '.' && id.back() != '.';
}

std::vector<std::string> read_string_array(const boost::property_tree::ptree& parent,
                                           const std::string& key) {
    std::vector<std::string> values;
    const auto child = parent.get_child_optional(key);
    if (!child) return values;
    for (const auto& item : *child) values.push_back(item.second.get_value<std::string>());
    return values;
}

std::string json_quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20U) out << "\\u00" << hex[(ch >> 4U) & 0x0fU] << hex[ch & 0x0fU];
                else out << static_cast<char>(ch);
        }
    }
    out << '"';
    return out.str();
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(encoded.begin(), encoded.end());
}

std::filesystem::path path_from_utf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char ch : value) encoded.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(encoded);
}

void restrict_managed_llama_cors(EngineConfig& engine) {
    if (engine.id != "llama-cpp" || engine.executable_path.empty()) return;
    auto normalized_path = engine.executable_path.lexically_normal().generic_string();
    std::transform(normalized_path.begin(), normalized_path.end(), normalized_path.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    // Upgrade already-installed first-party managed engines without rewriting
    // user config files. A manually configured llama-server outside this path
    // keeps its explicitly supplied arguments.
    if (normalized_path.find("/engines/llama.cpp/") == std::string::npos) return;

    std::vector<std::string> safe_args;
    safe_args.reserve(engine.args.size() + 3);
    for (std::size_t i = 0; i < engine.args.size(); ++i) {
        const auto& arg = engine.args[i];
        if (arg == "--cors-origins") {
            if (i + 1 < engine.args.size()) ++i;
            continue;
        }
        if (arg == "--cors-credentials" || arg == "--no-cors-credentials") continue;
        safe_args.push_back(arg);
    }
    safe_args.push_back("--cors-origins");
    safe_args.push_back("localhost");
    safe_args.push_back("--no-cors-credentials");
    engine.args = std::move(safe_args);
}

} // namespace

bool is_valid_engine_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const unsigned char ch : id) {
        if (!(std::isalnum(ch) != 0 || ch == '-' || ch == '_')) return false;
    }
    return true;
}

RuntimeConfig load_config(const std::filesystem::path& path) {
    boost::property_tree::ptree root;
    std::ifstream config_input(path, std::ios::binary);
    if (!config_input) throw std::runtime_error("Cannot open configuration file: " + path.string());
    boost::property_tree::read_json(config_input, root);

    RuntimeConfig config;
    std::error_code absolute_error;
    config.source_path = std::filesystem::absolute(path, absolute_error);
    if (absolute_error) config.source_path = path;
    config.host = trim(root.get<std::string>("server.host", "127.0.0.1"));
    if (config.host != "127.0.0.1" && config.host != "localhost") {
        throw std::runtime_error("Only loopback bind addresses are allowed in v0.1 (127.0.0.1 or localhost).");
    }
    const auto port = root.get<unsigned int>("server.port", 8000U);
    if (port == 0U || port > 65535U) throw std::runtime_error("server.port must be between 1 and 65535.");
    config.port = static_cast<unsigned short>(port);

    if (const auto profile = root.get_child_optional("runtime_profile")) {
        config.runtime_profile.engine_component_id = trim(profile->get<std::string>("engine_component_id", ""));
        config.runtime_profile.model_component_id = trim(profile->get<std::string>("model_component_id", ""));
        config.runtime_profile.context_size = profile->get<unsigned int>("context_size", 2048U);
        if (config.runtime_profile.context_size < 512U || config.runtime_profile.context_size > 32768U) {
            throw std::runtime_error("runtime_profile.context_size must be between 512 and 32768 tokens.");
        }
        if ((!config.runtime_profile.engine_component_id.empty() && !is_valid_component_id(config.runtime_profile.engine_component_id)) ||
            (!config.runtime_profile.model_component_id.empty() && !is_valid_component_id(config.runtime_profile.model_component_id))) {
            throw std::runtime_error("runtime_profile component IDs contain unsupported characters.");
        }
    }

    const auto engines = root.get_child_optional("engines");
    if (!engines) throw std::runtime_error("Config is missing the 'engines' array.");

    std::set<std::string> ids;
    for (const auto& entry : *engines) {
        const auto& node = entry.second;
        EngineConfig engine;
        engine.id = trim(node.get<std::string>("id", ""));
        engine.display_name = trim(node.get<std::string>("display_name", engine.id));
        engine.executable_path = path_from_utf8(node.get<std::string>("executable_path", ""));
        engine.model_path = path_from_utf8(node.get<std::string>("model_path", ""));
        engine.working_directory = path_from_utf8(node.get<std::string>("working_directory", ""));
        engine.args = read_string_array(node, "args");
        restrict_managed_llama_cors(engine);
        engine.upstream_base_url = trim(node.get<std::string>("upstream_base_url", ""));
        engine.capabilities = read_string_array(node, "capabilities");
        engine.default_for_chat = node.get<bool>("default_for_chat", false);

        if (!is_valid_engine_id(engine.id)) {
            throw std::runtime_error("Invalid engine id '" + engine.id + "'. Use letters, digits, '-' and '_' only.");
        }
        if (!ids.insert(engine.id).second) throw std::runtime_error("Duplicate engine id: " + engine.id);
        if (engine.display_name.empty()) throw std::runtime_error("Engine '" + engine.id + "' requires display_name.");
        if (!engine.upstream_base_url.empty()) {
            // v0.1 intentionally proxies only to a local engine. No arbitrary URL forwarding.
            if (engine.upstream_base_url.rfind("http://127.0.0.1:", 0) != 0 &&
                engine.upstream_base_url.rfind("http://localhost:", 0) != 0) {
                throw std::runtime_error("Engine '" + engine.id + "': upstream_base_url must use http://127.0.0.1:PORT or http://localhost:PORT.");
            }
            const auto colon = engine.upstream_base_url.find_last_of(':');
            if (colon == std::string::npos || colon + 1 >= engine.upstream_base_url.size()) {
                throw std::runtime_error("Engine '" + engine.id + "': invalid upstream_base_url port.");
            }
            const auto port_text = engine.upstream_base_url.substr(colon + 1);
            if (!std::all_of(port_text.begin(), port_text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                throw std::runtime_error("Engine '" + engine.id + "': invalid upstream_base_url port.");
            }
            const auto upstream_port = std::stoul(port_text);
            if (upstream_port == 0 || upstream_port > 65535) {
                throw std::runtime_error("Engine '" + engine.id + "': upstream port must be between 1 and 65535.");
            }
        }
        config.engines.push_back(std::move(engine));
    }

    const auto defaults = std::count_if(config.engines.begin(), config.engines.end(), [](const EngineConfig& e) {
        return e.default_for_chat;
    });
    if (defaults > 1) throw std::runtime_error("Only one engine may set default_for_chat=true.");
    return config;
}

std::vector<std::string> expand_arguments(const EngineConfig& engine) {
    std::vector<std::string> result;
    result.reserve(engine.args.size());
    // Serialize model paths as UTF-8 so Windows wide-character process launch
    // receives the same path after utf8_to_wide(), including non-ASCII usernames.
    const auto model = path_to_utf8(engine.model_path);
    for (auto arg : engine.args) {
        const std::string token = "{model_path}";
        std::size_t at = 0;
        while ((at = arg.find(token, at)) != std::string::npos) {
            arg.replace(at, token.size(), model);
            at += model.size();
        }
        result.push_back(std::move(arg));
    }
    return result;
}

void save_config(const std::filesystem::path& path, const RuntimeConfig& config) {
    if (path.empty()) throw std::runtime_error("Configuration path is empty.");
    if (config.host != "127.0.0.1" && config.host != "localhost") {
        throw std::runtime_error("Only loopback bind addresses are allowed in v0.1.");
    }
    if (config.port == 0) throw std::runtime_error("Configuration port must be non-zero.");

    std::set<std::string> ids;
    unsigned int default_count = 0;
    for (const auto& engine : config.engines) {
        if (!is_valid_engine_id(engine.id)) throw std::runtime_error("Cannot save invalid engine id: " + engine.id);
        if (!ids.insert(engine.id).second) throw std::runtime_error("Cannot save duplicate engine id: " + engine.id);
        if (engine.default_for_chat) ++default_count;
    }
    if (default_count > 1) throw std::runtime_error("Cannot save configuration with more than one default chat engine.");
    if (config.runtime_profile.context_size < 512U || config.runtime_profile.context_size > 32768U) {
        throw std::runtime_error("Cannot save runtime_profile.context_size outside 512..32768 tokens.");
    }
    if ((!config.runtime_profile.engine_component_id.empty() && !is_valid_component_id(config.runtime_profile.engine_component_id)) ||
        (!config.runtime_profile.model_component_id.empty() && !is_valid_component_id(config.runtime_profile.model_component_id))) {
        throw std::runtime_error("Cannot save invalid runtime profile component IDs.");
    }

    std::ostringstream json;
    json << "{\n  \"server\": {\n    \"host\": " << json_quote(config.host)
         << ",\n    \"port\": " << config.port << "\n  },\n  \"engines\": [";
    for (std::size_t i = 0; i < config.engines.size(); ++i) {
        const auto& engine = config.engines[i];
        json << (i == 0 ? "\n" : ",\n") << "    {\n"
             << "      \"id\": " << json_quote(engine.id) << ",\n"
             << "      \"display_name\": " << json_quote(engine.display_name) << ",\n"
             << "      \"executable_path\": " << json_quote(path_to_utf8(engine.executable_path)) << ",\n"
             << "      \"model_path\": " << json_quote(path_to_utf8(engine.model_path)) << ",\n"
             << "      \"working_directory\": " << json_quote(path_to_utf8(engine.working_directory)) << ",\n"
             << "      \"args\": [";
        for (std::size_t a = 0; a < engine.args.size(); ++a) {
            if (a) json << ", ";
            json << json_quote(engine.args[a]);
        }
        json << "],\n      \"upstream_base_url\": " << json_quote(engine.upstream_base_url) << ",\n"
             << "      \"capabilities\": [";
        for (std::size_t c = 0; c < engine.capabilities.size(); ++c) {
            if (c) json << ", ";
            json << json_quote(engine.capabilities[c]);
        }
        json << "],\n      \"default_for_chat\": " << (engine.default_for_chat ? "true" : "false") << "\n    }";
    }
    if (!config.engines.empty()) json << '\n';
    json << "  ],\n  \"runtime_profile\": {\n"
         << "    \"engine_component_id\": " << json_quote(config.runtime_profile.engine_component_id) << ",\n"
         << "    \"model_component_id\": " << json_quote(config.runtime_profile.model_component_id) << ",\n"
         << "    \"context_size\": " << config.runtime_profile.context_size << "\n"
         << "  }\n}\n";

    auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write temporary configuration file: " + path_to_utf8(temporary));
        const auto content = json.str();
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) throw std::runtime_error("Failed while writing configuration file: " + path_to_utf8(temporary));
    }

    std::error_code ec;
    const bool had_previous = std::filesystem::exists(path, ec) && !ec;
    if (had_previous) {
        std::filesystem::remove(backup, ec);
        ec.clear();
        std::filesystem::rename(path, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            throw std::runtime_error("Cannot back up existing configuration: " + ec.message());
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        if (had_previous) {
            std::error_code rollback_error;
            std::filesystem::rename(backup, path, rollback_error);
        }
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace configuration file: " + ec.message());
    }
    if (had_previous) std::filesystem::remove(backup, ec);
}

} // namespace lar
