#pragma once

#include "lar/process.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace lar {

inline constexpr std::uint32_t engine_adapter_contract_version = 1;

// EngineAdapter is the stable runtime boundary between Local AI Runtime and a
// concrete local inference server. Process creation remains centralized in
// ManagedProcess; adapters own engine-specific validation, lifecycle policy,
// readiness probes and upstream protocol paths.
class EngineAdapter {
public:
    virtual ~EngineAdapter() = default;

    virtual std::string_view id() const noexcept = 0;
    virtual std::string_view display_name() const noexcept = 0;
    virtual bool validate_config(const EngineConfig& config, std::string& error) const = 0;

    virtual bool start(ManagedProcess& process, const EngineConfig& config, std::string& error) const;
    virtual bool stop(ManagedProcess& process, std::string& error) const;
    virtual ProcessState state(ManagedProcess& process) const;
    virtual std::int64_t pid(const ManagedProcess& process) const;
    virtual int exit_code(const ManagedProcess& process) const;

    virtual std::string_view readiness_path() const noexcept = 0;
    virtual std::string_view readiness_fallback_path() const noexcept = 0;
    virtual std::string_view chat_completions_path() const noexcept = 0;
};

// Legacy configs without adapter_id are migrated deterministically: the
// built-in llama-cpp engine uses the llama.cpp adapter; other process-backed
// OpenAI-compatible engines use the compatibility adapter.
std::shared_ptr<EngineAdapter> create_engine_adapter(const EngineConfig& config, std::string& error);

} // namespace lar
