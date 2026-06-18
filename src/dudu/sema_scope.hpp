#pragma once

#include "dudu/sema_context.hpp"

#include <map>
#include <set>
#include <string>

namespace dudu {

struct FunctionScope {
    explicit FunctionScope(const Symbols& symbols_ref) : symbols(symbols_ref) {
    }

    const Symbols& symbols;
    std::map<std::string, std::string> locals;
    std::set<std::string> constants;
    std::string target_mode = "hosted";
    std::string current_class;
    bool allow_super_init = false;
    TypeRef return_type_ref;
    std::map<std::string, TypeRef> local_type_refs;
};

inline TypeRef local_type_ref(const FunctionScope& scope, const std::string& name,
                              SourceLocation location = {}) {
    if (const auto local = scope.local_type_refs.find(name); local != scope.local_type_refs.end()) {
        return local->second;
    }
    if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
        return parse_type_text(local->second, location);
    }
    return {};
}

inline TypeRef local_type_ref(const Symbols& symbols,
                              const std::map<std::string, std::string>& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const std::string& name, SourceLocation location = {}) {
    if (const auto local = local_type_refs.find(name); local != local_type_refs.end()) {
        return local->second;
    }
    if (const auto local = locals.find(name); local != locals.end()) {
        return parse_type_text(resolve_alias(symbols, local->second), location);
    }
    return {};
}

} // namespace dudu
