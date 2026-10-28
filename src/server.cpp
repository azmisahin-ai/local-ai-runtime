#include "lar/server.hpp"

#ifndef LAR_VERSION
#define LAR_VERSION "0.1.0-dev"
#endif

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
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

std::string quoted(const std::string& value) { return "\"" + json_escape(value) + "\""; }

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

std::string engine_status_json(const EngineStatus& engine) {
    std::ostringstream out;
    out << "{\"id\":" << quoted(engine.config.id)
        << ",\"display_name\":" << quoted(engine.config.display_name)
        << ",\"status\":" << quoted(to_string(engine.state))
        << ",\"pid\":" << engine.pid
        << ",\"exit_code\":" << engine.exit_code
        << ",\"last_error\":" << quoted(engine.last_error)
        << ",\"executable_configured\":" << (engine.config.executable_path.empty() ? "false" : "true")
        << ",\"model_configured\":" << (engine.config.model_path.empty() ? "false" : "true")
        << ",\"upstream_base_url\":" << quoted(engine.config.upstream_base_url)
        << ",\"capabilities\":[";
    for (std::size_t i = 0; i < engine.config.capabilities.size(); ++i) {
        if (i) out << ',';
        out << quoted(engine.config.capabilities[i]);
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
    return json_response(status, version, "{\"error\":{\"message\":" + quoted(message) + "}}");
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
        result.body = "{\"error\":{\"message\":" + quoted(std::string("Could not reach local model server: ") + ex.what()) + "}}";
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
            "{\"status\":\"ok\",\"version\":" + quoted(LAR_VERSION) + ",\"bind\":\"loopback\"}");
    }
    if (request.method() == http::verb::get && target == "/version") {
        return json_response(http::status::ok, version,
            "{\"version\":" + quoted(LAR_VERSION) +
            ",\"repository\":\"https://github.com/azmisahin-ai/local-ai-runtime\"" +
            ",\"releases_api_url\":\"https://api.github.com/repos/azmisahin-ai/local-ai-runtime/releases?per_page=10\"" +
            ",\"releases_url\":\"https://github.com/azmisahin-ai/local-ai-runtime/releases\"}");
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
    if (request.method() == http::verb::post) {
        const auto start_id = engine_id_from_path(target, "start");
        if (!start_id.empty()) {
            std::string error;
            if (!engines.start(start_id, error)) return error_response(http::status::unprocessable_entity, version, error);
            return json_response(http::status::ok, version, "{\"id\":" + quoted(start_id) + ",\"status\":\"running\"}");
        }
        const auto stop_id = engine_id_from_path(target, "stop");
        if (!stop_id.empty()) {
            std::string error;
            if (!engines.stop(stop_id, error)) return error_response(http::status::unprocessable_entity, version, error);
            return json_response(http::status::ok, version, "{\"id\":" + quoted(stop_id) + ",\"status\":\"stopped\"}");
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
                  std::filesystem::path web_dir) {
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(std::chrono::seconds(130));
    beast::flat_buffer buffer;
    try {
        http::request_parser<http::string_body> parser;
        parser.body_limit(4U * 1024U * 1024U);
        http::read(stream, buffer, parser);
        auto request = parser.release();
        auto response = handle_request(request, config, engines, web_dir);
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
        std::cerr << "Request handling error: " << ex.what() << '\n';
    }
}

} // namespace

void run_http_server(const RuntimeConfig& config, EngineManager& engines,
                     const std::filesystem::path& web_dir) {
    std::signal(SIGINT, request_shutdown);
    std::signal(SIGTERM, request_shutdown);
    shutdown_requested = 0;
    asio::io_context ioc{1};
    const auto address = asio::ip::make_address(config.host == "localhost" ? "127.0.0.1" : config.host);
    tcp::acceptor acceptor{ioc, tcp::endpoint{address, config.port}};
    acceptor.set_option(asio::socket_base::reuse_address(true));
    acceptor.non_blocking(true);
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
        std::thread(serve_client, std::move(socket), config, std::ref(engines), web_dir).detach();
    }
    beast::error_code ignored;
    acceptor.close(ignored);
}

} // namespace lar
