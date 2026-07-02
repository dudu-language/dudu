#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace dudu {

struct GenericTypeBindings {
    std::map<std::string, TypeRef> scalar;
    std::map<std::string, std::vector<TypeRef>> packs;
};

GenericTypeBindings generic_type_bindings(const std::vector<std::string>& params,
                                          const std::vector<TypeRef>& args);

bool unresolved_generic_binding(const TypeRef& binding);

TypeRef substitute_generic_type_ref(const TypeRef& type, const GenericTypeBindings& bindings);

void collect_value_generic_params(const TypeRef& type, const std::vector<std::string>& params,
                                  std::set<std::string>& out);

void collect_cpp_value_generic_params(const TypeRef& type, const std::vector<std::string>& params,
                                      std::set<std::string>& out);

bool bind_pack_generic(const std::string& param, const std::vector<TypeRef>& args,
                       std::map<std::string, std::vector<TypeRef>>& bindings, std::string& error);

bool infer_generic_binding_pack(const TypeRef& param_type, const TypeRef& arg_type,
                                const std::vector<std::string>& params,
                                GenericTypeBindings& bindings, std::string& error);

} // namespace dudu
