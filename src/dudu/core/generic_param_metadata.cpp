#include "dudu/core/generic_param_metadata.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/shape_value_expr.hpp"

#include <set>
#include <string_view>

namespace dudu {
namespace {

std::string_view generic_base_name(std::string_view name) {
    if (name.ends_with("...")) {
        name.remove_suffix(3);
    }
    return name;
}

void collect_value_params(const TypeRef& type, const std::vector<std::string>& params,
                          std::set<std::string>& found) {
    if (type.kind == TypeKind::FixedArray) {
        for (const TypeRef& dimension : explicit_array_shape_refs(type)) {
            const std::set<std::string> identifiers =
                dimension.kind == TypeKind::Value
                    ? shape_value_expr_identifiers(dimension.value)
                    : std::set<std::string>{type_ref_head_name(dimension)};
            for (const std::string& param : params) {
                const std::string base(generic_base_name(param));
                if (identifiers.contains(base) || type_ref_head_name(dimension) == base ||
                    (dimension.kind == TypeKind::PackExpansion && !dimension.children.empty() &&
                     type_ref_head_name(dimension.children.front()) == base)) {
                    found.insert(base);
                }
            }
        }
    }
    for (const TypeRef& child : type.children) {
        collect_value_params(child, params, found);
    }
}

} // namespace

std::vector<bool> infer_function_generic_param_value_flags(const FunctionDecl& function) {
    std::set<std::string> value_params;
    if (function_has_return_type(function)) {
        collect_value_params(function.return_type_ref, function.generic_params, value_params);
    }
    for (const ParamDecl& param : function.params) {
        collect_value_params(param.type_ref, function.generic_params, value_params);
    }
    std::vector<bool> flags;
    flags.reserve(function.generic_params.size());
    for (const std::string& param : function.generic_params) {
        flags.push_back(value_params.contains(std::string(generic_base_name(param))));
    }
    return flags;
}

} // namespace dudu
