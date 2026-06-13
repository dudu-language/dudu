#pragma once

#include "dudu/sema_context.hpp"

#include <map>
#include <set>
#include <string>

namespace dudu {

struct FunctionScope {
    const Symbols& symbols;
    std::map<std::string, std::string> locals;
    std::set<std::string> constants;
    std::string target_mode = "hosted";
};

} // namespace dudu
