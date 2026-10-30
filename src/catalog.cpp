#include "lar/catalog.hpp"
#include "lar/catalog_manifest_embedded.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace lar::catalog {
namespace {
namespace pt = boost::property_tree;

bool is_hex(std::string_view value, std::size_t expected_size) {
    return value.size() == expected_size && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

bool is_safe_filename(std::string_view value) {
    if (value.empty() || value == "." || value == "..") return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-';
    });
}

bool valid_id(std::string_view value) {
    static const std::regex pattern(R"(^[a-z0-9][a-z0-9._-]{0,127}$)");
    return std::regex_match(value.begin(), value.end(), pattern);
}

bool has_only_keys(const pt::ptree& node, const std::set<std::string>& allowed) {
    return std::all_of(node.begin(), node.end(), [&](const auto& property) {
        return allowed.find(property.first) != allowed.end();
    });
}

std::vector<ComponentDefinition> parse_embedded_manifest() {
    std::istringstream input(detail::embedded_manifest_json);
    pt::ptree root;
    try {
        pt::read_json(input, root);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("The embedded component manifest is invalid JSON: ") + ex.what());
    }

    if (!has_only_keys(root, {"schema_version", "manifest_version", "components"})) {
        throw std::runtime_error("The component manifest contains an unknown top-level property.");
    }
    if (root.get<unsigned int>("schema_version", 0U) != 1U) {
        throw std::runtime_error("Unsupported component manifest schema version.");
    }
    const auto manifest_version = root.get<std::string>("manifest_version", "");
    if (manifest_version.empty() || manifest_version.size() > 32) {
        throw std::runtime_error("The component manifest version is missing or invalid.");
    }
    const auto entries = root.get_child_optional("components");
    if (!entries || entries->empty()) throw std::runtime_error("The component manifest contains no components.");

    std::vector<ComponentDefinition> result;
    std::set<std::string> ids;
    std::size_t default_model_count = 0;
    for (const auto& entry : *entries) {
        const auto& node = entry.second;
        if (!has_only_keys(node, {"id", "kind", "installable", "default_model", "install", "metadata_json"})) {
            throw std::runtime_error("The component manifest entry contains an unknown property.");
        }
        const auto install_node = node.get_child_optional("install");
        if (!install_node || !has_only_keys(*install_node, {"strategy", "variant", "filename", "url", "sha256", "size_bytes", "targets"})) {
            throw std::runtime_error("The component manifest install policy is missing or contains an unknown property.");
        }
        ComponentDefinition component;
        component.id = node.get<std::string>("id", "");
        component.kind = node.get<std::string>("kind", "");
        component.installable = node.get<bool>("installable", false);
        component.default_model = node.get<bool>("default_model", false);
        component.installer = node.get<std::string>("install.strategy", "");
        component.variant = node.get<std::string>("install.variant", "");
        component.filename = node.get<std::string>("install.filename", "");
        component.url = node.get<std::string>("install.url", "");
        component.sha256 = node.get<std::string>("install.sha256", "");
        component.size_bytes = node.get<std::uint64_t>("install.size_bytes", 0U);
        component.metadata_json = node.get<std::string>("metadata_json", "");

        if (!valid_id(component.id) || !ids.insert(component.id).second) {
            throw std::runtime_error("The component manifest contains an invalid or duplicate component ID.");
        }
        if (!component.installable || component.metadata_json.empty() ||
            component.metadata_json.front() != '{' || component.metadata_json.back() != '}') {
            throw std::runtime_error("Every shipped manifest component must be installable and contain a JSON metadata object.");
        }
        if (component.kind != "engine" && component.kind != "model") {
            throw std::runtime_error("The component manifest contains an unsupported component kind.");
        }

        std::istringstream metadata_input(component.metadata_json);
        pt::ptree metadata;
        try {
            pt::read_json(metadata_input, metadata);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("Invalid catalog metadata for ") + component.id + ": " + ex.what());
        }
        if (metadata.get<std::string>("id", "") != component.id ||
            metadata.get<std::string>("type", "") != component.kind ||
            metadata.get<unsigned int>("schema_version", 0U) != 1U) {
            throw std::runtime_error("Catalog metadata identity does not match its install manifest entry: " + component.id);
        }

        const auto target_nodes = node.get_child_optional("install.targets");
        if (!target_nodes || target_nodes->empty()) {
            throw std::runtime_error("The component manifest entry has no supported install targets: " + component.id);
        }
        std::set<std::pair<std::string, std::string>> target_keys;
        for (const auto& target_entry : *target_nodes) {
            if (!has_only_keys(target_entry.second, {"platform", "architecture"})) {
                throw std::runtime_error("A component manifest target contains an unknown property: " + component.id);
            }
            InstallTarget target;
            target.platform = target_entry.second.get<std::string>("platform", "");
            target.architecture = target_entry.second.get<std::string>("architecture", "");
            const bool platform_valid = target.platform == "windows" || target.platform == "linux" || target.platform == "macos";
            const bool architecture_valid = target.architecture == "x64" || target.architecture == "arm64";
            if (!platform_valid || !architecture_valid ||
                !target_keys.emplace(target.platform, target.architecture).second) {
                throw std::runtime_error("The component manifest contains an invalid or duplicate platform target: " + component.id);
            }
            component.targets.push_back(std::move(target));
        }

        std::set<std::pair<std::string, std::string>> metadata_target_keys;
        const auto metadata_targets = metadata.get_child_optional("compatibility.targets");
        if (!metadata_targets) throw std::runtime_error("Catalog metadata has no compatibility targets: " + component.id);
        for (const auto& target_entry : *metadata_targets) {
            const auto platform = target_entry.second.get<std::string>("platform", "");
            const auto architecture = target_entry.second.get<std::string>("architecture", "");
            if (!metadata_target_keys.emplace(platform, architecture).second) {
                throw std::runtime_error("Catalog metadata contains a duplicate compatibility target: " + component.id);
            }
        }
        if (metadata_target_keys != target_keys) {
            throw std::runtime_error("Catalog metadata and install allowlist targets do not match: " + component.id);
        }

        if (component.installer == "llama-release") {
            if (component.kind != "engine" || component.default_model ||
                (component.variant != "cpu" && component.variant != "cuda12" && component.variant != "cuda13") ||
                !component.filename.empty() || !component.url.empty() || !component.sha256.empty() || component.size_bytes != 0U) {
                throw std::runtime_error("The llama-release install policy is invalid for component: " + component.id);
            }
            if (metadata.get<std::string>("family", "") != "llama.cpp" ||
                metadata.get<std::string>("backend", "") != component.variant) {
                throw std::runtime_error("The llama-release policy must target the reviewed llama.cpp family and matching backend.");
            }
            const auto target_allowed = [&](const InstallTarget& target) {
                if (component.variant == "cpu") {
                    return (target.platform == "windows" && target.architecture == "x64") ||
                           (target.platform == "linux" && target.architecture == "x64") ||
                           (target.platform == "macos" && (target.architecture == "x64" || target.architecture == "arm64"));
                }
                return (target.platform == "windows" || target.platform == "linux") && target.architecture == "x64";
            };
            if (!std::all_of(component.targets.begin(), component.targets.end(), target_allowed)) {
                throw std::runtime_error("The llama-release policy declares a platform target not supported by its reviewed asset resolver: " + component.id);
            }
        } else if (component.installer == "pinned-gguf") {
            constexpr std::string_view allowed_url_prefix =
                "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/";
            if (component.kind != "model" || !component.default_model || !is_safe_filename(component.filename) ||
                component.url.rfind(allowed_url_prefix, 0) != 0 ||
                component.url.substr(component.url.find_last_of('/') + 1U) != component.filename ||
                !is_hex(component.sha256, 64U) || component.size_bytes == 0U || component.size_bytes > 16ULL * 1024ULL * 1024ULL * 1024ULL) {
                throw std::runtime_error("The pinned GGUF install policy failed its URL, filename, digest or size checks: " + component.id);
            }
            const auto revision_start = allowed_url_prefix.size();
            const auto revision_end = component.url.find('/', revision_start);
            if (revision_end == std::string::npos || !is_hex(std::string_view(component.url).substr(revision_start, revision_end - revision_start), 40U)) {
                throw std::runtime_error("The pinned GGUF URL does not contain an immutable 40-character revision: " + component.id);
            }
            if (metadata.get<std::string>("sha256", "") != component.sha256 ||
                metadata.get<std::uint64_t>("size_bytes", 0U) != component.size_bytes) {
                throw std::runtime_error("The pinned GGUF install metadata does not match the catalog metadata: " + component.id);
            }
            const auto target_allowed = [](const InstallTarget& target) {
                return (target.platform == "windows" && target.architecture == "x64") ||
                       (target.platform == "linux" && target.architecture == "x64") ||
                       (target.platform == "macos" && (target.architecture == "x64" || target.architecture == "arm64"));
            };
            if (!std::all_of(component.targets.begin(), component.targets.end(), target_allowed)) {
                throw std::runtime_error("The pinned GGUF policy declares an unsupported platform target: " + component.id);
            }
            ++default_model_count;
        } else {
            throw std::runtime_error("The component manifest contains an unknown install strategy: " + component.installer);
        }
        result.push_back(std::move(component));
    }

    if (default_model_count != 1U) {
        throw std::runtime_error("The component manifest must contain exactly one default pinned model.");
    }
    return result;
}

} // namespace

const std::vector<ComponentDefinition>& components() {
    static const auto values = parse_embedded_manifest();
    return values;
}

const ComponentDefinition* find_component(std::string_view id) {
    const auto& values = components();
    const auto found = std::find_if(values.begin(), values.end(), [&](const ComponentDefinition& component) {
        return component.id == id;
    });
    return found == values.end() ? nullptr : &*found;
}

const ComponentDefinition* default_model_component() {
    const auto& values = components();
    const auto found = std::find_if(values.begin(), values.end(), [](const ComponentDefinition& component) {
        return component.default_model && component.kind == "model" && component.installer == "pinned-gguf";
    });
    return found == values.end() ? nullptr : &*found;
}

bool supports_target(const ComponentDefinition& component, const std::string& platform,
                     const std::string& architecture) {
    return std::any_of(component.targets.begin(), component.targets.end(), [&](const InstallTarget& target) {
        return target.platform == platform && target.architecture == architecture;
    });
}

bool is_known_engine_variant(const std::string& variant) {
    return std::any_of(components().begin(), components().end(), [&](const ComponentDefinition& component) {
        return component.installable && component.installer == "llama-release" && component.variant == variant;
    });
}

} // namespace lar::catalog
