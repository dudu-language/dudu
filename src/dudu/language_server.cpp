#include "dudu/language_server.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/format.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
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

int int_value(const Json* json, int fallback = 0) {
    if (json == nullptr) {
        return fallback;
    }
    if (const double* number = std::get_if<double>(&json->value)) {
        return static_cast<int>(*number);
    }
    return fallback;
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
    if (config.empty()) {
        return {};
    }
    ProjectConfig parsed = parse_project_config(config);
    const std::filesystem::path project_dir = config.parent_path();
    auto absolutize = [&](std::vector<std::string>& paths) {
        for (std::string& path_text : paths) {
            const std::filesystem::path path = path_text;
            if (!path.is_absolute()) {
                path_text = (project_dir / path).lexically_normal().string();
            }
        }
    };
    absolutize(parsed.include_dirs);
    absolutize(parsed.lib_dirs);
    return parsed;
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

struct Symbol {
    std::string name;
    std::string detail;
    SourceLocation location;
    int kind = 13;
};

struct ReferenceLocation {
    std::string uri;
    std::string range;
};

struct TextEdit {
    std::string range;
    std::string new_text;
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
    struct LintLocal {
        std::string name;
        std::string type;
        int line = 0;
        int column = 0;
        int indent = 0;
    };

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
        } else if (method == "textDocument/documentSymbol") {
            if (id != nullptr) {
                respond(*id, document_symbol_result(params));
            }
        } else if (method == "textDocument/definition") {
            if (id != nullptr) {
                respond(*id, definition_result(params));
            }
        } else if (method == "textDocument/references") {
            if (id != nullptr) {
                respond(*id, references_result(params));
            }
        } else if (method == "textDocument/rename") {
            if (id != nullptr) {
                respond(*id, rename_result(params));
            }
        } else if (method == "textDocument/codeAction") {
            if (id != nullptr) {
                respond(*id, code_action_result(params));
            }
        } else if (method == "textDocument/hover") {
            if (id != nullptr) {
                respond(*id, hover_result(params));
            }
        } else if (method == "textDocument/completion") {
            if (id != nullptr) {
                respond(*id, completion_result(params));
            }
        } else if (method == "completionItem/resolve") {
            if (id != nullptr) {
                respond(*id, completion_resolve_result(params));
            }
        } else if (method == "textDocument/signatureHelp") {
            if (id != nullptr) {
                respond(*id, signature_help_result(params));
            }
        } else if (method == "workspace/symbol") {
            if (id != nullptr) {
                respond(*id, workspace_symbol_result(params));
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
               "\"documentFormattingProvider\":true,"
               "\"documentSymbolProvider\":true,"
               "\"definitionProvider\":true,"
               "\"referencesProvider\":true,"
               "\"renameProvider\":true,"
               "\"codeActionProvider\":true,"
               "\"hoverProvider\":true,"
               "\"completionProvider\":{\"resolveProvider\":true,\"triggerCharacters\":[\".\"]},"
               "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},"
               "\"workspaceSymbolProvider\":true"
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
            ProjectConfig config;
            try {
                config = config_for_file(doc.path);
            } catch (const std::exception& error) {
                return {{{.file = doc.path, .line = 1, .column = 1},
                         error.what(),
                         "dudu/build-config",
                         1}};
            }
            if (const std::optional<std::string> missing = missing_pkg_config_package(config)) {
                return {{{.file = doc.path, .line = 1, .column = 1},
                         "missing pkg-config package: " + *missing,
                         "dudu/build-config",
                         1}};
            }
            module.build_values = config.build_values;
            module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
            module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
            module.target_mode_explicit = config.target_mode_explicit;
            merge_native_header_types(module,
                                      {.config = config, .source_dir = doc.path.parent_path()});
            analyze_module(module, {.check_bodies = true});
            return lint_diagnostics(doc);
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

    static std::optional<std::string> missing_pkg_config_package(const ProjectConfig& config) {
        const char* pkg_config = std::getenv("PKG_CONFIG");
        const std::string executable =
            pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
        for (const std::string& package : config.pkg_config_packages) {
            const std::string command = shell_quote_arg(executable) + " --exists " +
                                        shell_quote_arg(package) + " 2>/dev/null";
            if (std::system(command.c_str()) != 0) {
                return package;
            }
        }
        return std::nullopt;
    }

    static std::vector<Diagnostic> lint_diagnostics(const Document& doc) {
        std::vector<Diagnostic> out;
        std::vector<std::string> lines;
        std::vector<LintLocal> locals;
        std::vector<LintLocal> active_decls;
        static const std::regex local_decl(
            R"(^(\s*)([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_\.]*)\b)");
        static const std::regex def_decl(R"(^(\s*)def\s+[A-Za-z_][A-Za-z0-9_]*\(([^)]*)\))");
        std::istringstream in(doc.text);
        std::string line;
        int row = 0;
        std::optional<int> returned_indent;
        while (std::getline(in, line)) {
            lines.push_back(line);
            ++row;
            const std::string trimmed = trim_copy(line);
            if (trimmed.empty() || starts_with(trimmed, "#")) {
                continue;
            }
            const int indent = leading_spaces(line);
            while (!active_decls.empty() && active_decls.back().indent > indent) {
                active_decls.pop_back();
            }
            if (returned_indent && indent >= *returned_indent) {
                out.push_back({.location = {.file = doc.path, .line = row, .column = indent + 1},
                               .message = "unreachable statement after return",
                               .source = "dudu/lint",
                               .severity = 2});
                continue;
            }
            if (returned_indent && indent < *returned_indent) {
                returned_indent = std::nullopt;
            }
            if (starts_with(trimmed, "return") &&
                (trimmed.size() == 6 ||
                 std::isspace(static_cast<unsigned char>(trimmed[6])) != 0)) {
                returned_indent = indent;
            }
            std::smatch match;
            if (std::regex_search(line, match, def_decl)) {
                const std::vector<std::string> params = split_top_level_args(match[2].str());
                for (const std::string& param : params) {
                    const size_t colon = param.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    const std::string name = trim_copy(param.substr(0, colon));
                    const std::string type = trim_copy(param.substr(colon + 1));
                    if (!name.empty() && name != "self") {
                        active_decls.push_back({.name = name,
                                                .type = type,
                                                .line = row,
                                                .column = indent + 1,
                                                .indent = indent + 4});
                    }
                }
            }
            lint_suspicious_casts(line, active_decls, doc, row, out);
            if (starts_with(trimmed, "cpp(")) {
                out.push_back({.location = {.file = doc.path, .line = row, .column = indent + 1},
                               .message = "native interop hazard: raw cpp escape hatch",
                               .source = "dudu/lint",
                               .severity = 2});
            }
            if (std::regex_search(line, match, local_decl)) {
                const std::string name = match[2].str();
                const std::string type = trim_copy(match[3].str());
                for (const LintLocal& outer : active_decls) {
                    if (outer.name == name && outer.line != row && outer.indent <= indent) {
                        out.push_back(
                            {.location = {.file = doc.path, .line = row, .column = indent + 1},
                             .message = "local shadows outer binding: " + name,
                             .source = "dudu/lint",
                             .severity = 2});
                        break;
                    }
                }
                LintLocal local{.name = name,
                                .type = type,
                                .line = row,
                                .column = indent + 1,
                                .indent = indent};
                locals.push_back(local);
                active_decls.push_back(std::move(local));
            }
        }
        for (const LintLocal& local : locals) {
            bool used = false;
            for (size_t i = static_cast<size_t>(local.line); i < lines.size(); ++i) {
                if (contains_identifier(lines[i], local.name)) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                out.push_back(
                    {.location = {.file = doc.path, .line = local.line, .column = local.column},
                     .message = "unused local: " + local.name,
                     .source = "dudu/lint",
                     .severity = 2});
            }
        }
        return out;
    }

    static void lint_suspicious_casts(const std::string& line,
                                      const std::vector<LintLocal>& active_decls,
                                      const Document& doc, int row, std::vector<Diagnostic>& out) {
        static const std::regex cast_call(
            R"(\b(i8|i16|i32|i64|u8|u16|u32|u64|isize|usize|f32|f64)\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))");
        for (std::sregex_iterator it(line.begin(), line.end(), cast_call), end; it != end; ++it) {
            const std::string target = (*it)[1].str();
            const std::string source_name = (*it)[2].str();
            const std::string source_type = visible_local_type(active_decls, source_name);
            if (source_type.empty() || !is_suspicious_numeric_cast(target, source_type)) {
                continue;
            }
            out.push_back({.location = {.file = doc.path,
                                        .line = row,
                                        .column = static_cast<int>(it->position(0)) + 1},
                           .message = "suspicious narrowing cast: " + target + "(" + source_name +
                                      ") from " + source_type,
                           .source = "dudu/lint",
                           .severity = 2});
        }
    }

    static std::string visible_local_type(const std::vector<LintLocal>& active_decls,
                                          const std::string& name) {
        for (auto it = active_decls.rbegin(); it != active_decls.rend(); ++it) {
            if (it->name == name) {
                return it->type;
            }
        }
        return {};
    }

    static bool is_suspicious_numeric_cast(const std::string& target, std::string source) {
        source = trim_copy(std::move(source));
        if (target == source) {
            return false;
        }
        static const std::map<std::string, int> integer_bits = {
            {"i8", 8},   {"u8", 8},   {"i16", 16}, {"u16", 16},   {"i32", 32},
            {"u32", 32}, {"i64", 64}, {"u64", 64}, {"isize", 64}, {"usize", 64},
        };
        const bool source_float = source == "f32" || source == "f64";
        const auto source_integer = integer_bits.find(source);
        const auto target_integer = integer_bits.find(target);
        if (source_float && target_integer != integer_bits.end()) {
            return true;
        }
        if (source == "f64" && target == "f32") {
            return true;
        }
        if (source_integer != integer_bits.end() && target_integer != integer_bits.end() &&
            target_integer->second < source_integer->second) {
            return true;
        }
        if (source_integer != integer_bits.end() && target == "f32" &&
            source_integer->second > 24) {
            return true;
        }
        return false;
    }

    static bool contains_identifier(const std::string& line, const std::string& name) {
        for (size_t start = 0; start < line.size();) {
            if (!identifier_char(line[start])) {
                ++start;
                continue;
            }
            size_t end = start + 1;
            while (end < line.size() && identifier_char(line[end])) {
                ++end;
            }
            if (line.substr(start, end - start) == name) {
                return true;
            }
            start = end;
        }
        return false;
    }

    static int leading_spaces(const std::string& line) {
        int out = 0;
        while (out < static_cast<int>(line.size()) && line[static_cast<size_t>(out)] == ' ') {
            ++out;
        }
        return out;
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

    std::vector<Symbol> symbols_for(const Document& doc, bool include_native = true) const {
        std::vector<Symbol> out;
        try {
            ModuleAst module = parse_source(doc.text, doc.path);
            if (include_native) {
                const ProjectConfig config = config_for_file(doc.path);
                merge_native_header_types(module,
                                          {.config = config, .source_dir = doc.path.parent_path()});
            }
            for (const ClassDecl& klass : module.classes) {
                out.push_back({.name = klass.name,
                               .detail = "class " + klass.name,
                               .location = klass.location,
                               .kind = 5});
                for (const FieldDecl& field : klass.fields) {
                    out.push_back({.name = field.name,
                                   .detail = field.name + ": " + field.type,
                                   .location = field.location,
                                   .kind = 8});
                }
                for (const FunctionDecl& method : klass.methods) {
                    out.push_back({.name = method.name,
                                   .detail = function_detail(method),
                                   .location = method.location,
                                   .kind = method.name == "__init__" ? 9 : 6});
                }
            }
            for (const EnumDecl& en : module.enums) {
                out.push_back({.name = en.name,
                               .detail = "enum " + en.name,
                               .location = en.location,
                               .kind = 10});
            }
            for (const ConstDecl& constant : module.constants) {
                out.push_back({.name = constant.name,
                               .detail = constant.name + ": " + constant.type,
                               .location = constant.location,
                               .kind = 14});
            }
            for (const FunctionDecl& fn : module.functions) {
                out.push_back({.name = fn.name,
                               .detail = function_detail(fn),
                               .location = fn.location,
                               .kind = 12});
            }
            if (!include_native) {
                return out;
            }
            for (const NativeTypeDecl& type : module.native_types) {
                out.push_back(
                    {.name = type.name,
                     .detail = type.type.empty() ? "native type" : "native type = " + type.type,
                     .location = type.location,
                     .kind = 23});
            }
            for (const NativeValueDecl& value : module.native_values) {
                out.push_back({.name = value.name,
                               .detail = value.name + ": " + value.type,
                               .location = value.location,
                               .kind = 14});
            }
            for (const NativeMacroDecl& macro : module.native_macros) {
                out.push_back({.name = macro.name,
                               .detail = native_macro_detail(macro),
                               .location = macro.location,
                               .kind = macro.function_like ? 3 : 14});
            }
            for (const NativeFunctionDecl& fn : module.native_functions) {
                out.push_back({.name = fn.name,
                               .detail = native_function_detail(fn),
                               .location = fn.location,
                               .kind = 12});
            }
            for (const ClassDecl& klass : module.native_classes) {
                out.push_back({.name = klass.name,
                               .detail = "native class " + klass.name,
                               .location = klass.location,
                               .kind = 5});
                for (const FunctionDecl& method : klass.methods) {
                    out.push_back({.name = klass.name + "." + method.name,
                                   .detail = function_detail(method),
                                   .location = method.location,
                                   .kind = method.name == "__init__" ? 9 : 6});
                }
            }
        } catch (const std::exception&) {
        }
        return out;
    }

    static std::string function_detail(const FunctionDecl& fn) {
        std::ostringstream out;
        out << "def " << fn.name << "(";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << fn.params[i].name << ": " << fn.params[i].type;
        }
        out << ")";
        if (!fn.return_type.empty()) {
            out << " -> " << fn.return_type;
        }
        return out.str();
    }

    static std::string native_macro_detail(const NativeMacroDecl& macro) {
        if (!macro.function_like) {
            return "macro " + macro.name;
        }
        std::ostringstream out;
        out << "macro " << macro.name << "(";
        for (int i = 0; i < macro.arity; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << "arg" << i;
        }
        out << ")";
        return out.str();
    }

    static std::string native_function_detail(const NativeFunctionDecl& fn) {
        std::ostringstream out;
        out << fn.name << "(";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << fn.params[i];
        }
        if (fn.variadic) {
            if (!fn.params.empty()) {
                out << ", ";
            }
            out << "...";
        }
        out << ") -> " << fn.return_type;
        return out.str();
    }

    std::string document_symbol_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        std::ostringstream out;
        out << "[";
        const std::vector<Symbol> symbols = symbols_for(*doc);
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << symbol_json(symbols[i], *doc);
        }
        out << "]";
        return out.str();
    }

    static std::string symbol_json(const Symbol& symbol, const Document& doc) {
        std::ostringstream out;
        out << "{\"name\":\"" << json_escape(symbol.name) << "\",\"kind\":" << symbol.kind
            << ",\"detail\":\"" << json_escape(symbol.detail) << "\",\"location\":{\"uri\":\""
            << json_escape(uri_for_location(symbol.location, doc))
            << "\",\"range\":" << range_json(symbol.location) << "}}";
        return out.str();
    }

    std::string workspace_symbol_result(const Json* params) const {
        const std::string query =
            lower_copy(params == nullptr ? std::string{} : string_value(params->get("query")));
        std::ostringstream out;
        out << "[";
        bool first = true;
        const std::map<std::string, Document> workspace = workspace_documents();
        for (const auto& [uri, doc] : workspace) {
            (void)uri;
            const bool include_native = documents_.contains(uri);
            for (const Symbol& symbol : symbols_for(doc, include_native)) {
                if (!query.empty() && lower_copy(symbol.name).find(query) == std::string::npos) {
                    continue;
                }
                if (!first) {
                    out << ",";
                }
                first = false;
                out << symbol_json(symbol, doc);
            }
        }
        out << "]";
        return out.str();
    }

    std::string definition_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        if (const std::optional<std::string> header = header_definition_result(*doc, params)) {
            return *header;
        }
        const std::string word = symbol_at(*doc, params);
        for (const Symbol& symbol : symbols_for(*doc)) {
            if (symbol_matches(symbol.name, word)) {
                std::ostringstream out;
                out << "{\"uri\":\"" << json_escape(uri_for_location(symbol.location, *doc))
                    << "\",\"range\":" << range_json(symbol.location) << "}";
                return out.str();
            }
        }
        if (const std::optional<std::string> member_definition =
                member_definition_result(*doc, word)) {
            return *member_definition;
        }
        if (const std::optional<std::string> import_definition =
                import_definition_result(*doc, word)) {
            return *import_definition;
        }
        return "null";
    }

    std::optional<std::string> header_definition_result(const Document& doc,
                                                        const Json* params) const {
        const Json* position = params == nullptr ? nullptr : params->get("position");
        const int target_line = int_value(position == nullptr ? nullptr : position->get("line"));
        const int target_character =
            int_value(position == nullptr ? nullptr : position->get("character"));
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row != target_line) {
                continue;
            }
            const std::optional<std::string> header = header_import_at(line, target_character);
            if (!header) {
                return std::nullopt;
            }
            const ProjectConfig config = config_for_file(doc.path);
            const std::optional<std::filesystem::path> resolved =
                resolve_header_path(doc.path.parent_path(), config, *header);
            if (!resolved) {
                return std::nullopt;
            }
            return location_json(file_uri(*resolved), range_json(0, 0, 0));
        }
        return std::nullopt;
    }

    static std::optional<std::string> header_import_at(const std::string& line, int character) {
        static const std::regex import_regex(R"DD(^\s*import\s+(?:c|cpp)\s+"([^"]+)")DD");
        std::smatch match;
        if (!std::regex_search(line, match, import_regex)) {
            return std::nullopt;
        }
        const int start = static_cast<int>(match.position(1));
        const int end = start + static_cast<int>(match.length(1));
        return character >= start && character <= end ? std::optional<std::string>{match[1].str()}
                                                      : std::nullopt;
    }

    static std::optional<std::filesystem::path>
    resolve_header_path(const std::filesystem::path& source_dir, const ProjectConfig& config,
                        const std::string& header) {
        const std::filesystem::path header_path = header;
        std::vector<std::filesystem::path> roots;
        if (header_path.is_absolute()) {
            roots.push_back({});
        } else {
            roots.push_back(source_dir);
            for (const std::string& include_dir : config.include_dirs) {
                const std::filesystem::path path = include_dir;
                roots.push_back(path.is_absolute() ? path : source_dir / path);
            }
            for (const std::filesystem::path& include_dir : pkg_config_include_dirs(config)) {
                roots.push_back(include_dir);
            }
        }
        for (std::filesystem::path root : roots) {
            std::filesystem::path candidate = root.empty() ? header_path : root / header_path;
            std::error_code error;
            if (std::filesystem::exists(candidate, error) && !error) {
                const std::filesystem::path resolved =
                    std::filesystem::weakly_canonical(candidate, error);
                return error ? candidate.lexically_normal() : resolved;
            }
        }
        return std::nullopt;
    }

    static std::vector<std::filesystem::path> pkg_config_include_dirs(const ProjectConfig& config) {
        if (config.pkg_config_packages.empty()) {
            return {};
        }
        const char* pkg_config = std::getenv("PKG_CONFIG");
        const std::string executable =
            pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
        std::string command = shell_quote_arg(executable) + " --cflags";
        for (const std::string& package : config.pkg_config_packages) {
            command += " " + shell_quote_arg(package);
        }
        command += " 2>/dev/null";
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe == nullptr) {
            return {};
        }
        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        if (pclose(pipe) != 0) {
            return {};
        }
        std::vector<std::filesystem::path> out;
        std::istringstream flags(output);
        std::string flag;
        while (flags >> flag) {
            if (starts_with(flag, "-I") && flag.size() > 2) {
                out.emplace_back(flag.substr(2));
            }
        }
        return out;
    }

    std::optional<std::string> member_definition_result(const Document& doc,
                                                        const std::string& word) const {
        const size_t dot = word.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= word.size()) {
            return std::nullopt;
        }
        const std::string receiver = word.substr(0, dot);
        const std::string member = word.substr(dot + 1);
        const std::string type = local_type_before_cursor(doc, receiver);
        if (type.empty()) {
            return std::nullopt;
        }
        try {
            ModuleAst module = parse_source(doc.text, doc.path);
            const ProjectConfig config = config_for_file(doc.path);
            merge_native_header_types(module,
                                      {.config = config, .source_dir = doc.path.parent_path()});
            const std::set<std::string> candidate_types = member_candidate_types(module, type);
            const auto find_member =
                [&](const std::vector<ClassDecl>& classes) -> std::optional<std::string> {
                for (const ClassDecl& klass : classes) {
                    if (!candidate_types.contains(klass.name)) {
                        continue;
                    }
                    for (const FieldDecl& field : klass.fields) {
                        if (field.name == member) {
                            return location_json(uri_for_location(field.location, doc),
                                                 range_json(field.location));
                        }
                    }
                    for (const FunctionDecl& method : klass.methods) {
                        if (method.name == member) {
                            return location_json(uri_for_location(method.location, doc),
                                                 range_json(method.location));
                        }
                    }
                }
                return std::nullopt;
            };
            if (const std::optional<std::string> native = find_member(module.native_classes)) {
                return native;
            }
            return find_member(module.classes);
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }

    std::optional<std::string> import_definition_result(const Document& doc,
                                                        const std::string& word) const {
        if (word.empty()) {
            return std::nullopt;
        }
        const std::string root = word.substr(0, word.find('.'));
        try {
            const ModuleAst module = parse_source(doc.text, doc.path);
            for (const ImportDecl& import : module.imports) {
                if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
                    continue;
                }
                if (import.kind == ImportKind::Module && bound_import_name(import) != root &&
                    import.module_path != root) {
                    continue;
                }
                if (import.kind == ImportKind::From && bound_import_name(import) != word) {
                    continue;
                }
                const std::filesystem::path file =
                    module_path_to_file(doc.path.parent_path(), import.module_path);
                std::error_code error;
                if (!std::filesystem::exists(file, error) || error) {
                    continue;
                }
                if (import.kind == ImportKind::Module) {
                    return location_json(file_uri(file), range_json(0, 0, 0));
                }
                std::ifstream input(file);
                if (!input) {
                    continue;
                }
                const std::string text{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
                const Document imported{
                    .uri = file_uri(file),
                    .path = file,
                    .text = text,
                };
                for (const Symbol& symbol : symbols_for(imported, false)) {
                    if (symbol.name == import.imported_name) {
                        return location_json(uri_for_location(symbol.location, imported),
                                             range_json(symbol.location));
                    }
                }
            }
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }

    std::string references_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        const std::string query = symbol_at(*doc, params);
        if (query.empty()) {
            return "[]";
        }
        std::ostringstream out;
        out << "[";
        bool first = true;
        const std::map<std::string, Document> workspace = workspace_documents();
        for (const auto& [uri, candidate] : workspace) {
            (void)uri;
            for (const ReferenceLocation& location : references_in(candidate, query)) {
                if (!first) {
                    out << ",";
                }
                first = false;
                out << location_json(location.uri, location.range);
            }
        }
        out << "]";
        return out.str();
    }

    std::string rename_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        const std::string old_name = symbol_at(*doc, params);
        const std::string new_name =
            params == nullptr ? std::string{} : string_value(params->get("newName"));
        if (!valid_identifier(new_name) || !renameable_symbol(*doc, old_name)) {
            return "null";
        }
        std::ostringstream out;
        out << "{\"changes\":{";
        bool first_doc = true;
        const std::map<std::string, Document> workspace = workspace_documents();
        for (const auto& [uri, candidate] : workspace) {
            (void)uri;
            const std::vector<ReferenceLocation> locations = references_in(candidate, old_name);
            if (locations.empty()) {
                continue;
            }
            if (!first_doc) {
                out << ",";
            }
            first_doc = false;
            out << "\"" << json_escape(candidate.uri) << "\":[";
            for (size_t i = 0; i < locations.size(); ++i) {
                if (i > 0) {
                    out << ",";
                }
                out << "{\"range\":" << locations[i].range << ",\"newText\":\""
                    << json_escape(new_name) << "\"}";
            }
            out << "]";
        }
        out << "}}";
        return out.str();
    }

    std::string code_action_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "[]";
        }
        std::vector<std::string> actions;
        actions.push_back("{\"title\":\"Format document\",\"kind\":\"source.format\","
                          "\"command\":{\"title\":\"Format document\","
                          "\"command\":\"editor.action.formatDocument\"}}");
        if (const std::optional<TextEdit> edit = organize_imports_edit(*doc)) {
            actions.push_back("{\"title\":\"Organize imports\",\"kind\":\"source.organizeImports\","
                              "\"edit\":{\"changes\":{\"" +
                              json_escape(doc->uri) + "\":[{\"range\":" + edit->range +
                              ",\"newText\":\"" + json_escape(edit->new_text) + "\"}]}}}");
        }
        for (const std::string& action : missing_import_actions(*doc, params)) {
            actions.push_back(action);
        }
        for (const std::string& action : lint_actions(*doc, params)) {
            actions.push_back(action);
        }
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < actions.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << actions[i];
        }
        out << "]";
        return out.str();
    }

    std::string hover_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "null";
        }
        const std::string word = symbol_at(*doc, params);
        for (const Symbol& symbol : symbols_for(*doc)) {
            if (symbol_matches(symbol.name, word)) {
                std::string markdown = "`" + symbol.detail + "`";
                if (uri_for_location(symbol.location, *doc) == doc->uri) {
                    if (const std::string docs = doc_comment_before(*doc, symbol.location.line);
                        !docs.empty()) {
                        markdown += "\n\n" + docs;
                    }
                }
                return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
                       "\"},\"range\":" + range_json(symbol.location) + "}";
            }
        }
        if (const std::string type = local_type_before_cursor(*doc, word, params); !type.empty()) {
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`" + json_escape(word) + ": " +
                   json_escape(type) + "`\"}}";
        }
        return "null";
    }

    static std::string doc_comment_before(const Document& doc, int one_based_line) {
        std::vector<std::string> lines;
        std::istringstream in(doc.text);
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        int row = one_based_line - 2;
        std::vector<std::string> comments;
        while (row >= 0 && row < static_cast<int>(lines.size())) {
            const std::string trimmed = trim_copy(lines[static_cast<size_t>(row)]);
            if (!starts_with(trimmed, "#")) {
                break;
            }
            comments.push_back(trim_copy(trimmed.substr(1)));
            --row;
        }
        std::reverse(comments.begin(), comments.end());
        std::ostringstream out;
        for (size_t i = 0; i < comments.size(); ++i) {
            if (i > 0) {
                out << "\n";
            }
            out << comments[i];
        }
        return out.str();
    }

    std::string completion_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc != nullptr) {
            if (const std::optional<std::string> member_target =
                    member_completion_target(*doc, params)) {
                return member_completion_result(*doc, *member_target);
            }
        }
        std::ostringstream out;
        out << "[";
        bool first = true;
        const auto add = [&](std::string_view label, int kind, std::string_view detail) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind
                << ",\"detail\":\"" << json_escape(detail) << "\"}";
        };
        const auto add_snippet = [&](std::string_view label, std::string_view detail,
                                     std::string_view insert_text) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":15,\"detail\":\""
                << json_escape(detail) << "\",\"insertText\":\"" << json_escape(insert_text)
                << "\",\"insertTextFormat\":2}";
        };
        for (std::string_view keyword :
             {"class", "def", "enum", "import", "return", "for", "if", "elif", "else", "while",
              "try", "except", "True", "False", "None"}) {
            add(keyword, 14, "keyword");
        }
        add_snippet("def", "snippet", "def ${1:name}(${2:args}) -> ${3:i32}:\n    ${0:return 0}");
        add_snippet("class", "snippet", "class ${1:Name}:\n    ${0:field: i32}");
        add_snippet("if", "snippet", "if ${1:condition}:\n    ${0:pass}");
        add_snippet("for", "snippet", "for ${1:item}: ${2:i32} in ${3:items}:\n    ${0:pass}");
        add_snippet("while", "snippet", "while ${1:condition}:\n    ${0:pass}");
        add_snippet("enum", "snippet", "enum ${1:Name}:\n    ${0:VALUE}");
        add_snippet("import", "snippet", "import ${1:module}");
        add_snippet("from", "snippet", "from ${1:module} import ${2:symbol}");
        add_snippet("try", "snippet",
                    "try:\n    ${1:pass}\nexcept ${2:Exception} as ${3:error}:\n    ${0:pass}");
        add_snippet("except", "snippet", "except ${1:Exception} as ${2:error}:\n    ${0:pass}");
        for (std::string_view type : {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                                      "isize", "usize", "f32", "f64", "str", "cstr"}) {
            add(type, 25, "type");
        }
        if (doc != nullptr) {
            for (const auto& [name, type] : local_types_before_cursor(*doc, params)) {
                add(name, 6, name + ": " + type);
            }
            for (const Symbol& symbol : symbols_for(*doc)) {
                add(symbol.name, completion_kind(symbol.kind), symbol.detail);
            }
        }
        out << "]";
        return out.str();
    }

    std::string completion_resolve_result(const Json* params) const {
        const std::string label =
            params == nullptr ? std::string{} : string_value(params->get("label"));
        const int kind = int_value(params == nullptr ? nullptr : params->get("kind"));
        const std::string detail =
            params == nullptr ? std::string{} : string_value(params->get("detail"));
        std::ostringstream out;
        out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind;
        if (!detail.empty()) {
            out << ",\"detail\":\"" << json_escape(detail) << "\"";
        }
        if (const Json* insert_text = params == nullptr ? nullptr : params->get("insertText");
            insert_text != nullptr && insert_text->string() != nullptr) {
            out << ",\"insertText\":\"" << json_escape(*insert_text->string()) << "\"";
        }
        if (const int insert_format =
                int_value(params == nullptr ? nullptr : params->get("insertTextFormat"));
            insert_format != 0) {
            out << ",\"insertTextFormat\":" << insert_format;
        }
        out << ",\"documentation\":{\"kind\":\"markdown\",\"value\":\""
            << json_escape(completion_documentation(label, detail)) << "\"}}";
        return out.str();
    }

    std::string member_completion_result(const Document& doc, const std::string& target) const {
        if (const std::optional<std::string> module_result =
                module_completion_result(doc, target)) {
            return *module_result;
        }
        const std::string type = local_type_before_cursor(doc, target);
        if (type.empty()) {
            return "[]";
        }
        std::ostringstream out;
        out << "[";
        bool first = true;
        const auto add = [&](std::string_view label, int kind, std::string_view detail) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind
                << ",\"detail\":\"" << json_escape(detail) << "\"}";
        };
        try {
            ModuleAst module = parse_source(doc.text, doc.path);
            const ProjectConfig config = config_for_file(doc.path);
            merge_native_header_types(module,
                                      {.config = config, .source_dir = doc.path.parent_path()});
            const std::set<std::string> candidate_types = member_candidate_types(module, type);
            for (const ClassDecl& klass : module.classes) {
                if (!candidate_types.contains(klass.name)) {
                    continue;
                }
                for (const FieldDecl& field : klass.fields) {
                    add(field.name, 5, field.name + ": " + field.type);
                }
                for (const ConstDecl& constant : klass.constants) {
                    add(constant.name, 21, constant.name + ": " + constant.type);
                }
                for (const ConstDecl& field : klass.static_fields) {
                    add(field.name, 5, field.name + ": " + field.type);
                }
                for (const FunctionDecl& method : klass.methods) {
                    add(method.name, method.name == "__init__" ? 4 : 2, function_detail(method));
                }
                break;
            }
            for (const ClassDecl& klass : module.native_classes) {
                if (!candidate_types.contains(klass.name)) {
                    continue;
                }
                for (const FieldDecl& field : klass.fields) {
                    add(field.name, 5, field.name + ": " + field.type);
                }
                for (const FunctionDecl& method : klass.methods) {
                    add(method.name, method.name == "__init__" ? 4 : 2, function_detail(method));
                }
                break;
            }
        } catch (const std::exception&) {
        }
        out << "]";
        return out.str();
    }

    std::optional<std::string> module_completion_result(const Document& doc,
                                                        const std::string& target) const {
        ModuleAst module;
        try {
            module = parse_source(doc.text, doc.path);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        for (const ImportDecl& import : module.imports) {
            if (import.kind != ImportKind::Module || bound_import_name(import) != target) {
                continue;
            }
            const std::filesystem::path file =
                module_path_to_file(doc.path.parent_path(), import.module_path);
            std::ifstream input(file);
            if (!input) {
                return "[]";
            }
            const std::string text{std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>()};
            const Document imported{
                .uri = file_uri(file),
                .path = file,
                .text = text,
            };
            std::ostringstream out;
            out << "[";
            bool first = true;
            for (const Symbol& symbol : symbols_for(imported, false)) {
                if (!first) {
                    out << ",";
                }
                first = false;
                out << "{\"label\":\"" << json_escape(symbol.name)
                    << "\",\"kind\":" << completion_kind(symbol.kind) << ",\"detail\":\""
                    << json_escape(symbol.detail) << "\"}";
            }
            out << "]";
            return out.str();
        }
        return std::nullopt;
    }

    static std::set<std::string> member_candidate_types(const ModuleAst& module,
                                                        const std::string& type) {
        std::set<std::string> out{type};
        bool changed = true;
        while (changed) {
            changed = false;
            for (const NativeTypeDecl& alias : module.native_types) {
                if (!alias.type.empty() && out.contains(alias.name) &&
                    out.insert(alias.type).second) {
                    changed = true;
                }
            }
            for (const TypeAliasDecl& alias : module.aliases) {
                if (!alias.type.empty() && out.contains(alias.name) &&
                    out.insert(alias.type).second) {
                    changed = true;
                }
            }
        }
        return out;
    }

    static int completion_kind(int symbol_kind) {
        switch (symbol_kind) {
        case 5:
            return 7;
        case 6:
            return 2;
        case 8:
            return 5;
        case 9:
            return 4;
        case 10:
            return 13;
        case 12:
            return 3;
        case 14:
            return 21;
        default:
            return 6;
        }
    }

    struct CallSite {
        std::string name;
        int parameter = 0;
    };

    std::string signature_help_result(const Json* params) const {
        const Document* doc = document_from_params(params);
        if (doc == nullptr) {
            return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
        }
        const CallSite call = call_site_at(*doc, params);
        if (call.name.empty()) {
            return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
        }
        std::vector<std::string> signatures;
        for (const Symbol& symbol : symbols_for(*doc)) {
            if (symbol_matches(symbol.name, call.name) && (symbol.kind == 12 || symbol.kind == 6)) {
                signatures.push_back(symbol.detail);
            }
        }
        std::ostringstream out;
        out << "{\"signatures\":[";
        for (size_t i = 0; i < signatures.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << "{\"label\":\"" << json_escape(signatures[i]) << "\"}";
        }
        out << "],\"activeSignature\":0,\"activeParameter\":" << call.parameter << "}";
        return out.str();
    }

    CallSite call_site_at(const Document& doc, const Json* params) const {
        const Json* position = params == nullptr ? nullptr : params->get("position");
        const int target_line = int_value(position == nullptr ? nullptr : position->get("line"));
        const int target_character =
            int_value(position == nullptr ? nullptr : position->get("character"));
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row != target_line) {
                continue;
            }
            const int cursor = std::min(target_character, static_cast<int>(line.size()));
            int depth = 0;
            int parameter = 0;
            for (int i = cursor - 1; i >= 0; --i) {
                const char c = line[static_cast<size_t>(i)];
                if (c == ')') {
                    ++depth;
                } else if (c == '(') {
                    if (depth > 0) {
                        --depth;
                        continue;
                    }
                    int end = i;
                    while (end > 0 && std::isspace(static_cast<unsigned char>(
                                          line[static_cast<size_t>(end - 1)])) != 0) {
                        --end;
                    }
                    int start = end;
                    while (start > 0 && symbol_char(line[static_cast<size_t>(start - 1)])) {
                        --start;
                    }
                    return {
                        line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)),
                        parameter};
                } else if (c == ',' && depth == 0) {
                    ++parameter;
                }
            }
        }
        return {};
    }

    const Document* document_from_params(const Json* params) const {
        const Json* text_document = params == nullptr ? nullptr : params->get("textDocument");
        const std::string uri =
            text_document == nullptr ? std::string{} : string_value(text_document->get("uri"));
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    static std::string range_json(const SourceLocation& location) {
        const int line = std::max(0, location.line - 1);
        const int column = std::max(0, location.column - 1);
        return range_json(line, column, column + 1);
    }

    static std::string range_json(int line, int start_character, int end_character) {
        return range_json(line, start_character, line, end_character);
    }

    static std::string range_json(int start_line, int start_character, int end_line,
                                  int end_character) {
        std::ostringstream out;
        out << "{\"start\":{\"line\":" << start_line << ",\"character\":" << start_character
            << "},\"end\":{\"line\":" << end_line << ",\"character\":" << end_character << "}}";
        return out.str();
    }

    std::string symbol_at(const Document& doc, const Json* params) const {
        const Json* position = params == nullptr ? nullptr : params->get("position");
        const int target_line = int_value(position == nullptr ? nullptr : position->get("line"));
        const int target_character =
            int_value(position == nullptr ? nullptr : position->get("character"));
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row != target_line) {
                continue;
            }
            int start = std::min(target_character, static_cast<int>(line.size()));
            while (start > 0 && symbol_char(line[static_cast<size_t>(start - 1)])) {
                --start;
            }
            int end = std::min(target_character, static_cast<int>(line.size()));
            while (end < static_cast<int>(line.size()) &&
                   symbol_char(line[static_cast<size_t>(end)])) {
                ++end;
            }
            return start < end
                       ? line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start))
                       : std::string{};
        }
        return {};
    }

    static bool symbol_matches(const std::string& symbol, const std::string& query) {
        if (symbol == query) {
            return true;
        }
        const size_t dot = symbol.rfind('.');
        return dot != std::string::npos && symbol.substr(dot + 1) == query;
    }

    static bool symbol_char(char c) {
        return identifier_char(c) || c == '.';
    }

    std::optional<std::string> member_completion_target(const Document& doc,
                                                        const Json* params) const {
        const Json* position = params == nullptr ? nullptr : params->get("position");
        const int target_line = int_value(position == nullptr ? nullptr : position->get("line"));
        const int target_character =
            int_value(position == nullptr ? nullptr : position->get("character"));
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row != target_line) {
                continue;
            }
            int cursor = std::min(target_character, static_cast<int>(line.size()));
            while (cursor > 0 && identifier_char(line[static_cast<size_t>(cursor - 1)])) {
                --cursor;
            }
            if (cursor <= 0 || line[static_cast<size_t>(cursor - 1)] != '.') {
                return std::nullopt;
            }
            int end = cursor - 1;
            while (end > 0 && std::isspace(static_cast<unsigned char>(
                                  line[static_cast<size_t>(end - 1)])) != 0) {
                --end;
            }
            int start = end;
            while (start > 0 && identifier_char(line[static_cast<size_t>(start - 1)])) {
                --start;
            }
            if (start == end) {
                return std::nullopt;
            }
            return line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
        }
        return std::nullopt;
    }

    static std::string local_type_before_cursor(const Document& doc, const std::string& name,
                                                const Json* params = nullptr) {
        const std::map<std::string, std::string> locals = local_types_before_cursor(doc, params);
        const auto found = locals.find(name);
        return found == locals.end() ? std::string{} : found->second;
    }

    static std::map<std::string, std::string> local_types_before_cursor(const Document& doc,
                                                                        const Json* params) {
        static const std::regex local_decl(
            R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_\.]*)\b)");
        static const std::regex assign_decl(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^=].*)$)");
        static const std::regex def_decl(R"(^\s*def\s+[A-Za-z_][A-Za-z0-9_]*\(([^)]*)\))");
        const Json* position = params == nullptr ? nullptr : params->get("position");
        const int max_line = position == nullptr ? std::numeric_limits<int>::max()
                                                 : int_value(position->get("line"));
        const int target_indent =
            params == nullptr ? std::numeric_limits<int>::max() : target_line_indent(doc, max_line);
        std::map<std::string, std::string> out;
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row > max_line) {
                break;
            }
            std::smatch match;
            if (std::regex_search(line, match, def_decl)) {
                for (const std::string& param : split_top_level_args(match[1].str())) {
                    const size_t colon = param.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    const std::string name = trim_copy(param.substr(0, colon));
                    const std::string type = trim_copy(param.substr(colon + 1));
                    if (!name.empty() && !type.empty()) {
                        out[name] = type;
                    }
                }
            }
            if (std::regex_search(line, match, local_decl)) {
                if (leading_spaces(line) > target_indent) {
                    continue;
                }
                out[match[1].str()] = match[2].str();
            } else if (std::regex_search(line, match, assign_decl)) {
                if (leading_spaces(line) > target_indent) {
                    continue;
                }
                const std::string inferred = infer_simple_assignment_type(match[2].str());
                if (!inferred.empty()) {
                    out[match[1].str()] = inferred;
                }
            }
        }
        return out;
    }

    static std::string infer_simple_assignment_type(std::string expr) {
        expr = trim_copy(std::move(expr));
        if (expr == "True" || expr == "False") {
            return "bool";
        }
        if (expr.size() >= 2 && ((expr.front() == '"' && expr.back() == '"') ||
                                 (expr.front() == '\'' && expr.back() == '\''))) {
            return "str";
        }
        if (is_numeric_literal_text(expr)) {
            return expr.find('.') == std::string::npos ? "i32" : "f64";
        }
        const size_t call = expr.find('(');
        if (call != std::string::npos && call > 0 &&
            std::isupper(static_cast<unsigned char>(expr.front())) != 0) {
            return trim_copy(expr.substr(0, call));
        }
        return {};
    }

    static bool is_numeric_literal_text(const std::string& expr) {
        if (expr.empty()) {
            return false;
        }
        const size_t start = (expr.front() == '-' || expr.front() == '+') ? 1 : 0;
        bool digit = false;
        bool dot = false;
        for (size_t i = start; i < expr.size(); ++i) {
            const char c = expr[i];
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                digit = true;
                continue;
            }
            if (c == '.' && !dot) {
                dot = true;
                continue;
            }
            return false;
        }
        return digit;
    }

    static int target_line_indent(const Document& doc, int target_line) {
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            if (row == target_line) {
                return leading_spaces(line);
            }
        }
        return std::numeric_limits<int>::max();
    }

    static std::string completion_documentation(const std::string& label,
                                                const std::string& detail) {
        if (detail == "snippet") {
            return "Dudu snippet for `" + label + "`.";
        }
        if (detail == "keyword") {
            return "Dudu keyword `" + label + "`.";
        }
        if (detail == "type") {
            return "Built-in Dudu type `" + label + "`.";
        }
        if (!detail.empty()) {
            return detail;
        }
        return label;
    }

    static std::optional<TextEdit> organize_imports_edit(const Document& doc) {
        std::istringstream in(doc.text);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        size_t start = 0;
        while (start < lines.size() && trim_copy(lines[start]).empty()) {
            ++start;
        }
        size_t end = start;
        std::vector<std::string> imports;
        while (end < lines.size()) {
            const std::string trimmed = trim_copy(lines[end]);
            if (!(starts_with(trimmed, "import ") || starts_with(trimmed, "from "))) {
                break;
            }
            imports.push_back(lines[end]);
            ++end;
        }
        if (imports.size() < 2) {
            return std::nullopt;
        }
        std::vector<std::string> sorted = imports;
        std::sort(sorted.begin(), sorted.end(), [](const std::string& lhs, const std::string& rhs) {
            return trim_copy(lhs) < trim_copy(rhs);
        });
        if (sorted == imports) {
            return std::nullopt;
        }
        std::ostringstream replacement;
        for (const std::string& import : sorted) {
            replacement << import << "\n";
        }
        return TextEdit{.range = range_json(static_cast<int>(start), 0, static_cast<int>(end), 0),
                        .new_text = replacement.str()};
    }

    std::vector<std::string> missing_import_actions(const Document& doc, const Json* params) const {
        std::vector<std::string> out;
        const Json* context = params == nullptr ? nullptr : params->get("context");
        const Json* diagnostics = context == nullptr ? nullptr : context->get("diagnostics");
        const JsonArray* array = diagnostics == nullptr ? nullptr : diagnostics->array();
        if (array == nullptr) {
            return out;
        }
        std::set<std::string> seen;
        for (const Json& diagnostic : *array) {
            const std::string message = string_value(diagnostic.get("message"));
            const std::optional<std::string> name = missing_identifier(message);
            if (!name || !seen.insert(*name).second) {
                continue;
            }
            const std::optional<std::string> action = missing_import_action(doc, *name);
            if (action) {
                out.push_back(*action);
            }
        }
        return out;
    }

    std::vector<std::string> lint_actions(const Document& doc, const Json* params) const {
        std::vector<std::string> out;
        const Json* context = params == nullptr ? nullptr : params->get("context");
        const Json* diagnostics = context == nullptr ? nullptr : context->get("diagnostics");
        const JsonArray* array = diagnostics == nullptr ? nullptr : diagnostics->array();
        if (array == nullptr) {
            return out;
        }
        std::set<int> seen_lines;
        for (const Json& diagnostic : *array) {
            if (string_value(diagnostic.get("source")) != "dudu/lint") {
                continue;
            }
            const std::string message = string_value(diagnostic.get("message"));
            const bool unreachable = message == "unreachable statement after return";
            const bool unused_local = starts_with(message, "unused local: ");
            if (!unreachable && !unused_local) {
                continue;
            }
            const Json* range = diagnostic.get("range");
            const Json* start = range == nullptr ? nullptr : range->get("start");
            const int line = int_value(start == nullptr ? nullptr : start->get("line"), -1);
            if (line < 0 || !seen_lines.insert(line).second) {
                continue;
            }
            const std::string title =
                unused_local ? "Remove unused local" : "Remove unreachable statement";
            if (const std::optional<std::string> action = remove_line_action(doc, line, title)) {
                out.push_back(*action);
            }
        }
        return out;
    }

    static std::optional<std::string> remove_line_action(const Document& doc, int line,
                                                         const std::string& title) {
        std::vector<std::string> lines;
        std::istringstream in(doc.text);
        std::string text_line;
        while (std::getline(in, text_line)) {
            lines.push_back(text_line);
        }
        if (line < 0 || line >= static_cast<int>(lines.size())) {
            return std::nullopt;
        }
        const bool has_next = line + 1 <= line_count(doc.text);
        const std::string range =
            has_next ? range_json(line, 0, line + 1, 0)
                     : range_json(line, 0, line,
                                  static_cast<int>(lines[static_cast<size_t>(line)].size()));
        return "{\"title\":\"" + json_escape(title) +
               "\",\"kind\":\"quickfix\","
               "\"edit\":{\"changes\":{\"" +
               json_escape(doc.uri) + "\":[{\"range\":" + range + ",\"newText\":\"\"}]}}}";
    }

    static std::string join_lines(const std::vector<std::string>& lines, bool trailing_newline) {
        std::ostringstream out;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) {
                out << "\n";
            }
            out << lines[i];
        }
        if (trailing_newline) {
            out << "\n";
        }
        return out.str();
    }

    std::optional<std::string> missing_import_action(const Document& doc,
                                                     const std::string& name) const {
        const std::map<std::string, Document> workspace = workspace_documents();
        std::optional<Document> match_doc;
        std::optional<Symbol> match_symbol;
        for (const auto& [uri, candidate] : workspace) {
            (void)uri;
            if (same_path(candidate.path, doc.path)) {
                continue;
            }
            for (const Symbol& symbol : symbols_for(candidate, false)) {
                if (symbol.name != name || !importable_symbol_kind(symbol.kind)) {
                    continue;
                }
                if (match_symbol) {
                    return std::nullopt;
                }
                match_doc = candidate;
                match_symbol = symbol;
            }
        }
        if (!match_doc || !match_symbol) {
            return std::nullopt;
        }
        const std::optional<std::string> module_path =
            module_path_for_import(doc.path.parent_path(), match_doc->path);
        if (!module_path || import_already_present(doc, *module_path, name)) {
            return std::nullopt;
        }
        const int line = static_cast<int>(import_insertion_line(doc));
        const std::string edit_text = "from " + *module_path + " import " + name + "\n";
        return "{\"title\":\"Import " + json_escape(name) + " from " + json_escape(*module_path) +
               "\",\"kind\":\"quickfix\",\"edit\":{\"changes\":{\"" + json_escape(doc.uri) +
               "\":[{\"range\":" + range_json(line, 0, line, 0) + ",\"newText\":\"" +
               json_escape(edit_text) + "\"}]}}}";
    }

    static std::optional<std::string> missing_identifier(const std::string& message) {
        constexpr std::string_view prefix = "unknown identifier: ";
        const size_t start = message.find(prefix);
        if (start == std::string::npos) {
            return std::nullopt;
        }
        std::string name = trim_copy(message.substr(start + prefix.size()));
        const size_t end = name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        if (end != std::string::npos) {
            name = name.substr(0, end);
        }
        return valid_identifier(name) ? std::optional<std::string>{name} : std::nullopt;
    }

    static bool importable_symbol_kind(int kind) {
        return kind == 5 || kind == 10 || kind == 12 || kind == 14 || kind == 23;
    }

    static size_t import_insertion_line(const Document& doc) {
        std::istringstream in(doc.text);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        size_t current = 0;
        while (current < lines.size() && trim_copy(lines[current]).empty()) {
            ++current;
        }
        while (current < lines.size()) {
            const std::string trimmed = trim_copy(lines[current]);
            if (!(starts_with(trimmed, "import ") || starts_with(trimmed, "from "))) {
                break;
            }
            ++current;
        }
        return current;
    }

    static bool import_already_present(const Document& doc, const std::string& module_path,
                                       const std::string& name) {
        std::istringstream in(doc.text);
        std::string line;
        while (std::getline(in, line)) {
            const std::string trimmed = trim_copy(line);
            if (trimmed == "import " + module_path ||
                starts_with(trimmed, "import " + module_path + " as ") ||
                starts_with(trimmed, "from " + module_path + " import ")) {
                return trimmed.find(name) != std::string::npos ||
                       starts_with(trimmed, "import " + module_path);
            }
        }
        return false;
    }

    static std::optional<std::string> module_path_for_import(const std::filesystem::path& base,
                                                             const std::filesystem::path& file) {
        std::error_code error;
        std::filesystem::path relative = std::filesystem::relative(file, base, error);
        if (error || relative.empty() || relative.is_absolute()) {
            relative = file.filename();
        }
        relative.replace_extension();
        if (relative.empty()) {
            return std::nullopt;
        }
        std::vector<std::string> parts;
        for (const std::filesystem::path& part : relative) {
            const std::string text = part.string();
            if (text.empty() || text == ".") {
                continue;
            }
            if (!valid_identifier(text)) {
                return std::nullopt;
            }
            parts.push_back(text);
        }
        if (parts.empty()) {
            return std::nullopt;
        }
        std::ostringstream out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << ".";
            }
            out << parts[i];
        }
        return out.str();
    }

    static bool same_path(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        std::error_code error;
        const std::filesystem::path left = std::filesystem::weakly_canonical(lhs, error);
        if (error) {
            error.clear();
            return lhs.lexically_normal() == rhs.lexically_normal();
        }
        const std::filesystem::path right = std::filesystem::weakly_canonical(rhs, error);
        if (error) {
            error.clear();
            return lhs.lexically_normal() == rhs.lexically_normal();
        }
        return left == right;
    }

    static bool valid_identifier(const std::string& value) {
        if (value.empty() || (std::isalpha(static_cast<unsigned char>(value.front())) == 0 &&
                              value.front() != '_')) {
            return false;
        }
        return std::all_of(value.begin() + 1, value.end(), identifier_char);
    }

    static bool identifier_char(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    bool renameable_symbol(const Document& doc, const std::string& name) const {
        if (name.empty() || name.find('.') != std::string::npos) {
            return false;
        }
        for (const Symbol& symbol : symbols_for(doc)) {
            if (symbol.name == name && std::filesystem::path(symbol.location.file) == doc.path &&
                (symbol.kind == 5 || symbol.kind == 6 || symbol.kind == 8 || symbol.kind == 10 ||
                 symbol.kind == 12 || symbol.kind == 14)) {
                return true;
            }
        }
        return false;
    }

    std::map<std::string, Document> workspace_documents() const {
        std::map<std::string, Document> out = documents_;
        std::set<std::filesystem::path> open_paths;
        std::set<std::filesystem::path> roots;
        for (const auto& [uri, doc] : documents_) {
            (void)uri;
            std::error_code error;
            open_paths.insert(std::filesystem::weakly_canonical(doc.path, error));
            const std::filesystem::path config = project_config_path(doc.path);
            roots.insert(config.empty() ? doc.path.parent_path() : config.parent_path());
        }
        size_t scanned = 0;
        std::set<std::filesystem::path> visiting;
        std::vector<Document> seed_documents;
        for (const auto& [uri, doc] : out) {
            (void)uri;
            seed_documents.push_back(doc);
        }
        for (const Document& doc : seed_documents) {
            collect_imported_documents(doc, open_paths, out, visiting, scanned);
        }
        for (const std::filesystem::path& root : roots) {
            collect_workspace_documents(root, open_paths, out, scanned);
        }
        return out;
    }

    static void collect_imported_documents(const Document& doc,
                                           const std::set<std::filesystem::path>& open_paths,
                                           std::map<std::string, Document>& out,
                                           std::set<std::filesystem::path>& visiting,
                                           size_t& scanned) {
        if (scanned >= 1000) {
            return;
        }
        ModuleAst module;
        try {
            module = parse_source(doc.text, doc.path);
        } catch (const std::exception&) {
            return;
        }
        for (const ImportDecl& import : module.imports) {
            if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
                continue;
            }
            const std::filesystem::path path =
                module_path_to_file(doc.path.parent_path(), import.module_path);
            std::error_code error;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
            if (error || open_paths.contains(canonical) || visiting.contains(canonical)) {
                error.clear();
                continue;
            }
            std::ifstream file(path);
            if (!file) {
                continue;
            }
            const std::string text{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
            const std::string uri = file_uri(path);
            const Document imported{.uri = uri, .path = path, .text = text};
            out.try_emplace(uri, imported);
            ++scanned;
            visiting.insert(canonical);
            collect_imported_documents(imported, open_paths, out, visiting, scanned);
            visiting.erase(canonical);
            if (scanned >= 1000) {
                return;
            }
        }
    }

    static void collect_workspace_documents(const std::filesystem::path& root,
                                            const std::set<std::filesystem::path>& open_paths,
                                            std::map<std::string, Document>& out, size_t& scanned) {
        std::error_code error;
        if (root.empty() || !std::filesystem::exists(root, error) || error) {
            return;
        }
        for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::skip_permission_denied, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            const std::filesystem::directory_entry& entry = *it;
            const std::filesystem::path path = entry.path();
            if (entry.is_directory(error) && skip_workspace_dir(path.filename().string())) {
                it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file(error) || path.extension() != ".dd") {
                continue;
            }
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
            if (error || open_paths.contains(canonical)) {
                error.clear();
                continue;
            }
            std::ifstream file(path);
            if (!file) {
                continue;
            }
            const std::string text{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
            const std::string uri = file_uri(path);
            out[uri] = {.uri = uri, .path = path, .text = text};
            if (++scanned >= 1000) {
                return;
            }
        }
    }

    static bool skip_workspace_dir(const std::string& name) {
        return name == ".git" || name == "build" || name == ".dudu" || name == "node_modules" ||
               name == "vendor" || name == "third_party";
    }

    static std::string uri_for_location(const SourceLocation& location, const Document& doc) {
        if (location.file.empty() || std::filesystem::path(location.file) == doc.path) {
            return doc.uri;
        }
        std::filesystem::path path = location.file;
        if (path.is_relative()) {
            path = std::filesystem::absolute(path);
        }
        return "file://" + path.lexically_normal().string();
    }

    static std::string file_uri(const std::filesystem::path& path) {
        std::filesystem::path absolute = path;
        if (absolute.is_relative()) {
            absolute = std::filesystem::absolute(absolute);
        }
        return "file://" + absolute.lexically_normal().string();
    }

    static std::filesystem::path module_path_to_file(const std::filesystem::path& base,
                                                     const std::string& module_path) {
        std::filesystem::path out = base;
        size_t start = 0;
        while (start < module_path.size()) {
            const size_t dot = module_path.find('.', start);
            const size_t end = dot == std::string::npos ? module_path.size() : dot;
            out /= module_path.substr(start, end - start);
            if (dot == std::string::npos) {
                break;
            }
            start = dot + 1;
        }
        out += ".dd";
        return out;
    }

    static std::vector<ReferenceLocation> references_in(const Document& doc,
                                                        const std::string& query) {
        std::vector<ReferenceLocation> out;
        std::istringstream in(doc.text);
        std::string line;
        for (int row = 0; std::getline(in, line); ++row) {
            for (int start = 0; start < static_cast<int>(line.size());) {
                if (!symbol_char(line[static_cast<size_t>(start)])) {
                    ++start;
                    continue;
                }
                int end = start + 1;
                while (end < static_cast<int>(line.size()) &&
                       symbol_char(line[static_cast<size_t>(end)])) {
                    ++end;
                }
                if (line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)) ==
                    query) {
                    out.push_back({doc.uri, range_json(row, start, end)});
                }
                start = end;
            }
        }
        return out;
    }

    static std::string location_json(const std::string& uri, const std::string& range) {
        return "{\"uri\":\"" + json_escape(uri) + "\",\"range\":" + range + "}";
    }

    static std::string lower_copy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
};

} // namespace

int run_language_server(std::istream& in, std::ostream& out, std::ostream& err) {
    return LanguageServer(in, out, err).run();
}

} // namespace dudu
