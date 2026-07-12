#include "dudu/lsp/language_server_type_layout.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_context.hpp"

#include <algorithm>
#include <exception>
#include <set>
#include <string>
#include <utility>

namespace dudu {
namespace {

size_t align_up(size_t value, size_t alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

std::optional<size_t> align_decorator_value(const ClassDecl& klass) {
    for (const Decorator& decorator : klass.decorators) {
        const std::optional<std::string> value = decorator_first_arg_display(decorator, "align");
        if (!value) {
            continue;
        }
        try {
            const size_t parsed = static_cast<size_t>(std::stoull(*value));
            return parsed == 0 ? std::nullopt : std::optional<size_t>{parsed};
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<TypeLayout> resolved_type_layout_impl(const Symbols& symbols, TypeRef type,
                                                    std::set<const ClassDecl*>& active);

std::optional<TypeLayout> resolved_class_layout_impl(const Symbols& symbols, const ClassDecl& klass,
                                                     std::set<const ClassDecl*>& active) {
    if (klass.layout) {
        return klass.layout;
    }
    if (klass.native_declaration || !klass.base_class_refs.empty() ||
        !active.insert(&klass).second) {
        return std::nullopt;
    }

    size_t size = 0;
    size_t alignment = 1;
    for (const FieldDecl& field : klass.fields) {
        const std::optional<TypeLayout> field_layout =
            resolved_type_layout_impl(symbols, field.type_ref, active);
        if (!field_layout) {
            active.erase(&klass);
            return std::nullopt;
        }
        size = align_up(size, field_layout->alignment);
        size += field_layout->size;
        alignment = std::max(alignment, field_layout->alignment);
    }
    active.erase(&klass);
    if (const std::optional<size_t> explicit_alignment = align_decorator_value(klass)) {
        alignment = std::max(alignment, *explicit_alignment);
    }
    return TypeLayout{.size = align_up(std::max<size_t>(size, 1), alignment),
                      .alignment = alignment};
}

std::optional<TypeLayout> fixed_array_layout(const Symbols& symbols, const TypeRef& type,
                                             std::set<const ClassDecl*>& active) {
    if (type.children.size() < 2) {
        return std::nullopt;
    }
    const std::optional<TypeLayout> element =
        resolved_type_layout_impl(symbols, type.children.front(), active);
    if (!element) {
        return std::nullopt;
    }
    size_t count = 1;
    for (size_t i = 1; i < type.children.size(); ++i) {
        const std::string extent = type_ref_head_name(type.children[i]);
        try {
            count *= static_cast<size_t>(std::stoull(extent));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return TypeLayout{.size = element->size * count, .alignment = element->alignment};
}

std::optional<TypeLayout> resolved_type_layout_impl(const Symbols& symbols, TypeRef type,
                                                    std::set<const ClassDecl*>& active) {
    type = resolve_alias_ref(symbols, std::move(type));
    if (!has_type_ref(type)) {
        return std::nullopt;
    }
    if (type.kind == TypeKind::Named || type.kind == TypeKind::Qualified) {
        const std::string name = type_ref_text(type);
        if (const std::optional<TypeLayout> primitive = primitive_type_layout(name)) {
            return primitive;
        }
        if (const NativeTypeDecl* native = native_type_decl_for_binding(symbols, name);
            native != nullptr && native->layout) {
            return native->layout;
        }
        if (const auto klass = symbols.classes.find(name); klass != symbols.classes.end()) {
            return resolved_class_layout_impl(symbols, *klass->second, active);
        }
        return std::nullopt;
    }
    if (type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference ||
        type.kind == TypeKind::Function) {
        return TypeLayout{.size = sizeof(void*), .alignment = alignof(void*)};
    }
    if ((type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
         type.kind == TypeKind::Static || type.kind == TypeKind::Device ||
         type.kind == TypeKind::Storage || type.kind == TypeKind::Shared ||
         type.kind == TypeKind::Shaped) &&
        type.children.size() == 1) {
        return resolved_type_layout_impl(symbols, type.children.front(), active);
    }
    if (type.kind == TypeKind::FixedArray) {
        return fixed_array_layout(symbols, type, active);
    }
    return std::nullopt;
}

} // namespace

std::optional<TypeLayout> primitive_type_layout(std::string_view name) {
    if (name == "bool" || name == "char" || name == "i8" || name == "u8") {
        return TypeLayout{.size = 1, .alignment = 1};
    }
    if (name == "i16" || name == "u16") {
        return TypeLayout{.size = 2, .alignment = 2};
    }
    if (name == "i32" || name == "u32" || name == "f32") {
        return TypeLayout{.size = 4, .alignment = 4};
    }
    if (name == "i64" || name == "u64" || name == "isize" || name == "usize" || name == "f64") {
        return TypeLayout{.size = 8, .alignment = 8};
    }
    return std::nullopt;
}

std::optional<TypeLayout> resolved_type_layout(const Symbols& symbols, const TypeRef& type) {
    std::set<const ClassDecl*> active;
    return resolved_type_layout_impl(symbols, type, active);
}

std::optional<TypeLayout> resolved_class_layout(const Symbols& symbols, const ClassDecl& klass) {
    std::set<const ClassDecl*> active;
    return resolved_class_layout_impl(symbols, klass, active);
}

} // namespace dudu
