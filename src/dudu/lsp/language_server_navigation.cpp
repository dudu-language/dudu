#include "dudu/lsp/language_server_navigation.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/lsp/language_server_json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace dudu {
namespace {

std::filesystem::path canonical_navigation_path(std::filesystem::path path) {
    if (path.is_relative()) {
        path = std::filesystem::absolute(path);
    }
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

} // namespace

std::string range_json(const SourceLocation& location) {
    const int line = std::max(0, location.line - 1);
    const int column = std::max(0, location.column - 1);
    return range_json(line, column, column + 1);
}

std::string range_json(const SourceRange& range) {
    return range_json(std::max(0, range.start.line - 1), std::max(0, range.start.column - 1),
                      std::max(0, range.end.line - 1), std::max(0, range.end.column - 1));
}

std::string range_json(int line, int start_character, int end_character) {
    return range_json(line, start_character, line, end_character);
}

std::string range_json(int start_line, int start_character, int end_line, int end_character) {
    std::ostringstream out;
    out << "{\"start\":{\"line\":" << start_line << ",\"character\":" << start_character
        << "},\"end\":{\"line\":" << end_line << ",\"character\":" << end_character << "}}";
    return out.str();
}

LspPosition lsp_position(const Json* params) {
    const Json* position = params == nullptr ? nullptr : params->get("position");
    return {.line = required_int_value(position == nullptr ? nullptr : position->get("line"),
                                       "position.line"),
            .character = required_int_value(
                position == nullptr ? nullptr : position->get("character"), "position.character")};
}

std::string location_json(const std::string& uri, const std::string& range) {
    return "{\"uri\":\"" + json_escape(uri) + "\",\"range\":" + range + "}";
}

std::string uri_for_location(const SourceLocation& location, const Document& doc) {
    if (location.file.empty() || same_path(location.file.str(), doc.path)) {
        return doc.uri;
    }
    return file_uri(location.file.str());
}

std::string file_uri(const std::filesystem::path& path) {
    return "file://" + canonical_navigation_path(path).string();
}

SourceLocation expr_name_location(const Expr& expr) {
    if (expr.kind == ExprKind::Member && !expr.children.empty() &&
        expr.children.front().range.end.column > 0) {
        SourceLocation location = range_end_location(expr.children.front().range);
        location.column += 1;
        return location;
    }
    return expr.location;
}

bool symbol_matches(const std::string& symbol, const std::string& query) {
    if (symbol == query) {
        return true;
    }
    const size_t dot = symbol.rfind('.');
    return dot != std::string::npos && symbol.substr(dot + 1) == query;
}

bool identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool valid_identifier(const std::string& value) {
    if (value.empty() ||
        (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_')) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), identifier_char);
}

bool same_path(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
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

bool skip_workspace_dir(const std::string& name) {
    return name == ".git" || name == "build" || name == ".dudu" || name == "node_modules" ||
           name == "vendor" || name == "third_party";
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace dudu
