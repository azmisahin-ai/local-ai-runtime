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
    const auto* manifest_text = reinterpret_cast<const char*>(detail::embedded_manifest_json);
    std::istringstream input(manifest_text);
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
        if (!install_node || !has_only_keys(*install_node, {"strategy", "variant", "filename", "url", "sha256", "size_bytes", "assets", "targets"})) {
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
        if (const auto assets = node.get_child_optional("install.assets")) {
            for (const auto& asset_entry : *assets) {
                const auto& asset_node = asset_entry.second;
                if (!has_only_keys(asset_node, {"role", "filename", "url", "sha256", "size_bytes"})) {
                    throw std::runtime_error("An install asset contains an unknown property: " + component.id);
                }
                ComponentAsset asset;
                asset.role = asset_node.get<std::string>("role", "");
                asset.filename = asset_node.get<std::string>("filename", "");
                asset.url = asset_node.get<std::string>("url", "");
                asset.sha256 = asset_node.get<std::string>("sha256", "");
                asset.size_bytes = asset_node.get<std::uint64_t>("size_bytes", 0U);
                component.assets.push_back(std::move(asset));
            }
        }

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
        if (component.kind == "model") {
            const auto profile = metadata.get_child_optional("model_profile");
            if (!profile || !has_only_keys(*profile, {"task_group", "architecture", "parameter_count", "format", "quantization",
                                                       "recommended_for", "context_tokens", "preferred_context_tokens"})) {
                throw std::runtime_error("A model catalog entry requires a reviewed model_profile: " + component.id);
            }
            const auto task_group = profile->get<std::string>("task_group", "");
            const auto format = profile->get<std::string>("format", "");
            const auto is_non_empty_string = [&](const char* key) {
                const auto value = profile->get<std::string>(key, "");
                return !value.empty() && value.size() <= 128U;
            };
            const auto recommendation = profile->get<std::string>("recommended_for", "");
            const auto context_tokens = profile->get_optional<unsigned int>("context_tokens");
            const auto preferred_context_tokens = profile->get_optional<unsigned int>("preferred_context_tokens");
            const auto valid_context = [](const auto& value) {
                return !value || (*value >= 1U && *value <= 1048576U);
            };
            if ((task_group != "chat" && task_group != "vision" && task_group != "embeddings" &&
                 task_group != "speech" && task_group != "image-generation" && task_group != "video" && task_group != "audio") ||
                (format != "GGUF" && format != "ONNX" && format != "SafeTensors" && format != "Other") ||
                !is_non_empty_string("architecture") || !is_non_empty_string("parameter_count") ||
                !is_non_empty_string("quantization") ||
                (!recommendation.empty() && recommendation != "balanced" && recommendation != "alternative" && recommendation != "low-memory") ||
                !valid_context(context_tokens) || !valid_context(preferred_context_tokens) ||
                (context_tokens && preferred_context_tokens && *preferred_context_tokens > *context_tokens)) {
                throw std::runtime_error("A model catalog entry contains an unsupported or incomplete model_profile: " + component.id);
            }
        } else if (metadata.get_child_optional("model_profile")) {
            throw std::runtime_error("Engine catalog entries cannot declare model_profile: " + component.id);
        }

        const auto dependency_nodes = metadata.get_child_optional("provenance.dependencies");
        if (!dependency_nodes) {
            throw std::runtime_error("Catalog metadata has no dependency declaration: " + component.id);
        }
        std::set<std::string> dependency_ids;
        for (const auto& dependency_entry : *dependency_nodes) {
            if (!has_only_keys(dependency_entry.second, {"id", "kind", "resolution"})) {
                throw std::runtime_error("A catalog dependency contains an unknown property: " + component.id);
            }
            ComponentDependency dependency;
            dependency.id = dependency_entry.second.get<std::string>("id", "");
            dependency.kind = dependency_entry.second.get<std::string>("kind", "");
            dependency.resolution = dependency_entry.second.get<std::string>("resolution", "");
            if (!valid_id(dependency.id) || dependency.kind != "runtime-archive" ||
                dependency.resolution.empty() || dependency.resolution.size() > 240 ||
                !dependency_ids.insert(dependency.id).second) {
                throw std::runtime_error("A catalog dependency has an invalid identity, kind, resolution or duplicate ID: " + component.id);
            }
            component.dependencies.push_back(std::move(dependency));
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
                !component.filename.empty() || !component.url.empty() || !component.sha256.empty() || component.size_bytes != 0U || !component.assets.empty()) {
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
            const auto revision = metadata.get<std::string>("version", "");
            const auto source_url = metadata.get<std::string>("source_url", "");
            if (component.kind != "model" || !is_safe_filename(component.filename) ||
                !is_supported_pinned_gguf_url(component.url, component.filename, revision, source_url) ||
                !is_hex(component.sha256, 64U) || component.size_bytes == 0U || component.size_bytes > 16ULL * 1024ULL * 1024ULL * 1024ULL || !component.assets.empty()) {
                throw std::runtime_error("The pinned GGUF install policy failed its reviewed Hugging Face URL, filename, revision, digest or size checks: " + component.id);
            }
            if (metadata.get<std::string>("sha256", "") != component.sha256 ||
                metadata.get<std::uint64_t>("size_bytes", 0U) != component.size_bytes ||
                metadata.get<std::string>("provenance.source_url", "") != source_url ||
                metadata.get<std::string>("provenance.revision", "") != revision ||
                metadata.get<std::string>("provenance.revision_policy", "") != "pinned-revision" ||
                metadata.get<std::string>("provenance.digest_policy", "") != "pinned-sha256-and-exact-size" ||
                metadata.get<std::string>("model_profile.format", "") != "GGUF") {
                throw std::runtime_error("The pinned GGUF install metadata does not match the reviewed source, revision, format, digest or exact size: " + component.id);
            }
            const auto target_allowed = [](const InstallTarget& target) {
                return (target.platform == "windows" && target.architecture == "x64") ||
                       (target.platform == "linux" && target.architecture == "x64") ||
                       (target.platform == "macos" && (target.architecture == "x64" || target.architecture == "arm64"));
            };
            if (!std::all_of(component.targets.begin(), component.targets.end(), target_allowed)) {
                throw std::runtime_error("The pinned GGUF policy declares an unsupported platform target: " + component.id);
            }
        } else if (component.installer == "audio-cpp-release-set") {
            if (component.kind != "engine" || component.default_model ||
                (component.variant != "cpu" && component.variant != "cuda13.3") ||
                !component.filename.empty() || !component.url.empty() || !component.sha256.empty() ||
                component.size_bytes != 0U || component.assets.empty() || component.assets.size() > 2U ||
                metadata.get<std::string>("family", "") != "audio.cpp" ||
                metadata.get<std::string>("version", "") != "v0.9.3" ||
                metadata.get<std::string>("backend", "") != component.variant ||
                metadata.get<std::string>("license", "") != "Apache-2.0" ||
                metadata.get<std::string>("provenance.source_url", "") != "https://github.com/0xShug0/audio.cpp" ||
                metadata.get<std::string>("provenance.revision", "") != "v0.9.3" ||
                metadata.get<std::string>("provenance.digest_policy", "") != "pinned-sha256-and-exact-size") {
                throw std::runtime_error("The audio.cpp release-set install policy is incomplete or invalid: " + component.id);
            }
            const auto target_allowed = [](const InstallTarget& target) {
                return target.platform == "windows" && target.architecture == "x64";
            };
            if (component.targets.size() != 1U || !std::all_of(component.targets.begin(), component.targets.end(), target_allowed)) {
                throw std::runtime_error("audio.cpp release-set is currently allowlisted only for Windows x64: " + component.id);
            }
            std::set<std::string> roles;
            for (const auto& asset : component.assets) {
                if (!is_supported_audio_cpp_release_asset(asset.role, asset.filename, asset.url,
                                                          asset.sha256, asset.size_bytes, component.variant) ||
                    !roles.insert(asset.role).second) {
                    throw std::runtime_error("An audio.cpp release asset failed the exact filename/URL/digest/size allowlist: " + component.id);
                }
            }
            if ((component.variant == "cpu" && (component.assets.size() != 1U || !roles.count("server"))) ||
                (component.variant == "cuda13.3" &&
                 (component.assets.size() != 2U || !roles.count("server") || !roles.count("runtime")))) {
                throw std::runtime_error("audio.cpp package set is incomplete or mixes CPU/CUDA assets: " + component.id);
            }
        } else {
            throw std::runtime_error("The component manifest contains an unknown install strategy: " + component.installer);
        }

        if (component.default_model) {
            if (component.kind != "model" || component.installer != "pinned-gguf") {
                throw std::runtime_error("Only a pinned GGUF model can be marked as the default model: " + component.id);
            }
            ++default_model_count;
        }

        if (component.installer == "llama-release" && component.variant == "cuda12") {
            if (component.dependencies.size() != 1U || component.dependencies.front().id != "llama.cpp-cudart-cuda12") {
                throw std::runtime_error("CUDA 12 llama.cpp must declare its matching runtime archive dependency.");
            }
        } else if (component.installer == "llama-release" && component.variant == "cuda13") {
            if (component.dependencies.size() != 1U || component.dependencies.front().id != "llama.cpp-cudart-cuda13") {
                throw std::runtime_error("CUDA 13 llama.cpp must declare its matching runtime archive dependency.");
            }
        } else if (!component.dependencies.empty()) {
            throw std::runtime_error("This install strategy does not support external component dependencies: " + component.id);
        }
        result.push_back(std::move(component));
    }

    if (default_model_count != 1U) {
        throw std::runtime_error("The component manifest must contain exactly one default pinned model.");
    }
    return result;
}

} // namespace

