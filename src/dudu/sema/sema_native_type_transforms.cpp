#include "dudu/sema/sema_native_type_transforms.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_enum.hpp"

namespace dudu {
namespace {

TypeRef remove_top_level_cv(TypeRef type) {
    while ((type.kind == TypeKind::Const || type.kind == TypeKind::Volatile) &&
           type.children.size() == 1) {
        type = std::move(type.children.front());
    }
    return type;
}

TypeRef remove_reference(TypeRef type) {
    if (type.kind == TypeKind::Reference && type.children.size() == 1) {
        return std::move(type.children.front());
    }
    return type;
}

} // namespace

TypeRef resolve_native_type_transform(const Symbols& symbols, TypeRef type) {
    if (type.kind != TypeKind::NativeTransform || type.children.size() != 1) {
        return type;
    }

    TypeRef argument = resolve_alias_ref(symbols, std::move(type.children.front()));
    if (type.name == "__remove_cv") {
        return remove_top_level_cv(std::move(argument));
    }
    if (type.name == "__remove_reference") {
        return remove_reference(std::move(argument));
    }
    if (type.name == "__remove_cvref") {
        return remove_top_level_cv(remove_reference(std::move(argument)));
    }
    if (type.name == "__underlying_type") {
        if (const EnumDecl* en = enum_decl_for_type(symbols, argument);
            en != nullptr && has_type_ref(en->underlying_type_ref)) {
            return resolve_alias_ref(symbols, en->underlying_type_ref);
        }
    }
    type.children = {std::move(argument)};
    return type;
}

} // namespace dudu
