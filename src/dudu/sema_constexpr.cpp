#include "dudu/sema_constexpr.hpp"

#include "dudu/cpp_lower.hpp"

#include <cctype>
#include <set>
#include <string_view>

namespace dudu {
namespace {

bool has_constexpr_decorator(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (trim_copy(decorator.text) == "constexpr") {
            return true;
        }
    }
    return false;
}

std::string call_name_before(const std::string& expr, size_t open) {
    size_t end = open;
    while (end > 0 && std::isspace(static_cast<unsigned char>(expr[end - 1])) != 0) {
        --end;
    }
    if (end > 0 && expr[end - 1] == ']') {
        int depth = 1;
        --end;
        while (end > 0 && depth > 0) {
            --end;
            if (expr[end] == ']') {
                ++depth;
            } else if (expr[end] == '[') {
                --depth;
            }
        }
    }
    while (end > 0 && std::isspace(static_cast<unsigned char>(expr[end - 1])) != 0) {
        --end;
    }
    size_t start = end;
    while (start > 0) {
        const char c = expr[start - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_' && c != '.') {
            break;
        }
        --start;
    }
    return trim_copy(expr.substr(start, end - start));
}

void check_expr_calls(const SourceLocation& location, const std::string& expr,
                      const std::set<std::string>& constexpr_functions,
                      const std::set<std::string>& dudu_functions) {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < expr.size(); ++i) {
        const char c = expr[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c != '(') {
            continue;
        }
        const std::string callee = call_name_before(expr, i);
        if (dudu_functions.contains(callee) && !constexpr_functions.contains(callee)) {
            throw CompileError(location,
                               "compile-time expression calls non-constexpr function: " + callee);
        }
    }
}

} // namespace

void check_constexpr_uses(const ModuleAst& module) {
    std::set<std::string> functions;
    std::set<std::string> constexpr_functions;
    for (const FunctionDecl& fn : module.functions) {
        functions.insert(fn.name);
        if (has_constexpr_decorator(fn)) {
            constexpr_functions.insert(fn.name);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        check_expr_calls(constant.location, constant.value, constexpr_functions, functions);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        check_expr_calls(assertion.location, assertion.expression, constexpr_functions, functions);
    }
}

} // namespace dudu