bool is_supported_pinned_gguf_url(std::string_view url, std::string_view filename,
                                  std::string_view revision, std::string_view source_url) {
    constexpr std::string_view prefix = "https://huggingface.co/";
    const auto safe_segment = [](std::string_view value) {
        if (value.empty() || value == "." || value == ".." || value.front() == '.' || value.back() == '.') return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                   (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        });
    };
    if (url.rfind(prefix, 0) != 0 || url.find_first_of("?#\\%") != std::string_view::npos ||
        !safe_segment(filename) || !is_hex(revision, 40U)) {
        return false;
    }

    auto remainder = url.substr(prefix.size());
    std::string_view segments[4];
    for (std::size_t index = 0; index < 4U; ++index) {
        const auto slash = remainder.find('/');
        if (slash == std::string_view::npos) return false;
        segments[index] = remainder.substr(0, slash);
        remainder.remove_prefix(slash + 1U);
    }

    const auto owner = segments[0];
    const auto repository = segments[1];
    if (!safe_segment(owner) || !safe_segment(repository) || segments[2] != "resolve" ||
        !is_hex(segments[3], 40U) || segments[3] != revision || remainder.empty()) {
        return false;
    }

    // Hugging Face repositories may keep a single GGUF artifact in a nested
    // directory. Permit only safe, unencoded path segments and still pin the
    // final basename independently from the URL.
    std::string_view final_segment;
    while (!remainder.empty()) {
        const auto slash = remainder.find('/');
        const auto segment = slash == std::string_view::npos ? remainder : remainder.substr(0, slash);
        if (!safe_segment(segment)) return false;
        final_segment = segment;
        if (slash == std::string_view::npos) break;
        remainder.remove_prefix(slash + 1U);
        if (remainder.empty()) return false;
    }
    if (final_segment != filename) return false;

    const auto expected_source = std::string(prefix) + std::string(owner) + "/" + std::string(repository);
    return source_url == expected_source;
}

