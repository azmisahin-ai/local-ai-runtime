#include "lar/config.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace lar {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> read_string_array(const boost::property_tree::ptree& parent,
                                           const std::string& key) {
    std::vector<std::string> values;
    const auto child = parent.get_child_optional(key);
    if (!child) return values;
    for (const auto& item : *child) values.push_back(item.second.get_value<std::string>());
    return values;
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
    boost::property_tree::read_json(path.string(), root);

    RuntimeConfig config;
    config.host = trim(root.get<std::string>("server.host", "127.0.0.1"));
    if (config.host != "127.0.0.1" && config.host != "localhost") {
        throw std::runtime_error("Only loopback bind addresses are allowed in v0.1 (127.0.0.1 or localhost).");
    }
    const auto port = root.get<unsigned int>("server.port", 8000U);
    if (port == 0U || port > 65535U) throw std::runtime_error("server.port must be between 1 and 65535.");
    config.port = static_cast<unsigned short>(port);

    const auto engines = root.get_child_optional("engines");
    if (!engines) throw std::runtime_error("Config is missing the 'engines' array.");

    std::set<std::string> ids;
    for (const auto& entry : *engines) {
        const auto& node = entry.second;
        EngineConfig engine;
        engine.id = trim(node.get<std::string>("id", ""));
        engine.display_name = trim(node.get<std::string>("display_name", engine.id));
        engine.executable_path = node.get<std::string>("executable_path", "");
        engine.model_path = node.get<std::string>("model_path", "");
        engine.working_directory = node.get<std::string>("working_directory", "");
        engine.args = read_string_array(node, "args");
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
    const auto model = engine.model_path.string();
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

} // namespace lar
