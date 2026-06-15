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
    std::map<std::string, TypeRef> local_type_refs;
};

} // namespace dudu
