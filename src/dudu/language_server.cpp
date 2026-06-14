#include "dudu/language_server.hpp"

#include "dudu/format.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dudu {
namespace {

struct Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::map<std::string, Json>;

struct Json {
    using Value = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

    Value value = nullptr;

    bool is_null() const {
        return std::holds_alternative<std::nullptr_t>(value);
    }
    const std::string* string() const {
        return std::get_if<std::string>(&value);
    }
    const JsonArray* array() const {
        return std::get_if<JsonArray>(&value);
    }
    const JsonObject* object() const {
        return std::get_if<JsonObject>(&value);
    }
    const Json* get(std::string_view key) const {
        const JsonObject* obj = object();
        if (obj == nullptr) {
            return nullptr;
        }
        const auto found = obj->find(std::string(key));
        return found == obj->end() ? nullptr : &found->second;
    }
};

class JsonParser {
  public:
    explicit JsonParser(std::string_view text) : text_(text) {
    }

    Json parse() {
        skip_ws();
        return parse_value();
    }

  private:
    std::string_view text_;
    size_t pos_ = 0;

    char peek() const {
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    char get() {
        return pos_ < text_.size() ? text_[pos_++] : '\0';
    }

    void skip_ws() {
        while (std::isspace(static_cast<unsigned char>(peek())) != 0) {
            ++pos_;
        }
    }

    void consume(std::string_view token) {
        if (text_.substr(pos_, token.size()) != token) {
            throw std::runtime_error("invalid json");
        }
        pos_ += token.size();
    }

    Json parse_value() {
        skip_ws();
        const char c = peek();
        if (c == '"') {
            return Json{parse_string()};
        }
        if (c == '{') {
            return Json{parse_object()};
        }
        if (c == '[') {
            return Json{parse_array()};
        }
        if (c == 't') {
            consume("true");
            return Json{true};
        }
        if (c == 'f') {
            consume("false");
            return Json{false};
        }
        if (c == 'n') {
            consume("null");
            return Json{nullptr};
        }
        return Json{parse_number()};
    }

    std::string parse_string() {
        if (get() != '"') {
            throw std::runtime_error("expected string");
        }
        std::string out;
        while (peek() != '\0') {
            const char c = get();
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            const char esc = get();
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                out.push_back(esc);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
                out += "?";
                pos_ += 4;
                break;
            default:
                throw std::runtime_error("invalid escape");
            }
        }
        throw std::runtime_error("unterminated string");
    }

    double parse_number() {
        const size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            ++pos_;
        }
        if (peek() == '.') {
            ++pos_;
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                ++pos_;
            }
        }
        return std::stod(std::string(text_.substr(start, pos_ - start)));
    }

    JsonArray parse_array() {
        JsonArray out;
        get();
        skip_ws();
        if (peek() == ']') {
            get();
            return out;
        }
        while (true) {
            out.push_back(parse_value());
            skip_ws();
            const char c = get();
            if (c == ']') {
                return out;
            }
            if (c != ',') {
                throw std::runtime_error("expected array comma");
            }
        }
    }

    JsonObject parse_object() {
        JsonObject out;
        get();
        skip_ws();
        if (peek() == '}') {
            get();
            return out;
        }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            if (get() != ':') {
                throw std::runtime_error("expected object colon");
            }
            out.emplace(std::move(key), parse_value());
            skip_ws();
            const char c = get();
            if (c == '}') {
                return out;
            }
            if (c != ',') {
                throw std::runtime_error("expected object comma");
            }
        }
    }
};

std::string json_escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string string_value(const Json* json) {
    if (json == nullptr) {
        return {};
    }
    const std::string* text = json->string();
    return text == nullptr ? std::string{} : *text;
}

std::string file_uri_to_path(std::string uri) {
    constexpr std::string_view prefix = "file://";
    if (uri.rfind(prefix, 0) == 0) {
        uri.erase(0, prefix.size());
    }
    std::string out;
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const std::string hex = uri.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(uri[i]);
        }
    }
    return out;
}

