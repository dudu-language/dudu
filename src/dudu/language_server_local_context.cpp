#include "dudu/language_server_local_context.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"

#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace dudu {
namespace {

int target_line_indent(const Document& doc, int target_line) {
    std::istringstream in(doc.text);
    std::string line;
    for (int row = 0; std::getline(in, line); ++row) {
        if (row == target_line) {
            return leading_spaces(line);
        }
    }
    return std::numeric_limits<int>::max();
}

bool is_numeric_literal_text(const std::string& expr) {
    if (expr.empty()) {
        return false;
    }
    size_t start = (expr.front() == '-' || expr.front() == '+') ? 1 : 0;
    if (start == expr.size()) {
        return false;
    }
    auto valid_digit = [](char c, int base) {
        if (c == '_') {
            return true;
        }
        if (base <= 10) {
            return c >= '0' && c < static_cast<char>('0' + base);
        }
        return std::isdigit(static_cast<unsigned char>(c)) != 0 ||
               (c >= 'a' && c < static_cast<char>('a' + base - 10)) ||
               (c >= 'A' && c < static_cast<char>('A' + base - 10));
    };
    int base = 10;
    if (start + 1 < expr.size() && expr[start] == '0') {
        const char prefix = expr[start + 1];
        if (prefix == 'x' || prefix == 'X') {
            base = 16;
            start += 2;
        } else if (prefix == 'b' || prefix == 'B') {
            base = 2;
            start += 2;
        } else if (prefix == 'o' || prefix == 'O') {
            base = 8;
            start += 2;
        }
    }
    if (start == expr.size()) {
        return false;
    }
    bool digit = false;
    bool dot = false;
    for (size_t i = start; i < expr.size(); ++i) {
        const char c = expr[i];
        if (base != 10) {
            if (!valid_digit(c, base)) {
                return false;
            }
            digit = digit || c != '_';
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            digit = true;
            continue;
        }
        if (c == '_') {
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

std::string infer_simple_assignment_type(std::string expr) {
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

} // namespace

std::optional<std::string> member_completion_target(const Document& doc, const Json* params) {
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
        while (end > 0 &&
               std::isspace(static_cast<unsigned char>(line[static_cast<size_t>(end - 1)])) != 0) {
            --end;
        }
        int start = end;
        while (start > 0 &&
               (identifier_char(line[static_cast<size_t>(start - 1)]) ||
                line[static_cast<size_t>(start - 1)] == '.')) {
            --start;
        }
        if (start == end) {
            return std::nullopt;
        }
        return line.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    }
    return std::nullopt;
}

std::string local_type_before_cursor(const Document& doc, const std::string& name,
                                     const Json* params) {
    const std::map<std::string, std::string> locals = local_types_before_cursor(doc, params);
    const auto found = locals.find(name);
    return found == locals.end() ? std::string{} : found->second;
}

std::map<std::string, std::string> local_types_before_cursor(const Document& doc,
                                                             const Json* params) {
    static const std::regex local_decl(
        R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_\.]*)\b)");
    static const std::regex assign_decl(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^=].*)$)");
    static const std::regex def_decl(R"(^\s*def\s+[A-Za-z_][A-Za-z0-9_]*\(([^)]*)\))");
    const Json* position = params == nullptr ? nullptr : params->get("position");
    const int max_line =
        position == nullptr ? std::numeric_limits<int>::max() : int_value(position->get("line"));
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

std::set<std::string> member_candidate_types(const ModuleAst& module, const std::string& type) {
    std::set<std::string> out{type};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const NativeTypeDecl& alias : module.native_types) {
            if (!alias.type.empty() && out.contains(alias.name) && out.insert(alias.type).second) {
                changed = true;
            }
        }
        for (const TypeAliasDecl& alias : module.aliases) {
            if (!alias.type.empty() && out.contains(alias.name) && out.insert(alias.type).second) {
                changed = true;
            }
        }
    }
    return out;
}

} // namespace dudu
