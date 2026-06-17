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

} // namespace dudu
