#include "lar/server.hpp"
#include "lar/downloads.hpp"
#include "lar/diagnostics.hpp"

#ifndef LAR_VERSION
#define LAR_VERSION "0.1.0-dev"
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <cctype>
#include <sstream>
#include <string_view>
#include <thread>

namespace lar {
namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

volatile std::sig_atomic_t shutdown_requested = 0;
void request_shutdown(int) { shutdown_requested = 1; }

std::string json_escape(std::string_view input) {
    std::ostringstream out;
    for (const unsigned char ch : input) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20U) {
                    constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(ch >> 4U) & 0x0fU] << hex[ch & 0x0fU];
                } else out << static_cast<char>(ch);
        }
    }
    return out.str();
}

std::string json_quoted(std::string_view value) { return "\"" + json_escape(value) + "\""; }

bool is_loopback_host(const std::string& header, unsigned short port) {
    const auto expected_port = ":" + std::to_string(port);
    return header == "localhost" + expected_port || header == "127.0.0.1" + expected_port ||
           (port == 80 && (header == "localhost" || header == "127.0.0.1"));
}

bool origin_is_same_loopback(const std::string& origin, unsigned short port) {
    if (origin.empty()) return true;
    const auto expected_port = ":" + std::to_string(port);
    if (origin == "http://127.0.0.1" + expected_port || origin == "http://localhost" + expected_port) return true;
    return port == 80 && (origin == "http://127.0.0.1" || origin == "http://localhost");
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string read_engine_log_tails(const RuntimeConfig& config) {
    constexpr std::uintmax_t max_bytes_per_log = 256U * 1024U;
    constexpr std::size_t max_lines_per_log = 120;
    const auto log_directory = runtime_logs_directory();
    std::ostringstream report;
    bool included_any = false;
    for (const auto& engine : config.engines) {
        if (!is_valid_engine_id(engine.id)) continue;
        const auto path = log_directory / (engine.id + ".log");
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) continue;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) continue;
        std::ifstream input(path, std::ios::binary);
        if (!input) continue;
        if (size > max_bytes_per_log) {
            input.seekg(static_cast<std::streamoff>(size - max_bytes_per_log), std::ios::beg);
            std::string discarded;
            std::getline(input, discarded); // the first line may start mid-record
        }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(std::move(line));
            if (lines.size() > max_lines_per_log) lines.erase(lines.begin());
        }
        if (lines.empty()) continue;
        included_any = true;
        report << "-- engine " << engine.id << " --\n";
        for (const auto& item : lines) report << sanitize_diagnostic_text(item) << '\n';
    }
    return included_any ? report.str() : std::string("No engine process log files were available.\n");
}

bool upstream_is_ready(const EngineStatus& engine);

std::string engine_status_json(const EngineStatus& engine) {
    const bool chat_capable = std::find(engine.config.capabilities.begin(), engine.config.capabilities.end(), "chat") != engine.config.capabilities.end();
    const bool model_missing = chat_capable && engine.config.model_path.empty();
    const bool ready = !model_missing && upstream_is_ready(engine);
    std::string readiness = "stopped";
    if (engine.config.executable_path.empty()) readiness = "not-configured";
    else if (model_missing) readiness = "model-not-configured";
    else if (engine.state == ProcessState::failed) readiness = "failed";
    else if (engine.state == ProcessState::running) readiness = ready ? "ready" : "loading-or-unavailable";
    std::ostringstream out;
    out << "{\"id\":" << json_quoted(engine.config.id)
        << ",\"display_name\":" << json_quoted(engine.config.display_name)
        << ",\"status\":" << json_quoted(to_string(engine.state))
        << ",\"pid\":" << engine.pid
        << ",\"exit_code\":" << engine.exit_code
        << ",\"last_error\":" << json_quoted(engine.last_error)
        << ",\"executable_configured\":" << (engine.config.executable_path.empty() ? "false" : "true")
        << ",\"model_configured\":" << (engine.config.model_path.empty() ? "false" : "true")
        << ",\"default_for_chat\":" << (engine.config.default_for_chat ? "true" : "false")
        << ",\"ready\":" << (ready ? "true" : "false")
        << ",\"readiness\":" << json_quoted(readiness)
        << ",\"upstream_base_url\":" << json_quoted(engine.config.upstream_base_url)
        << ",\"capabilities\":[";
    for (std::size_t i = 0; i < engine.config.capabilities.size(); ++i) {
        if (i) out << ',';
        out << json_quoted(engine.config.capabilities[i]);
    }
    out << "]}";
    return out.str();
}

