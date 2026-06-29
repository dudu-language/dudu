#include "dudu/sema/sema_method_templates.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/sema/sema_context.hpp"

#include <map>

namespace dudu {
namespace {

std::map<std::string, TypeRef>
receiver_template_ref_substitutions(const std::vector<TypeRef>& receiver_args) {
    std::map<std::string, TypeRef> substitutions;
    if (receiver_args.empty()) {
        return substitutions;
    }
    const TypeRef& first = receiver_args.front();
    for (const char* name :
         {"T", "_T", "_Tp", "_Tp1", "_Ty", "_Ty1", "value_type", "element_type", "key_type"}) {
        substitutions.emplace(name, first);
    }
    if (receiver_args.size() >= 2) {
        const TypeRef& first_arg = receiver_args[0];
        const TypeRef& second_arg = receiver_args[1];
        substitutions.insert_or_assign("_Key", first_arg);
        substitutions.insert_or_assign("_Val", second_arg);
        substitutions.insert_or_assign("_T1", first_arg);
        substitutions.insert_or_assign("_T2", second_arg);
        substitutions.insert_or_assign("_Tp1", first_arg);
        substitutions.insert_or_assign("_Tp2", second_arg);
        substitutions.insert_or_assign("_Ty1", first_arg);
        substitutions.insert_or_assign("_Ty2", second_arg);
        substitutions.insert_or_assign("mapped_type", second_arg);
        substitutions.insert_or_assign("key_type", first_arg);
    }
    return substitutions;
}

} // namespace

std::vector<TypeRef> template_arg_refs_from_type(const TypeRef& type) {
    return type.kind == TypeKind::Template ? type.children : std::vector<TypeRef>{};
}

TypeRef substitute_receiver_template_type(const TypeRef& type,
                                          const std::vector<TypeRef>& receiver_args) {
    const std::map<std::string, TypeRef> substitutions =
        receiver_template_ref_substitutions(receiver_args);
    if (substitutions.empty()) {
        return type;
    }
    return substitute_type_ref(type, substitutions);
}

} // namespace dudu
