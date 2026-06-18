#include "dudu/array_shape.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_scope.hpp"
#include "dudu/type_compat.hpp"

#include <optional>

namespace dudu {
namespace {

TypeRef unwrap_reference_and_const(TypeRef type) {
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const) &&
           type.children.size() == 1) {
        type = type.children.front();
    }
    return type;
}

std::optional<TypeRef> fixed_array_element_type_ref(const TypeRef& type) {
    if (type.kind != TypeKind::FixedArray || type.children.empty()) {
        return std::nullopt;
    }
    const TypeRef& storage = type.children.front();
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        return storage.children.front();
    }
    return storage;
}

std::optional<TypeRef> single_template_child_type_ref(const TypeRef& type, std::string_view name) {
    if (type.kind == TypeKind::Template && type.name == name && type.children.size() == 1) {
        return type.children.front();
    }
    return std::nullopt;
}

} // namespace

std::optional<TypeRef> iterable_type_ref_from_type(TypeRef type) {
    type = unwrap_reference_and_const(std::move(type));
    if (const auto element = single_template_child_type_ref(type, "list")) {
        return *element;
    }
    if (const auto element = single_template_child_type_ref(type, "span")) {
        return *element;
    }
    if (const auto element = single_template_child_type_ref(type, "strided_span")) {
        return *element;
    }
    if (const auto element = fixed_array_element_type_ref(type)) {
        return *element;
    }
    if (type.kind == TypeKind::Template && type.children.size() == 1) {
        return type.children.front();
    }
    return std::nullopt;
}

std::optional<std::string> iterable_type_from_type(TypeRef type) {
    if (const auto element = iterable_type_ref_from_type(std::move(type))) {
        return substitute_type_ref_text(*element, {});
    }
    return std::nullopt;
}

std::optional<TypeRef>
iterable_value_type_ref(const std::map<std::string, TypeRef>& local_type_refs,
                        const std::string& name) {
    const TypeRef type_ref = local_type_ref(local_type_refs, name);
    if (type_ref.kind == TypeKind::Unknown) {
        return std::nullopt;
    }
    return iterable_type_ref_from_type(type_ref);
}

std::string iterable_value_type(const std::map<std::string, TypeRef>& local_type_refs,
                                const std::string& name) {
    if (const auto element = iterable_value_type_ref(local_type_refs, name)) {
        return substitute_type_ref_text(*element, {});
    }
    return {};
}

void check_iterable_binding(const Symbols& symbols,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const SourceLocation& location, const TypeRef& binding_type,
                            const Expr& iterable) {
    if (direct_callee_name(iterable) == "range") {
        return;
    }
    if (iterable.kind != ExprKind::Name) {
        return;
    }
    const std::string& name = iterable.name;
    if (!local_type_refs.contains(name)) {
        throw CompileError(location, "iteration over unknown local: " + name);
    }
    const std::optional<TypeRef> element_type = iterable_value_type_ref(local_type_refs, name);
    if (!element_type) {
        throw CompileError(location, "cannot iterate non-container: " + name);
    }
    const std::string element = substitute_type_ref_text(*element_type, {});
    const std::string binding_text = substitute_type_ref_text(binding_type, {});
    if (!type_assignment_allowed(binding_type, *element_type) &&
        !type_assignment_allowed(resolve_alias(symbols, binding_text),
                                 resolve_alias(symbols, element))) {
        throw CompileError(location, "loop binding expects " + binding_text + ", got " + element);
    }
}

} // namespace dudu