http::response<http::string_body> json_response(http::status status, unsigned version,
                                                const std::string& body) {
    http::response<http::string_body> response{status, version};
    response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
    response.set(http::field::content_type, "application/json; charset=utf-8");
    response.set("X-Content-Type-Options", "nosniff");
    response.set("Cache-Control", "no-store");
    response.body() = body;
    response.keep_alive(false);
    response.prepare_payload();
    return response;
}

http::response<http::string_body> error_response(http::status status, unsigned version,
                                                  const std::string& message) {
    return json_response(status, version, "{\"error\":{\"message\":" + json_quoted(message) + "}}");
}

std::string engine_id_from_path(const std::string& path, const std::string& action) {
    const std::string prefix = "/v1/engines/";
    const std::string suffix = "/" + action;
    if (path.rfind(prefix, 0) != 0 || path.size() <= prefix.size() + suffix.size()) return {};
    if (path.substr(path.size() - suffix.size()) != suffix) return {};
    const auto id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
    return is_valid_engine_id(id) ? id : std::string{};
}

std::string upstream_port(const std::string& base_url) {
    const auto colon = base_url.find_last_of(':');
    if (colon == std::string::npos || colon + 1 >= base_url.size()) return {};
    return base_url.substr(colon + 1);
}

bool probe_status(const std::string& port_text, const std::string& target, unsigned int& status_code) {
    try {
        std::size_t consumed = 0;
        const auto port_value = std::stoul(port_text, &consumed);
        if (consumed != port_text.size() || port_value == 0 || port_value > 65535) return false;
        asio::io_context ioc;
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::milliseconds(450));
        stream.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port_value)));
        http::request<http::empty_body> request{http::verb::get, target, 11};
        request.set(http::field::host, "127.0.0.1:" + port_text);
        request.set(http::field::user_agent, "local-ai-runtime-readiness/" LAR_VERSION);
        request.keep_alive(false);
        http::write(stream, request);
        beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(4096);
        http::read(stream, buffer, parser);
        status_code = parser.get().result_int();
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
        return true;
    } catch (...) {
        return false;
    }
}

bool upstream_is_ready(const EngineStatus& engine) {
    if (engine.state != ProcessState::running || engine.config.upstream_base_url.empty()) return false;
    const auto port_text = upstream_port(engine.config.upstream_base_url);
    if (port_text.empty()) return false;
    unsigned int status_code = 0;
    if (!probe_status(port_text, "/health", status_code)) return false;
    if (status_code == 200) return true;
    // Some older OpenAI-compatible engines do not implement /health.
    if (status_code == 404 && probe_status(port_text, "/v1/models", status_code)) return status_code == 200;
    return false;
}

std::filesystem::path path_from_utf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char ch : value) encoded.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(encoded);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path require_absolute_regular_file(const std::string& raw, const std::string& field) {
    const auto text_first = raw.find_first_not_of(" \t\r\n");
    const auto text_last = raw.find_last_not_of(" \t\r\n");
    if (text_first == std::string::npos) throw std::runtime_error(field + " is required.");
    auto normalized = raw.substr(text_first, text_last - text_first + 1);
    // Windows Explorer's "Copy as path" includes surrounding quotes. Accept
    // that common format while still passing the path as a single argument.
    if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
        normalized = normalized.substr(1, normalized.size() - 2);
    }
    const auto path = path_from_utf8(normalized);
    if (!path.is_absolute()) throw std::runtime_error(field + " must be an absolute path.");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        throw std::runtime_error(field + " does not point to an existing regular file.");
    }
    return path;
}

