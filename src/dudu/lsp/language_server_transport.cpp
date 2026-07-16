#include "dudu/lsp/language_server_transport.hpp"

#include "dudu/lsp/language_server_json.hpp"

#include <istream>
#include <ostream>
#include <sstream>
#include <variant>

namespace dudu {

LspTransport::LspTransport(std::istream& in, std::ostream& out) : in_(in), out_(out) {
}

std::optional<std::string> LspTransport::read_message() {
    std::string line;
    size_t content_length = 0;
    while (std::getline(in_, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        constexpr std::string_view header = "Content-Length:";
        if (line.rfind(header, 0) == 0) {
            content_length = static_cast<size_t>(std::stoul(line.substr(header.size())));
        }
    }
    if (content_length == 0) {
        return std::nullopt;
    }
    std::string body(content_length, '\0');
    in_.read(body.data(), static_cast<std::streamsize>(body.size()));
    if (in_.gcount() != static_cast<std::streamsize>(body.size())) {
        return std::nullopt;
    }
    return body;
}

void LspTransport::write_message(const std::string& body) {
    const std::lock_guard lock(output_mutex_);
    write_message_unlocked(body);
}

void LspTransport::write_message_unlocked(const std::string& body) {
    out_ << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out_.flush();
}

void LspTransport::respond(const Json& id, const std::string& result) {
    std::ostringstream body;
    body << "{\"jsonrpc\":\"2.0\",\"id\":" << id_json(id) << ",\"result\":" << result << "}";
    write_message(body.str());
}

void LspTransport::respond_error(const Json& id, int code, const std::string& message) {
    std::ostringstream body;
    body << "{\"jsonrpc\":\"2.0\",\"id\":" << id_json(id) << ",\"error\":{\"code\":" << code
         << ",\"message\":\"" << json_escape(message) << "\"}}";
    write_message(body.str());
}

void LspTransport::notify(std::string_view method, const std::string& params) {
    std::ostringstream body;
    body << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method << "\",\"params\":" << params << "}";
    write_message(body.str());
}

void LspTransport::request_client_refresh(std::string_view method) {
    const std::lock_guard lock(output_mutex_);
    std::ostringstream body;
    body << "{\"jsonrpc\":\"2.0\",\"id\":\"dudu-refresh-" << ++server_request_sequence_
         << "\",\"method\":\"" << method << "\",\"params\":null}";
    write_message_unlocked(body.str());
}

std::string LspTransport::id_json(const Json& id) {
    if (id.is_null()) {
        return "null";
    }
    if (const std::string* text = id.string()) {
        return "\"" + json_escape(*text) + "\"";
    }
    if (const double* number = std::get_if<double>(&id.value)) {
        std::ostringstream out;
        out << static_cast<long long>(*number);
        return out.str();
    }
    return "null";
}

} // namespace dudu
