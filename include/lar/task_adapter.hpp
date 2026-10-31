#pragma once

#include "lar/config.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lar {

inline constexpr std::uint32_t task_adapter_contract_version = 1;
inline constexpr std::uint64_t max_task_audio_input_bytes = 24ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t max_task_audio_output_bytes = 24ULL * 1024ULL * 1024ULL;

struct TaskCapability {
    std::string id;
    std::string input_media_type;
    std::string output_media_type;
    std::uint64_t max_input_bytes{0};
    bool cancellable{false};
};

struct AsrTranscriptionRequest {
    std::string model_id;
    std::string language;
    // WAV bytes are held in memory and bounded by max_task_audio_input_bytes.
    // Callers must never pass an arbitrary filesystem path.
    std::string wav_bytes;
};

struct TtsSynthesisRequest {
    std::string model_id;
    std::string text;
    std::string voice;
};

struct TaskUpstreamRequest {
    std::string path;
    std::string content_type;
    std::string body;
};

struct AsrTranscriptionResult {
    std::string text;
    std::string language;
};

struct TtsSynthesisResult {
    std::string content_type;
    std::string wav_bytes;
};

// Typed, task-specific protocol boundary. It is deliberately separate from
// EngineAdapter's Chat completions contract; new task families must not
// masquerade as OpenAI Chat engines.
class TaskEngineAdapter {
public:
    virtual ~TaskEngineAdapter() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual std::string_view display_name() const noexcept = 0;
    virtual const std::vector<TaskCapability>& capabilities() const noexcept = 0;
    virtual bool validate_config(const EngineConfig& config, std::string& error) const = 0;
    virtual bool build_asr_request(const AsrTranscriptionRequest& request,
                                   TaskUpstreamRequest& upstream, std::string& error) const = 0;
    virtual bool parse_asr_response(std::string_view content_type, std::string_view body,
                                    AsrTranscriptionResult& result, std::string& error) const = 0;
    virtual bool build_tts_request(const TtsSynthesisRequest& request,
                                   TaskUpstreamRequest& upstream, std::string& error) const = 0;
    virtual bool parse_tts_response(std::string_view content_type, std::string_view body,
                                    TtsSynthesisResult& result, std::string& error) const = 0;
};

std::shared_ptr<TaskEngineAdapter> create_task_engine_adapter(const EngineConfig& config,
                                                              std::string& error);

} // namespace lar