std::filesystem::path project_config_path(const std::filesystem::path& file) {
    std::filesystem::path dir = file.has_parent_path() ? file.parent_path() : ".";
    while (true) {
        const std::filesystem::path candidate = dir / "dudu.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            return {};
        }
        dir = dir.parent_path();
    }
}

ProjectConfig config_for_file(const std::filesystem::path& file) {
    const std::filesystem::path config = project_config_path(file);
    return config.empty() ? ProjectConfig{} : parse_project_config(config);
}

struct Document {
    std::string uri;
    std::filesystem::path path;
    std::string text;
};

struct Diagnostic {
    SourceLocation location;
    std::string message;
    std::string source;
    int severity = 1;
};

class LanguageServer {
  public:
    LanguageServer(std::istream& in, std::ostream& out, std::ostream& err)
        : in_(in), out_(out), err_(err) {
    }

    int run() {
        while (std::optional<std::string> body = read_message()) {
            try {
                handle_message(*body);
            } catch (const std::exception& error) {
                err_ << "dudu lsp: " << error.what() << '\n';
            }
            if (exit_) {
                return 0;
            }
        }
        return 0;
    }

  private:
    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    std::map<std::string, Document> documents_;
    bool shutdown_ = false;
    bool exit_ = false;

    std::optional<std::string> read_message() {
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

    void write_message(const std::string& body) {
        out_ << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        out_.flush();
    }

    void respond(const Json& id, const std::string& result) {
        std::ostringstream body;
        body << "{\"jsonrpc\":\"2.0\",\"id\":" << id_json(id) << ",\"result\":" << result << "}";
        write_message(body.str());
    }

    void notify(std::string_view method, const std::string& params) {
        std::ostringstream body;
        body << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method << "\",\"params\":" << params << "}";
        write_message(body.str());
    }

    static std::string id_json(const Json& id) {
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

    void handle_message(const std::string& body) {
        const Json message = JsonParser(body).parse();
        const Json* method_json = message.get("method");
        if (method_json == nullptr) {
            return;
        }
        const std::string method = string_value(method_json);
        const Json* id = message.get("id");
        const Json* params = message.get("params");

        if (method == "initialize") {
            if (id != nullptr) {
                respond(*id, initialize_result());
            }
        } else if (method == "shutdown") {
            shutdown_ = true;
            if (id != nullptr) {
                respond(*id, "null");
            }
        } else if (method == "exit") {
            exit_ = true;
        } else if (method == "textDocument/didOpen") {
            did_open(params);
        } else if (method == "textDocument/didChange") {
            did_change(params);
        } else if (method == "textDocument/didSave") {
            did_save(params);
        } else if (method == "textDocument/formatting") {
            if (id != nullptr) {
                respond(*id, formatting_result(params));
            }
        } else if (id != nullptr) {
            respond(*id, "null");
        }
        if (shutdown_ && method == "exit") {
            exit_ = true;
        }
    }

    static std::string initialize_result() {
        return "{\"capabilities\":{"
               "\"textDocumentSync\":2,"
               "\"documentFormattingProvider\":true"
               "},\"serverInfo\":{\"name\":\"duc lsp\",\"version\":\"0.1.0\"}}";
    }

    void did_open(const Json* params) {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        if (text_document == nullptr) {
            return;
        }
        const std::string uri = string_value(text_document->get("uri"));
        documents_[uri] = {.uri = uri,
                           .path = file_uri_to_path(uri),
                           .text = string_value(text_document->get("text"))};
        publish_diagnostics(uri);
    }

    void did_change(const Json* params) {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const Json* changes = params == nullptr ? nullptr : params->get("contentChanges");
        const JsonArray* array = changes == nullptr ? nullptr : changes->array();
        if (text_document == nullptr || array == nullptr || array->empty()) {
            return;
        }
        const std::string uri = string_value(text_document->get("uri"));
        Document& doc = documents_[uri];
        doc.uri = uri;
        doc.path = file_uri_to_path(uri);
        doc.text = string_value(array->back().get("text"));
        publish_diagnostics(uri);
    }

    void did_save(const Json* params) {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        if (text_document == nullptr) {
            return;
        }
        publish_diagnostics(string_value(text_document->get("uri")));
    }

    std::vector<Diagnostic> diagnostics_for(const Document& doc) {
        try {
            ModuleAst module = parse_source(doc.text, doc.path);
            const ProjectConfig config = config_for_file(doc.path);
            module.build_values = config.build_values;
            module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
            module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
            module.target_mode_explicit = config.target_mode_explicit;
            merge_native_header_types(module,
                                      {.config = config, .source_dir = doc.path.parent_path()});
            analyze_module(module, {.check_bodies = true});
            return {};
        } catch (const CompileError& error) {
            return {{.location = error.location(),
                     .message = error.what(),
                     .source = diagnostic_source(error.what()),
                     .severity = 1}};
        } catch (const std::exception& error) {
            return {{{.file = doc.path, .line = 1, .column = 1}, error.what(), "dudu/lsp", 1}};
        }
    }

    static std::string diagnostic_source(std::string_view message) {
        if (message.find("could not scan native header") != std::string_view::npos) {
            return "dudu/native-header";
        }
        if (message.find("return type mismatch") != std::string_view::npos ||
            message.find("cannot assign") != std::string_view::npos ||
            message.find("unknown identifier") != std::string_view::npos ||
            message.find("unknown type") != std::string_view::npos ||
            message.find("argument ") != std::string_view::npos) {
            return "dudu/sema";
        }
        if (message.find("unexpected") != std::string_view::npos ||
            message.find("expected newline") != std::string_view::npos ||
            message.find("expected indent") != std::string_view::npos ||
            message.find("expected identifier") != std::string_view::npos) {
            return "dudu/parser";
        }
        return "dudu/sema";
    }

    void publish_diagnostics(const std::string& uri) {
        const auto found = documents_.find(uri);
        if (found == documents_.end()) {
            return;
        }
        std::ostringstream params;
        params << "{\"uri\":\"" << json_escape(uri) << "\",\"diagnostics\":[";
        const std::vector<Diagnostic> diagnostics = diagnostics_for(found->second);
        for (size_t i = 0; i < diagnostics.size(); ++i) {
            if (i > 0) {
                params << ',';
            }
            params << diagnostic_json(diagnostics[i]);
        }
        params << "]}";
        notify("textDocument/publishDiagnostics", params.str());
    }

    static std::string diagnostic_json(const Diagnostic& diagnostic) {
        const int line = std::max(0, diagnostic.location.line - 1);
        const int column = std::max(0, diagnostic.location.column - 1);
        std::ostringstream out;
        out << "{\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << column
            << "},\"end\":{\"line\":" << line << ",\"character\":" << (column + 1)
            << "}},\"severity\":" << diagnostic.severity << ",\"source\":\""
            << json_escape(diagnostic.source) << "\",\"message\":\""
            << json_escape(diagnostic.message) << "\"}";
        return out.str();
    }

    std::string formatting_result(const Json* params) const {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const std::string uri =
            text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
        const auto found = documents_.find(uri);
        if (found == documents_.end()) {
            return "[]";
        }
        const std::string formatted = format_source(found->second.text);
        std::ostringstream out;
        out << "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":"
            << line_count(found->second.text) << ",\"character\":0}},\"newText\":\""
            << json_escape(formatted) << "\"}]";
        return out.str();
    }

    static int line_count(const std::string& text) {
        int lines = 0;
        for (const char c : text) {
            if (c == '\n') {
                ++lines;
            }
        }
        return lines + 1;
    }
};

} // namespace

int run_language_server(std::istream& in, std::ostream& out, std::ostream& err) {
    return LanguageServer(in, out, err).run();
}

} // namespace dudu