bool is_supported_audio_cpp_release_asset(std::string_view role, std::string_view filename,
                                          std::string_view url, std::string_view sha256,
                                          std::uint64_t size_bytes, std::string_view variant) {
    struct Pin {
        std::string_view role;
        std::string_view filename;
        std::string_view sha256;
        std::uint64_t size_bytes;
    };
    // Exact `assets[].size` values and SHA-256 digests from the official
    // GitHub v0.9.3 release API. These are immutable pins, not rounded UI sizes.
    static constexpr Pin cpu{"server", "audio-v0.9.3-bin-windows-x64-cpu.zip",
        "3ac66f46395b4e92e4f3bb68b200f0032310bd9922f12d97f10ccdadb02dee4b", 30547092ULL};
    static constexpr Pin cuda_server{"server", "audio-v0.9.3-bin-windows-x64-cuda13.3.zip",
        "0bd3988be10c31ec078c40df2bd0d4266e660804f6d5083aeb05d1309d091e43", 277336614ULL};
    static constexpr Pin cuda_runtime{"runtime", "audio-v0.9.3-cudart-windows-x64-cuda13.3.zip",
        "839eb20f684a8aab64895b1e466b32ce9b29dfc19dc1ce15b2ef35218512ac0a", 575457454ULL};
    const Pin* expected = nullptr;
    if (variant == "cpu") expected = &cpu;
    else if (variant == "cuda13.3" && role == "server") expected = &cuda_server;
    else if (variant == "cuda13.3" && role == "runtime") expected = &cuda_runtime;
    if (!expected || expected->role != role || expected->filename != filename ||
        expected->sha256 != sha256 || expected->size_bytes != size_bytes) {
        return false;
    }
    const auto expected_url = std::string("https://github.com/0xShug0/audio.cpp/releases/download/v0.9.3/") + std::string(filename);
    return url == expected_url;
}

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
