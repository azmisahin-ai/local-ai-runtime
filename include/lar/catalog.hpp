#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lar::catalog {

struct InstallTarget {
    std::string platform;
    std::string architecture;
};

// A compiled, reviewed component definition. The catalog manifest is embedded
// into the runtime at build time; it is not loaded from a user-writable path.
struct ComponentDependency {
    std::string id;
    std::string kind;
    std::string resolution;
};

struct ComponentAsset {
    std::string role;
    std::string filename;
    std::string url;
    std::string sha256;
    std::uint64_t size_bytes{0};
};

struct ComponentDefinition {
    std::string id;
    std::string kind;
    std::string installer;
    std::string variant;
    std::string filename;
    std::string url;
    std::string sha256;
    std::uint64_t size_bytes{0};
    std::vector<ComponentAsset> assets;
    bool installable{false};
    bool default_model{false};
    std::vector<InstallTarget> targets;
    std::vector<ComponentDependency> dependencies;
    std::string metadata_json;
};

// Throws when the embedded manifest is invalid, duplicated or contains an
// installer strategy/target outside the runtime's explicitly supported set.
const std::vector<ComponentDefinition>& components();
const ComponentDefinition* find_component(std::string_view id);
const ComponentDefinition* default_model_component();
bool supports_target(const ComponentDefinition& component, const std::string& platform,
                     const std::string& architecture);
bool is_known_engine_variant(const std::string& variant);

// Accepts only a single GGUF file from a pinned Hugging Face revision.
// The caller must still verify the downloaded file against its declared digest and size.
bool is_supported_pinned_gguf_url(std::string_view url, std::string_view filename,
                                  std::string_view revision, std::string_view source_url);

// Pure, host-parameterized archive-name predicates used by the release resolver
// and regression tests. Callers must still verify HTTPS URL, SHA-256 and size.
bool is_supported_engine_archive(const std::string& name, const std::string& variant,
                                 const std::string& os, const std::string& architecture);
bool is_supported_cuda_runtime_archive(const std::string& name, const std::string& variant,
                                       const std::string& os, const std::string& architecture);

// Exact upstream v0.9.3 Windows x64 asset allowlist. Size is supplied by the
// immutable release manifest and must be positive/bounded; the publisher's
// exact byte count was not available from the release HTML snapshot.
bool is_supported_audio_cpp_release_asset(std::string_view role, std::string_view filename,
                                          std::string_view url, std::string_view sha256,
                                          std::uint64_t size_bytes, std::string_view variant);

} // namespace lar::catalog