EngineConfig build_llama_config(const boost::property_tree::ptree& body, const RuntimeConfig& runtime) {
    const auto executable = require_absolute_regular_file(body.get<std::string>("executable_path", ""), "llama-server executable path");
    const auto model = require_absolute_regular_file(body.get<std::string>("model_path", ""), "GGUF model path");
    const auto executable_name = lower_ascii(executable.filename().string());
#ifdef _WIN32
    if (executable_name != "llama-server.exe" && executable_name != "llama-server") {
        throw std::runtime_error("Choose the official llama-server.exe executable, not a different program.");
    }
#else
    if (executable_name != "llama-server") {
        throw std::runtime_error("Choose the official llama-server executable, not a different program.");
    }
#endif
    if (lower_ascii(model.extension().string()) != ".gguf") {
        throw std::runtime_error("Model path must point to a .gguf file.");
    }
    std::error_code size_error;
    const auto model_size = std::filesystem::file_size(model, size_error);
    if (size_error || model_size < 4) throw std::runtime_error("GGUF model file is empty or too small to contain a header.");
    {
        std::ifstream model_input(model, std::ios::binary);
        char magic[4]{};
        model_input.read(magic, sizeof(magic));
        if (model_input.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
            std::string_view(magic, sizeof(magic)) != "GGUF") {
            throw std::runtime_error("Selected model does not start with the GGUF file signature.");
        }
    }

    const auto port = body.get<unsigned int>("engine_port", 8081U);
    if (port < 1024U || port > 65535U) throw std::runtime_error("Engine port must be between 1024 and 65535.");
    if (port == runtime.port) throw std::runtime_error("Engine port must differ from the Local AI Runtime port.");
    const auto context = body.get<unsigned int>("context_size", 2048U);
    if (context < 512U || context > 32768U) throw std::runtime_error("Context size must be between 512 and 32768 tokens.");
    const auto backend = body.get<std::string>("backend", "cpu");
    if (backend != "cpu" && backend != "gpu") throw std::runtime_error("Backend must be 'cpu' or 'gpu'.");

    EngineConfig engine;
    engine.id = "llama-cpp";
    engine.display_name = "llama.cpp server";
    engine.executable_path = executable;
    engine.model_path = model;
    engine.working_directory = executable.parent_path();
    engine.args = {"--host", "127.0.0.1", "--port", std::to_string(port),
                   "--ctx-size", std::to_string(context), "--n-gpu-layers",
                   backend == "gpu" ? "auto" : "0", "-m", "{model_path}"};
    engine.upstream_base_url = "http://127.0.0.1:" + std::to_string(port);
    engine.capabilities = {"chat"};
    engine.default_for_chat = true;
    return engine;
}

struct ProxyResult {
    http::status status{http::status::bad_gateway};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body;
};

