#include "lar/task_adapter.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lar {
namespace {
namespace pt = boost::property_tree;

bool is_safe_token(std::string_view value, std::size_t max_length = 128U) {
    if (value.empty() || value == "." || value == ".." || value.size() > max_length) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
    });
}

bool is_safe_language(std::string_view value) {
    if (value.empty()) return true;
    if (value.size() > 16U) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-';
    });
}

bool is_wav(std::string_view value) {
    if (value.size() < 12U || value.compare(0, 4, "RIFF") != 0 ||
        value.compare(8, 4, "WAVE") != 0) return false;
    const auto byte = [&](std::size_t index) { return static_cast<std::uint32_t>(static_cast<unsigned char>(value[index])); };
    const auto declared_riff_size = byte(4U) | (byte(5U) << 8U) | (byte(6U) << 16U) | (byte(7U) << 24U);
    return static_cast<std::uint64_t>(declared_riff_size) + 8ULL == value.size();
}

std::string json_quote(std::string_view value) {
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

class AudioCppTaskAdapter final : public TaskEngineAdapter {
public:
    AudioCppTaskAdapter() {
        capabilities_ = {
            {"asr.transcribe", "audio/wav", "application/json", max_task_audio_input_bytes, false},
            {"tts.synthesize", "application/json", "audio/wav", 64U * 1024U, false},
        };
    }

    std::string_view id() const noexcept override { return "audio.cpp"; }
    std::string_view display_name() const noexcept override { return "audio.cpp task runtime"; }
    const std::vector<TaskCapability>& capabilities() const noexcept override { return capabilities_; }

    bool validate_config(const EngineConfig& config, std::string& error) const override {
        if (config.id != "audio-cpp") {
            error = "The audio.cpp task adapter requires engine id 'audio-cpp'.";
            return false;
        }
        if (!config.executable_path.empty()) {
            auto filename = config.executable_path.filename().string();
            std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
#ifdef _WIN32
            if (filename != "audiocpp_server.exe" && filename != "audiocpp_server") {
#else
            if (filename != "audiocpp_server") {
#endif
                error = "The audio.cpp adapter only starts the reviewed audiocpp_server executable.";
                return false;
            }
        }
        error.clear();
        return true;
    }

    bool build_asr_request(const AsrTranscriptionRequest& request,
                           TaskUpstreamRequest& upstream, std::string& error) const override {
        if (!is_safe_token(request.model_id)) {
            error = "ASR model_id must be a non-empty identifier containing only letters, digits, '.', '_' or '-'.";
            return false;
        }
        if (!is_safe_language(request.language)) {
            error = "ASR language must be an ISO-style language token such as 'tr'.";
            return false;
        }
        if (request.wav_bytes.size() < 12U || request.wav_bytes.size() > max_task_audio_input_bytes ||
            !is_wav(request.wav_bytes)) {
            error = "ASR input must be a valid RIFF/WAVE payload no larger than 24 MiB.";
            return false;
        }

        constexpr std::string_view boundary = "----LocalAIRuntimeAudioCppBoundary7MA4YWxk";
        if (request.wav_bytes.find(boundary) != std::string::npos) {
            error = "The audio payload conflicts with the multipart framing token.";
            return false;
        }
        std::string body;
        body.reserve(request.wav_bytes.size() + 512U);
        const auto field = [&](std::string_view name, std::string_view value) {
            body += "--";
            body += boundary;
            body += "\r\nContent-Disposition: form-data; name=\"";
            body += name;
            body += "\"\r\n\r\n";
            body += value;
            body += "\r\n";
        };
        field("model", request.model_id);
        if (!request.language.empty()) field("language", request.language);
        body += "--";
        body += boundary;
        body += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
        body += "Content-Type: audio/wav\r\n\r\n";
        body += request.wav_bytes;
        body += "\r\n--";
        body += boundary;
        body += "--\r\n";

        upstream.path = "/v1/audio/transcriptions";
        upstream.content_type = "multipart/form-data; boundary=" + std::string(boundary);
        upstream.body = std::move(body);
        error.clear();
        return true;
    }

    bool parse_asr_response(std::string_view content_type, std::string_view body,
                            AsrTranscriptionResult& result, std::string& error) const override {
        if (content_type.find("application/json") == std::string_view::npos || body.empty() ||
            body.size() > 1024U * 1024U) {
            error = "audio.cpp returned an invalid or oversized ASR response.";
            return false;
        }
        try {
            std::istringstream input{std::string(body)};
            pt::ptree response;
            pt::read_json(input, response);
            const auto text = response.get_optional<std::string>("text");
            if (!text) {
                error = "audio.cpp ASR response did not contain the required transcript text.";
                return false;
            }
            result.text = *text;
            result.language = response.get<std::string>("language", "");
            if (!is_safe_language(result.language)) result.language.clear();
            error.clear();
            return true;
        } catch (const std::exception&) {
            error = "audio.cpp returned malformed ASR JSON.";
            return false;
        }
    }

    bool build_tts_request(const TtsSynthesisRequest& request,
                           TaskUpstreamRequest& upstream, std::string& error) const override {
        if (!is_safe_token(request.model_id) || request.text.empty() || request.text.size() > 64U * 1024U ||
            request.text.find('\0') != std::string::npos ||
            (!request.voice.empty() && !is_safe_token(request.voice))) {
            error = "TTS requires a valid model_id, text up to 64 KiB and an optional safe voice identifier.";
            return false;
        }
        upstream.path = "/v1/audio/speech";
        upstream.content_type = "application/json";
        upstream.body = "{\"model\":" + json_quote(request.model_id) +
                        ",\"input\":" + json_quote(request.text) +
                        ",\"response_format\":\"wav\"";
        if (!request.voice.empty()) upstream.body += ",\"voice\":" + json_quote(request.voice);
        upstream.body += "}";
        error.clear();
        return true;
    }

    bool parse_tts_response(std::string_view content_type, std::string_view body,
                            TtsSynthesisResult& result, std::string& error) const override {
        if ((content_type.find("audio/wav") == std::string_view::npos &&
             content_type.find("audio/x-wav") == std::string_view::npos) ||
            body.size() < 12U || body.size() > max_task_audio_output_bytes || !is_wav(body)) {
            error = "audio.cpp did not return a bounded WAV audio artifact.";
            return false;
        }
        result.content_type = "audio/wav";
        result.wav_bytes.assign(body.data(), body.size());
        error.clear();
        return true;
    }

private:
    std::vector<TaskCapability> capabilities_;
};

} // namespace

std::string build_audio_cpp_server_config_json(std::string_view backend, unsigned short port) {
    if ((backend != "cpu" && backend != "cuda") || port == 0U) {
        throw std::invalid_argument("audio.cpp server configuration requires a CPU/CUDA backend and non-zero port.");
    }
    std::ostringstream output;
    output << "{\n"
           << "  \"host\": \"127.0.0.1\",\n"
           << "  \"port\": " << port << ",\n"
           << "  \"backend\": \"" << backend << "\",\n"
           << "  \"ui_management\": true,\n"
           << "  \"lazy_load\": true,\n"
           << "  \"max_loaded_models\": 1,\n"
           << "  \"models\": []\n"
           << "}\n";
    return output.str();
}

std::string register_audio_cpp_asr_model_config_json(std::string_view config_json,
                                                     std::string_view model_id,
                                                     std::string_view model_path) {
    if (config_json.empty() || config_json.size() > 1024U * 1024U ||
        !is_safe_token(model_id) || model_path.empty() || model_path.size() > 32767U ||
        model_path.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("audio.cpp ASR model registration requires a bounded config, safe model id and absolute model path.");
    }

    pt::ptree config;
    try {
        std::istringstream input{std::string(config_json)};
        pt::read_json(input, config);
    } catch (const std::exception& ex) {
        throw std::invalid_argument(std::string("audio.cpp server config is not valid JSON: ") + ex.what());
    }

    const auto backend = config.get<std::string>("backend", "");
    if (backend != "cpu" && backend != "cuda") {
        throw std::invalid_argument("audio.cpp ASR registration requires an explicit CPU or CUDA server backend.");
    }

    const auto models = config.get_child_optional("models");
    if (!models) {
        throw std::invalid_argument("audio.cpp server config is missing its models array.");
    }
    bool already_registered = false;
    for (const auto& item : *models) {
        if (!item.first.empty()) {
            throw std::invalid_argument("audio.cpp models must be represented as a JSON array.");
        }
        if (item.second.get<std::string>("id", "") != model_id) continue;
        if (already_registered) {
            throw std::invalid_argument("audio.cpp server config contains duplicate registrations for the requested ASR model id.");
        }
        const bool identical = item.second.get<std::string>("family", "") == "qwen3_asr" &&
                               item.second.get<std::string>("path", "") == model_path &&
                               item.second.get<std::string>("task", "") == "asr" &&
                               item.second.get<std::string>("mode", "") == "offline";
        if (!identical) {
            throw std::invalid_argument("audio.cpp already contains a conflicting model registration with the requested id.");
        }
        already_registered = true;
    }
    if (already_registered) return std::string(config_json);
    if (!models->empty()) {
        throw std::invalid_argument("audio.cpp server config already contains other model registrations; refusing to overwrite or reorder them.");
    }

    // The current managed config is generated by build_audio_cpp_server_config_json.
    // Do not round-trip it through property_tree's writer: that API stringifies
    // JSON numbers and booleans. Replace only its known empty models array.
    constexpr std::string_view empty_models = "\"models\": []";
    const auto marker = config_json.find(empty_models);
    if (marker == std::string_view::npos ||
        config_json.find(empty_models, marker + empty_models.size()) != std::string_view::npos) {
        throw std::invalid_argument("audio.cpp config is not the expected managed empty-model template; refusing an unsafe text rewrite.");
    }
    const auto model_json = std::string("\"models\": [\n    {\n") +
        "      \"id\": " + json_quote(model_id) + ",\n" +
        "      \"family\": \"qwen3_asr\",\n" +
        "      \"path\": " + json_quote(model_path) + ",\n" +
        "      \"task\": \"asr\",\n" +
        "      \"mode\": \"offline\"\n" +
        "    }\n  ]";
    std::string updated(config_json);
    updated.replace(marker, empty_models.size(), model_json);
    return updated;
}

std::shared_ptr<TaskEngineAdapter> create_task_engine_adapter(const EngineConfig& config,
                                                              std::string& error) {
    const auto adapter_id = config.adapter_id.empty() ? default_engine_adapter_id(config.id) : config.adapter_id;
    if (adapter_id != "audio.cpp") {
        error = "No typed task adapter is registered for engine adapter '" + adapter_id + "'.";
        return {};
    }
    auto adapter = std::make_shared<AudioCppTaskAdapter>();
    if (!adapter->validate_config(config, error)) return {};
    error.clear();
    return adapter;
}

} // namespace lar
