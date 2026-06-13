#include "dudu/unsupported.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema.hpp"

#include <array>
#include <cctype>

namespace dudu {
namespace {

struct UnsupportedPrefix {
    std::string_view prefix;
    std::string_view feature;
};

bool starts_statement(std::string_view text, std::string_view prefix) {
    if (text == prefix) {
        return true;
    }
    if (text.size() <= prefix.size() || text.substr(0, prefix.size()) != prefix) {
        return false;
    }
    const char next = text[prefix.size()];
    return next == ' ' || next == ':';
}

bool contains_call(std::string_view text, std::string_view name) {
    size_t pos = text.find(name);
    while (pos != std::string_view::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(text[pos - 1])) == 0 && text[pos - 1] != '_');
        const size_t open = pos + name.size();
        if (left_ok && open < text.size() && text[open] == '(') {
            return true;
        }
        pos = text.find(name, pos + name.size());
    }
    return false;
}

bool contains_word(std::string_view text, std::string_view name) {
    size_t pos = text.find(name);
    while (pos != std::string_view::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(text[pos - 1])) == 0 && text[pos - 1] != '_');
        const size_t end = pos + name.size();
        const bool right_ok =
            end == text.size() ||
            (std::isalnum(static_cast<unsigned char>(text[end])) == 0 && text[end] != '_');
        if (left_ok && right_ok) {
            return true;
        }
        pos = text.find(name, pos + name.size());
    }
    return false;
}

void check_statement(const RawStmt& stmt) {
    const std::string text = trim_copy(stmt.text);
    constexpr std::array prefixes = {
        UnsupportedPrefix{"raise", "exceptions"},
        UnsupportedPrefix{"try", "exceptions"},
        UnsupportedPrefix{"except", "exceptions"},
        UnsupportedPrefix{"finally", "exceptions"},
        UnsupportedPrefix{"yield", "generators"},
        UnsupportedPrefix{"async", "async"},
        UnsupportedPrefix{"await", "async"},
        UnsupportedPrefix{"with", "context managers"},
        UnsupportedPrefix{"global", "global rebinding"},
        UnsupportedPrefix{"nonlocal", "nonlocal rebinding"},
        UnsupportedPrefix{"del", "dynamic deletion"},
        UnsupportedPrefix{"assert", "runtime assertions"},
        UnsupportedPrefix{"import", "local imports"},
        UnsupportedPrefix{"from", "local imports"},
        UnsupportedPrefix{"match", "pattern matching"},
    };
    for (const UnsupportedPrefix& prefix : prefixes) {
        if (starts_statement(text, prefix.prefix)) {
            throw CompileError(stmt.location,
                               "unsupported Python feature: " + std::string(prefix.feature));
        }
    }
    if (contains_call(text, "eval") || contains_call(text, "exec")) {
        throw CompileError(stmt.location, "unsupported Python feature: dynamic execution");
    }
    if (contains_call(text, "getattr") || contains_call(text, "setattr")) {
        throw CompileError(stmt.location, "unsupported Python feature: dynamic attribute access");
    }
    if (contains_word(text, "await")) {
        throw CompileError(stmt.location, "unsupported Python feature: async");
    }
    for (const RawStmt& child : stmt.children) {
        check_statement(child);
    }
}

} // namespace

void check_unsupported_python(const ModuleAst& module) {
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            for (const RawStmt& stmt : method.body) {
                check_statement(stmt);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        for (const RawStmt& stmt : fn.body) {
            check_statement(stmt);
        }
    }
}

} // namespace dudu