ProxyResult proxy_local_request(const EngineStatus& engine, http::verb method,
                                const std::string& target, const std::string& body) {
    ProxyResult result;
    if (engine.state != ProcessState::running) {
        result.status = http::status::service_unavailable;
        result.body = "{\"error\":{\"message\":\"Configured engine is not running. Start it from the dashboard first.\"}}";
        return result;
    }
    const auto port_text = upstream_port(engine.config.upstream_base_url);
    if (port_text.empty()) {
        result.body = "{\"error\":{\"message\":\"No valid local upstream_base_url is configured for this engine.\"}}";
        return result;
    }
    try {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::seconds(120));
        const auto endpoints = resolver.resolve("127.0.0.1", port_text);
        stream.connect(endpoints);
        http::request<http::string_body> request{method, target, 11};
        request.set(http::field::host, "127.0.0.1:" + port_text);
        request.set(http::field::user_agent, "local-ai-runtime/" LAR_VERSION);
        request.keep_alive(false);
        if (method == http::verb::post) request.set(http::field::content_type, "application/json");
        request.body() = body;
        request.prepare_payload();
        http::write(stream, request);

        beast::flat_buffer buffer;
        http::response_parser<http::string_body> parser;
        parser.body_limit(16U * 1024U * 1024U);
        http::read(stream, buffer, parser);
        auto upstream_response = parser.release();
        result.status = upstream_response.result();
        const auto content_type = upstream_response[http::field::content_type];
        if (!content_type.empty()) result.content_type = std::string(content_type.data(), content_type.size());
        result.body = std::move(upstream_response.body());
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    } catch (const std::exception& ex) {
        result.status = http::status::bad_gateway;
        result.body = "{\"error\":{\"message\":" + json_quoted(std::string("Could not reach local model server: ") + ex.what()) + "}}";
    }
    return result;
}

ProxyResult proxy_chat(const EngineStatus& engine, const std::string& body) {
    return proxy_local_request(engine, http::verb::post, "/v1/chat/completions", body);
}

ProxyResult proxy_models(const EngineStatus& engine) {
    return proxy_local_request(engine, http::verb::get, "/v1/models", {});
}

