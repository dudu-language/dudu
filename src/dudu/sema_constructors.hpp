#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_scope.hpp"

#include <functional>
#include <string>
#include <vector>

namespace dudu {

struct ConstructorParam {
    std::string name;
    std::string type;
};

std::vector<ConstructorParam> constructor_params(const ClassDecl& klass);
void check_constructor_args(
    const FunctionScope& scope, const ClassDecl& klass, const std::vector<std::string>& args,
    const SourceLocation* location,
    const std::function<std::string(const FunctionScope&, std::string, const SourceLocation*)>&
        infer_expr,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>&
        can_assign);

} // namespace dudu
