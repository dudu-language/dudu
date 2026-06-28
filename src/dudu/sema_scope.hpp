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
    std::set<std::string> constants;
    std::string target_mode = "hosted";
    std::string current_class;
    bool allow_super_init = false;
    TypeRef return_type_ref;
    std::map<std::string, TypeRef> local_type_refs;
};

inline const TypeRef* local_type_ref_ptr(const std::map<std::string, TypeRef>& local_type_refs,
                                         const std::string& name) {
    if (const auto local = local_type_refs.find(name); local != local_type_refs.end()) {
        return &local->second;
    }
    return nullptr;
}

inline const TypeRef* local_type_ref_ptr(const FunctionScope& scope, const std::string& name) {
    return local_type_ref_ptr(scope.local_type_refs, name);
}

inline TypeRef local_type_ref(const FunctionScope& scope, const std::string& name,
                              SourceLocation location = {}) {
    if (const TypeRef* local = local_type_ref_ptr(scope, name)) {
        return *local;
    }
    TypeRef unknown;
    unknown.location = location;
    return unknown;
}

inline TypeRef local_type_ref(const std::map<std::string, TypeRef>& local_type_refs,
                              const std::string& name, SourceLocation location = {}) {
    if (const TypeRef* local = local_type_ref_ptr(local_type_refs, name)) {
        return *local;
    }
    TypeRef unknown;
    unknown.location = location;
    return unknown;
}

} // namespace dudu
