#pragma once

#include "dudu/ast.hpp"

#include <string>
#include <vector>

namespace dudu {

struct ConstructorParam {
    std::string name;
    std::string type;
};

std::vector<ConstructorParam> constructor_params(const ClassDecl& klass);

} // namespace dudu
