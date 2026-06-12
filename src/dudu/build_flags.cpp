#include "dudu/build_flags.hpp"

#include "dudu/sema.hpp"

#include <cctype>
#include <set>

namespace dudu {
namespace {

bool is_ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

void check_text(const std::set<std::string>& names, const SourceLocation& location,
                const std::string& text) {
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
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
        if (text.compare(i, 6, "build.") != 0) {
            continue;
        }
        const size_t start = i + 6;
        size_t end = start;
        while (end < text.size() && is_ident(text[end])) {
            ++end;
        }
        const std::string name = text.substr(start, end - start);
        if (!names.contains(name)) {
            throw CompileError(location, "unknown build flag: build." + name);
        }
        i = end;
    }
}

void check_body(const std::set<std::string>& names, const std::vector<RawStmt>& body) {
    for (const RawStmt& stmt : body) {
        check_text(names, stmt.location, stmt.text);
        check_body(names, stmt.children);
    }
}

} // namespace

void check_build_flags(const ModuleAst& module) {
    std::set<std::string> names = {"DEBUG", "RENDER_BACKEND"};
    for (const auto& [name, _] : module.build_values) {
        names.insert(name);
    }
    for (const ConstDecl& constant : module.constants) {
        check_text(names, constant.location, constant.value);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        check_text(names, assertion.location, assertion.expression);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            check_body(names, method.body);
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        check_body(names, fn.body);
    }
}

} // namespace dudu