http::response<http::string_body> handle_request(const http::request<http::string_body>& request,
                                                  const RuntimeConfig& config,
                                                  EngineManager& engines,
                                                  DownloadManager& downloads,
                                                  const std::filesystem::path& web_dir) {
    const auto version = request.version();
    const std::string host = std::string(request[http::field::host].data(), request[http::field::host].size());
    if (!is_loopback_host(host, config.port)) return error_response(http::status::forbidden, version, "Host header is not allowed.");
    const std::string origin = std::string(request[http::field::origin].data(), request[http::field::origin].size());
    if (!origin_is_same_loopback(origin, config.port)) {
        return error_response(http::status::forbidden, version, "Cross-origin requests are disabled. Use the local dashboard origin.");
    }

    std::string target = std::string(request.target().data(), request.target().size());
    const auto query = target.find('?');
    const bool include_engine_logs = query != std::string::npos &&
        target.substr(query + 1).find("include_engine_logs=1") != std::string::npos;
    if (query != std::string::npos) target.resize(query);

    if (request.method() == http::verb::get && (target == "/" || target == "/index.html")) {
        const auto body = read_text_file(web_dir / "index.html");
        if (body.empty()) return error_response(http::status::not_found, version, "Dashboard file not found. Start from the repository root or pass --web-dir.");
        http::response<http::string_body> response{http::status::ok, version};
        response.set(http::field::content_type, "text/html; charset=utf-8");
        response.set("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self' https://api.github.com; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Referrer-Policy", "no-referrer");
        response.set("Cache-Control", "no-store");
        response.body() = body;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::get && (target == "/app.js" || target == "/style.css")) {
        const auto file = web_dir / (target == "/app.js" ? "app.js" : "style.css");
        const auto body = read_text_file(file);
        if (body.empty()) return error_response(http::status::not_found, version, "Dashboard asset not found.");
        http::response<http::string_body> response{http::status::ok, version};
        response.set(http::field::content_type, target == "/app.js" ? "text/javascript; charset=utf-8" : "text/css; charset=utf-8");
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Cache-Control", "no-store");
        response.body() = body;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::get && target == "/health") {
        return json_response(http::status::ok, version,
            "{\"status\":\"ok\",\"version\":" + json_quoted(LAR_VERSION) + ",\"bind\":\"loopback\"}");
    }
    if (request.method() == http::verb::get && target == "/version") {
        return json_response(http::status::ok, version,
            "{\"version\":" + json_quoted(LAR_VERSION) +
            ",\"repository\":\"https://github.com/azmisahin-ai/local-ai-runtime\"" +
            ",\"releases_api_url\":\"https://api.github.com/repos/azmisahin-ai/local-ai-runtime/releases?per_page=10\"" +
            ",\"releases_url\":\"https://github.com/azmisahin-ai/local-ai-runtime/releases\"}");
    }
    if (request.method() == http::verb::get && target == "/v1/diagnostics/export") {
        log_diagnostic("INFO", "diagnostics.export_requested", std::string("source=dashboard engine_logs=") + (include_engine_logs ? "included" : "omitted"));
        const auto statuses = engines.statuses();
        std::ostringstream engine_json;
        engine_json << "{\"data\":[";
        for (std::size_t i = 0; i < statuses.size(); ++i) {
            if (i) engine_json << ',';
            engine_json << engine_status_json(statuses[i]);
        }
        engine_json << "]}";
        const auto engine_logs = include_engine_logs ? read_engine_log_tails(config) : std::string{};
        const auto report = build_diagnostic_report(LAR_VERSION, downloads.jobs_json(), engine_json.str(), engine_logs);
        http::response<http::string_body> response{http::status::ok, version};
        response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
        response.set(http::field::content_type, "text/plain; charset=utf-8");
        response.set(http::field::content_disposition, "attachment; filename=\"local-ai-runtime-diagnostics.txt\"");
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Cache-Control", "no-store");
        response.body() = report;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::post && target == "/v1/diagnostics/events") {
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            auto event = body.get<std::string>("event", "ui.unknown");
            auto detail = body.get<std::string>("detail", "");
            if (event.size() > 64 || detail.size() > 1200 || event.rfind("ui.", 0) != 0) {
                return error_response(http::status::unprocessable_entity, version, "Diagnostic event is outside the allowed format.");
            }
            log_diagnostic("WARN", event, detail);
            return json_response(http::status::accepted, version, "{\"status\":\"recorded-locally\"}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version, std::string("Invalid diagnostic event JSON: ") + ex.what());
        }
    }
    if (request.method() == http::verb::get && target == "/v1/models") {
        const auto engine = engines.default_chat_engine();
        if (!engine) return error_response(http::status::service_unavailable, version, "No chat engine is configured. Set upstream_base_url in config/engines.json.");
        const auto proxied = proxy_models(*engine);
        http::response<http::string_body> response{proxied.status, version};
        response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
        response.set(http::field::content_type, proxied.content_type);
        response.set("X-Content-Type-Options", "nosniff");
        response.set("Cache-Control", "no-store");
        response.body() = proxied.body;
        response.keep_alive(false);
        response.prepare_payload();
        return response;
    }
    if (request.method() == http::verb::get && target == "/v1/catalog") {
        return json_response(http::status::ok, version, downloads.catalog_json());
    }
    if (request.method() == http::verb::get && target == "/v1/downloads") {
        return json_response(http::status::ok, version, downloads.jobs_json());
    }
    constexpr std::string_view download_prefix = "/v1/downloads/";
    if (target.rfind(download_prefix, 0) == 0 && target.size() > download_prefix.size()) {
        const auto download_id = target.substr(download_prefix.size());
        if (download_id.find('/') == std::string::npos && request.method() == http::verb::get) {
            const auto job = downloads.job_json(download_id);
            if (!job) return error_response(http::status::not_found, version, "Download job was not found.");
            return json_response(http::status::ok, version, *job);
        }
        if (download_id.find('/') == std::string::npos && request.method() == http::verb::delete_) {
            std::string error;
            if (!downloads.cancel(download_id, error)) return error_response(http::status::conflict, version, error);
            return json_response(http::status::accepted, version, "{\"id\":" + json_quoted(download_id) + ",\"status\":\"canceling\"}");
        }
        if (download_id.find('/') == std::string::npos && request.method() == http::verb::post) {
            try {
                std::istringstream input(request.body());
                boost::property_tree::ptree body;
                boost::property_tree::read_json(input, body);
                const bool accept_license = body.get<bool>("accept_license", false);
                std::string job_id;
                std::string error;
                if (!downloads.start(download_id, accept_license, job_id, error)) {
                    const auto status = error.find("already being installed") != std::string::npos || error.find("running engines") != std::string::npos
                        ? http::status::conflict : http::status::unprocessable_entity;
                    return error_response(status, version, error);
                }
                return json_response(http::status::accepted, version,
                    "{\"job_id\":" + json_quoted(job_id) + ",\"item_id\":" + json_quoted(download_id) + ",\"status\":\"queued\"}");
            } catch (const boost::property_tree::json_parser_error& ex) {
                return error_response(http::status::bad_request, version, std::string("Invalid download request JSON: ") + ex.what());
            }
        }
    }
    if (request.method() == http::verb::get && target == "/v1/engines") {
        const auto status_list = engines.statuses();
        std::ostringstream body;
        body << "{\"data\":[";
        for (std::size_t i = 0; i < status_list.size(); ++i) {
            if (i) body << ',';
            body << engine_status_json(status_list[i]);
        }
        body << "]}";
        return json_response(http::status::ok, version, body.str());
    }
    if (request.method() == http::verb::post && target == "/v1/setup/llama-cpp") {
        if (downloads.has_active_jobs()) {
            return error_response(http::status::conflict, version, "A component installation is active. Finish or cancel it before changing engine configuration.");
        }
        if (config.source_path.empty()) {
            return error_response(http::status::internal_server_error, version, "The active configuration path is unknown; setup cannot be persisted.");
        }
        try {
            std::istringstream input(request.body());
            boost::property_tree::ptree body;
            boost::property_tree::read_json(input, body);
            const auto engine = build_llama_config(body, config);
            auto candidate_engines = engines.configurations();
            for (auto& configured : candidate_engines) configured.default_for_chat = false;
            const auto found = std::find_if(candidate_engines.begin(), candidate_engines.end(), [](const EngineConfig& value) {
                return value.id == "llama-cpp";
            });
            if (found == candidate_engines.end()) candidate_engines.push_back(engine);
            else *found = engine;

            RuntimeConfig next_config = config;
            next_config.engines = candidate_engines;
            std::string error;
            const auto persist = [&](std::string& persist_error) {
                try {
                    save_config(config.source_path, next_config);
                    return true;
                } catch (const std::exception& ex) {
                    persist_error = std::string("Could not save engine configuration: ") + ex.what();
                    return false;
                }
            };
            if (!engines.replace_configs(std::move(candidate_engines), persist, error)) {
                return error_response(http::status::conflict, version, error);
            }
            return json_response(http::status::ok, version,
                "{\"id\":\"llama-cpp\",\"status\":\"configured\",\"backend\":" + json_quoted(body.get<std::string>("backend", "cpu")) +
                ",\"ready\":false,\"config_saved\":true}");
        } catch (const boost::property_tree::json_parser_error& ex) {
            return error_response(http::status::bad_request, version, std::string("Invalid JSON setup request: ") + ex.what());
        } catch (const std::exception& ex) {
            return error_response(http::status::unprocessable_entity, version, ex.what());
        }
    }

    if (request.method() == http::verb::post) {
        const auto start_id = engine_id_from_path(target, "start");
        if (!start_id.empty()) {
            if (downloads.has_active_jobs()) {
                return error_response(http::status::conflict, version, "A component installation is active. Wait for it to finish before starting an engine.");
            }
            std::string error;
            if (!engines.start(start_id, error)) return error_response(http::status::unprocessable_entity, version, error);
            return json_response(http::status::ok, version, "{\"id\":" + json_quoted(start_id) + ",\"status\":\"running\"}");
        }
        const auto stop_id = engine_id_from_path(target, "stop");
        if (!stop_id.empty()) {
            std::string error;
            if (!engines.stop(stop_id, error)) return error_response(http::status::unprocessable_entity, version, error);
            return json_response(http::status::ok, version, "{\"id\":" + json_quoted(stop_id) + ",\"status\":\"stopped\"}");
        }
        if (target == "/v1/chat/completions") {
            if (request.body().empty()) return error_response(http::status::bad_request, version, "Request body must contain a chat completion JSON object.");
            const auto engine = engines.default_chat_engine();
            if (!engine) return error_response(http::status::service_unavailable, version, "No chat engine is configured. Set upstream_base_url in config/engines.json.");
            const auto proxied = proxy_chat(*engine, request.body());
            http::response<http::string_body> response{proxied.status, version};
            response.set(http::field::server, "local-ai-runtime/" LAR_VERSION);
            response.set(http::field::content_type, proxied.content_type);
            response.set("X-Content-Type-Options", "nosniff");
            response.set("Cache-Control", "no-store");
            response.body() = proxied.body;
            response.keep_alive(false);
            response.prepare_payload();
            return response;
        }
    }
    if (request.method() == http::verb::options) {
        return error_response(http::status::method_not_allowed, version, "CORS preflight is not supported; use the same-origin local dashboard.");
    }
    return error_response(http::status::not_found, version, "Route not found.");
}

