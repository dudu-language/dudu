#pragma once

#include "dudu/core/ast.hpp"

#include <map>
#include <string>
#include <vector>

namespace dudu {

struct BodyTypeSubstitutions {
    std::map<std::string, TypeRef> scalar;
    std::map<std::string, std::vector<TypeRef>> packs;
};

BodyTypeSubstitutions body_type_substitutions(const std::vector<std::string>& params,
                                              const std::vector<TypeRef>& args);
std::string body_instantiated_label(const std::string& name, const std::vector<TypeRef>& args);
std::vector<Stmt> substitute_body_types(std::vector<Stmt> body,
                                        const std::map<std::string, TypeRef>& substitutions);
std::vector<Stmt> substitute_body_types(std::vector<Stmt> body,
                                        const BodyTypeSubstitutions& substitutions);

} // namespace dudu
