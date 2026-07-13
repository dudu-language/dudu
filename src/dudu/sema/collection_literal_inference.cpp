#include "dudu/sema/collection_literal_inference.hpp"

#include "dudu/core/ast_type.hpp"

#include <utility>

namespace dudu {
namespace {

std::string collection_name(ExprKind kind) {
    switch (kind) {
    case ExprKind::ListLiteral:
        return "list";
    case ExprKind::DictLiteral:
        return "dict";
    case ExprKind::SetLiteral:
        return "set";
    default:
        return "collection";
    }
}

TypeRef bare_collection_type(const Expr& expr) {
    return named_type_ref(collection_name(expr.kind), expr.location);
}

TypeRef template_type(std::string name, std::vector<TypeRef> args,
                      const SourceLocation& location) {
    TypeRef out = named_type_ref(std::move(name), location);
    out.kind = TypeKind::Template;
    out.children = std::move(args);
    return out;
}

TypeRef comparable_type(const Symbols* symbols, TypeRef type) {
    return symbols == nullptr ? type : resolve_alias_ref(*symbols, std::move(type));
}

bool same_type(const Symbols* symbols, const TypeRef& left, const TypeRef& right) {
    return type_ref_equivalent(comparable_type(symbols, left), comparable_type(symbols, right));
}

CollectionLiteralInference infer_value(const Symbols* symbols, const Expr& expr,
                                       const CollectionElementTypeInfer& infer_element) {
    if (is_collection_literal(expr)) {
        return infer_collection_literal_type(symbols, expr, infer_element);
    }
    const TypeRef type = infer_element(expr);
    if (!has_type_ref(type) || type_ref_is_auto(type)) {
        return {.status = CollectionLiteralStatus::Unresolved,
                .type_ref = {},
                .expected_ref = {},
                .actual_ref = {},
                .error_location = expr.location,
                .component = {}};
    }
    return {.status = CollectionLiteralStatus::Inferred,
            .type_ref = type,
            .expected_ref = {},
            .actual_ref = {},
            .error_location = {},
            .component = {}};
}

CollectionLiteralInference propagate_nested(CollectionLiteralInference nested,
                                             std::string component) {
    nested.component = std::move(component) +
                       (nested.component.empty() ? std::string{} : " " + nested.component);
    return nested;
}

CollectionLiteralInference infer_homogeneous(const Symbols* symbols, const Expr& expr,
                                              std::string component,
                                              const CollectionElementTypeInfer& infer_element) {
    if (expr.children.empty()) {
        return {.status = CollectionLiteralStatus::Empty,
                .type_ref = bare_collection_type(expr),
                .expected_ref = {},
                .actual_ref = {},
                .error_location = expr.location,
                .component = std::move(component)};
    }

    CollectionLiteralInference first = infer_value(symbols, expr.children.front(), infer_element);
    if (first.status != CollectionLiteralStatus::Inferred) {
        return propagate_nested(std::move(first), component);
    }
    for (size_t i = 1; i < expr.children.size(); ++i) {
        CollectionLiteralInference current = infer_value(symbols, expr.children[i], infer_element);
        if (current.status != CollectionLiteralStatus::Inferred) {
            return propagate_nested(std::move(current), component);
        }
        if (!same_type(symbols, first.type_ref, current.type_ref)) {
            return {.status = CollectionLiteralStatus::Heterogeneous,
                    .type_ref = bare_collection_type(expr),
                    .expected_ref = first.type_ref,
                    .actual_ref = current.type_ref,
                    .error_location = expr.children[i].location,
                    .component = std::move(component)};
        }
    }

    return {.status = CollectionLiteralStatus::Inferred,
            .type_ref = template_type(collection_name(expr.kind), {first.type_ref}, expr.location),
            .expected_ref = {},
            .actual_ref = {},
            .error_location = {},
            .component = {}};
}

CollectionLiteralInference infer_dict(const Symbols* symbols, const Expr& expr,
                                      const CollectionElementTypeInfer& infer_element) {
    if (expr.children.empty()) {
        return {.status = CollectionLiteralStatus::Empty,
                .type_ref = bare_collection_type(expr),
                .expected_ref = {},
                .actual_ref = {},
                .error_location = expr.location,
                .component = "dict"};
    }

    TypeRef key_type;
    TypeRef value_type;
    for (size_t i = 0; i < expr.children.size(); ++i) {
        const Expr& entry = expr.children[i];
        if (entry.kind != ExprKind::DictEntry || entry.children.size() != 2) {
            return {.status = CollectionLiteralStatus::Malformed,
                    .type_ref = bare_collection_type(expr),
                    .expected_ref = {},
                    .actual_ref = {},
                    .error_location = entry.location,
                    .component = "dict entry"};
        }
        CollectionLiteralInference key =
            infer_value(symbols, entry.children[0], infer_element);
        if (key.status != CollectionLiteralStatus::Inferred) {
            return propagate_nested(std::move(key), "dict key");
        }
        CollectionLiteralInference value =
            infer_value(symbols, entry.children[1], infer_element);
        if (value.status != CollectionLiteralStatus::Inferred) {
            return propagate_nested(std::move(value), "dict value");
        }
        if (i == 0) {
            key_type = key.type_ref;
            value_type = value.type_ref;
            continue;
        }
        if (!same_type(symbols, key_type, key.type_ref)) {
            return {.status = CollectionLiteralStatus::Heterogeneous,
                    .type_ref = bare_collection_type(expr),
                    .expected_ref = key_type,
                    .actual_ref = key.type_ref,
                    .error_location = entry.children[0].location,
                    .component = "dict key"};
        }
        if (!same_type(symbols, value_type, value.type_ref)) {
            return {.status = CollectionLiteralStatus::Heterogeneous,
                    .type_ref = bare_collection_type(expr),
                    .expected_ref = value_type,
                    .actual_ref = value.type_ref,
                    .error_location = entry.children[1].location,
                    .component = "dict value"};
        }
    }

    return {.status = CollectionLiteralStatus::Inferred,
            .type_ref = template_type("dict", {key_type, value_type}, expr.location),
            .expected_ref = {},
            .actual_ref = {},
            .error_location = {},
            .component = {}};
}

} // namespace

bool is_collection_literal(const Expr& expr) {
    return expr.kind == ExprKind::ListLiteral || expr.kind == ExprKind::DictLiteral ||
           expr.kind == ExprKind::SetLiteral;
}

CollectionLiteralInference
infer_collection_literal_type(const Symbols* symbols, const Expr& expr,
                              const CollectionElementTypeInfer& infer_element) {
    if (expr.kind == ExprKind::DictLiteral) {
        return infer_dict(symbols, expr, infer_element);
    }
    if (expr.kind == ExprKind::ListLiteral) {
        return infer_homogeneous(symbols, expr, "list element", infer_element);
    }
    if (expr.kind == ExprKind::SetLiteral) {
        return infer_homogeneous(symbols, expr, "set element", infer_element);
    }
    return {};
}

std::string collection_literal_error(const CollectionLiteralInference& inference) {
    switch (inference.status) {
    case CollectionLiteralStatus::Empty:
        return "cannot infer type of empty collection literal; add an explicit list[T], "
               "dict[K, V], or set[T] annotation";
    case CollectionLiteralStatus::Heterogeneous:
        return inference.component + " type mismatch: expected " +
               type_ref_text(inference.expected_ref) + ", got " +
               type_ref_text(inference.actual_ref) +
               "; use an explicit variant element type for heterogeneous values";
    case CollectionLiteralStatus::Unresolved:
        return "cannot infer " + inference.component + " type";
    case CollectionLiteralStatus::Malformed:
        return "malformed " + inference.component;
    case CollectionLiteralStatus::NotCollection:
        return "expression is not a collection literal";
    case CollectionLiteralStatus::Inferred:
        return {};
    }
    return "cannot infer collection literal type";
}

} // namespace dudu