void serve_client(tcp::socket socket, RuntimeConfig config, EngineManager& engines,
                  DownloadManager& downloads, std::filesystem::path web_dir) {
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(std::chrono::seconds(130));
    beast::flat_buffer buffer;
    try {
        http::request_parser<http::string_body> parser;
        parser.body_limit(4U * 1024U * 1024U);
        http::read(stream, buffer, parser);
        auto request = parser.release();
        auto response = handle_request(request, config, engines, downloads, web_dir);
        http::write(stream, response);
        beast::error_code ignored;
        stream.socket().shutdown(tcp::socket::shutdown_send, ignored);
    } catch (const boost::system::system_error& ex) {
        if (ex.code() == http::error::body_limit) {
            auto response = error_response(http::status::payload_too_large, 11, "Request body exceeds the 4 MiB limit.");
            beast::error_code ignored;
            http::write(stream, response, ignored);
        }
        // Malformed or disconnected clients are closed without reflecting untrusted bytes.
    } catch (const std::exception& ex) {
        log_diagnostic("ERROR", "http.request_failed", ex.what());
        std::cerr << "Request handling error: " << ex.what() << '\n';
    }
}

} // namespace

void run_http_server(const RuntimeConfig& config, EngineManager& engines,
                     const std::filesystem::path& web_dir) {
    std::signal(SIGINT, request_shutdown);
    std::signal(SIGTERM, request_shutdown);
    shutdown_requested = 0;
    initialize_diagnostic_log(DownloadManager::user_data_directory(), LAR_VERSION);
    asio::io_context ioc{1};
    const auto address = asio::ip::make_address(config.host == "localhost" ? "127.0.0.1" : config.host);
    tcp::acceptor acceptor{ioc, tcp::endpoint{address, config.port}};
    acceptor.set_option(asio::socket_base::reuse_address(true));
    acceptor.non_blocking(true);
    DownloadManager downloads(config, engines);
    log_diagnostic("INFO", "runtime.started", "bind=loopback port=" + std::to_string(config.port));
    while (shutdown_requested == 0) {
        tcp::socket socket{ioc};
        boost::system::error_code ec;
        acceptor.accept(socket, ec);
        if (ec == asio::error::would_block || ec == asio::error::try_again) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (ec == asio::error::interrupted && shutdown_requested != 0) break;
        if (ec) throw boost::system::system_error(ec, "accept");
        std::thread(serve_client, std::move(socket), config, std::ref(engines), std::ref(downloads), web_dir).detach();
    }
    beast::error_code ignored;
    acceptor.close(ignored);
    log_diagnostic("INFO", "runtime.stopped", "shutdown=normal");
}

} // namespace lar
