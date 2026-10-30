#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <string>

namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

unsigned short parse_port(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "--port") {
        throw std::runtime_error("Usage: lar_fake_http_engine --port PORT");
    }
    const std::string text(argv[2]);
    std::size_t consumed = 0;
    const auto value = std::stoul(text, &consumed);
    if (consumed != text.size() || value == 0 || value > 65535) {
        throw std::runtime_error("Port must be an integer between 1 and 65535.");
    }
    return static_cast<unsigned short>(value);
}

void respond(tcp::socket& socket, const http::request<http::string_body>& request) {
    if (request.method() == http::verb::post && request.target() == "/v1/chat/completions" &&
        (request.body().find("\"stream\": true") != std::string::npos ||
         request.body().find("\"stream\":true") != std::string::npos)) {
        // Delay response headers for the explicit-cancel case to prove that
        // Activity cancellation also interrupts a slow prefill, not just an
        // already-started response-body stream.
        const bool explicit_cancel_case = request.body().find("cancel this") != std::string::npos;
        if (explicit_cancel_case) std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        http::response<http::empty_body> streaming_response{http::status::ok, request.version()};
        streaming_response.set(http::field::server, "lar-fake-http-engine");
        streaming_response.set(http::field::content_type, "text/event-stream; charset=utf-8");
        streaming_response.set("Cache-Control", "no-store");
        streaming_response.keep_alive(false);
        streaming_response.chunked(true);
        http::response_serializer<http::empty_body> serializer{streaming_response};
        boost::system::error_code header_error;
        http::write_header(socket, serializer, header_error);
        if (header_error) {
            socket.close(header_error);
            return;
        }
        // Simulate real model prefill latency before the first body token.
        if (!explicit_cancel_case) std::this_thread::sleep_for(std::chrono::milliseconds(450));
        const std::string chunks[] = {
            "data: {\"id\":\"chatcmpl-ci-stream\",\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\"}}]}\n\n",
            "data: {\"id\":\"chatcmpl-ci-stream\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"LAR_STREAM_SMOKE_OK\"}}]}\n\n",
            "data: [DONE]\n\n",
        };
        for (const auto& chunk : chunks) {
            std::ostringstream prefix;
            prefix << std::hex << chunk.size() << "\r\n";
            boost::system::error_code write_error;
            asio::write(socket, asio::buffer(prefix.str()), write_error);
            if (write_error) break;
            asio::write(socket, asio::buffer(chunk), write_error);
            if (write_error) break;
            asio::write(socket, asio::buffer("\r\n", 2), write_error);
            if (write_error) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
        boost::system::error_code write_error;
        asio::write(socket, asio::buffer("0\r\n\r\n", 5), write_error);
        beast::error_code ignored;
        socket.shutdown(tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
        return;
    }
    http::response<http::string_body> response{http::status::ok, request.version()};
    response.set(http::field::server, "lar-fake-http-engine");
    response.set(http::field::content_type, "application/json; charset=utf-8");
    response.keep_alive(false);

    if (request.method() == http::verb::get && request.target() == "/health") {
        response.body() = R"({"status":"ok"})";
    } else if (request.method() == http::verb::get && request.target() == "/v1/models") {
        response.body() = R"({"object":"list","data":[{"id":"fake-model","object":"model"}]})";
    } else if (request.method() == http::verb::post && request.target() == "/v1/chat/completions") {
        response.body() = R"({"id":"chatcmpl-ci-smoke","object":"chat.completion","choices":[{"index":0,"message":{"role":"assistant","content":"LAR_HTTP_SMOKE_OK"},"finish_reason":"stop"}]})";
    } else {
        response.result(http::status::not_found);
        response.body() = R"({"error":{"message":"not found"}})";
    }
    response.prepare_payload();
    beast::error_code ec;
    http::write(socket, response, ec);
    socket.shutdown(tcp::socket::shutdown_both, ec);
    socket.close(ec);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto port = parse_port(argc, argv);
        asio::io_context ioc{1};
        tcp::acceptor acceptor{ioc};
        const tcp::endpoint endpoint{asio::ip::make_address("127.0.0.1"), port};
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen(asio::socket_base::max_listen_connections);

        std::cout << "FAKE_ENGINE_READY host=127.0.0.1 port=" << port
                  << " cwd=" << std::filesystem::current_path().string() << std::endl;

        for (;;) {
            tcp::socket socket{ioc};
            beast::error_code ec;
            acceptor.accept(socket, ec);
            if (ec) {
                std::cerr << "accept failed: " << ec.message() << std::endl;
                continue;
            }
            beast::flat_buffer buffer;
            http::request<http::string_body> request;
            http::read(socket, buffer, request, ec);
            if (ec) {
                socket.close(ec);
                continue;
            }
            std::thread([client = std::move(socket), message = std::move(request)]() mutable {
                respond(client, message);
            }).detach();
        }
    } catch (const std::exception& ex) {
        std::cerr << "fake HTTP engine failed: " << ex.what() << std::endl;
        return 2;
    }
}
