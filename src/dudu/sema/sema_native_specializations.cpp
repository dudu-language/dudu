#include "dudu/sema/sema_native_specializations.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_methods_internal.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace dudu {
namespace {

bool specialization_values_equivalent(std::string_view pattern, std::string_view actual) {
    if (pattern == actual) {
        return true;
    }
    return (pattern == "1" && actual == "true") || (pattern == "true" && actual == "1") ||
           (pattern == "0" && actual == "false") || (pattern == "false" && actual == "0");
}

std::optional<std::string> native_literal_value(const TypeRef& type) {
    if (type.kind == TypeKind::Value) {
        return std::string(type.value);
    }
    if (type.kind != TypeKind::Named && type.kind != TypeKind::Qualified) {
        return std::nullopt;
    }
    std::string text = type_ref_head_name(type);
    if (text == "true" || text == "false") {
        return text;
    }
    std::string_view digits = text;
    if (!digits.empty() && (digits.front() == '+' || digits.front() == '-')) {
        digits.remove_prefix(1);
    }
    if (!digits.empty() && std::ranges::all_of(digits, [](const char ch) {
            return std::isdigit(static_cast<unsigned char>(ch));
        })) {
        return text;
    }
    return std::nullopt;
}

bool bind_specialization_scalar(const std::string& name, const TypeRef& actual,
                                GenericTypeBindings& bindings) {
    const auto [found, inserted] = bindings.scalar.emplace(name, actual);
    return inserted || type_ref_equivalent(found->second, actual) ||
           type_ref_text(found->second) == type_ref_text(actual);
}

bool specialization_pack_name(const TypeRef& pattern, const std::set<std::string>& pack_params,
                              std::string& name) {
    if (pattern.kind != TypeKind::PackExpansion || pattern.children.size() != 1) {
        return false;
    }
    name = type_ref_head_name(pattern.children.front());
    return pack_params.contains(name);
}

bool specialization_packs_equivalent(const std::vector<TypeRef>& left,
                                     const std::vector<TypeRef>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (!type_ref_equivalent(left[i], right[i]) &&
            type_ref_text(left[i]) != type_ref_text(right[i])) {
            return false;
        }
    }
    return true;
}

bool bind_specialization_pattern(const TypeRef& pattern, const TypeRef& actual,
                                 const std::set<std::string>& params,
                                 const std::set<std::string>& pack_params,
                                 GenericTypeBindings& bindings, int& score);

bool bind_specialization_children(const std::vector<TypeRef>& patterns,
                                  const std::vector<TypeRef>& actuals,
                                  const std::set<std::string>& params,
                                  const std::set<std::string>& pack_params,
                                  GenericTypeBindings& bindings, int& score) {
    size_t actual_index = 0;
    for (size_t pattern_index = 0; pattern_index < patterns.size(); ++pattern_index) {
        std::string pack_name;
        if (specialization_pack_name(patterns[pattern_index], pack_params, pack_name)) {
            size_t required_tail = 0;
            for (size_t tail = pattern_index + 1; tail < patterns.size(); ++tail) {
                std::string ignored;
                if (!specialization_pack_name(patterns[tail], pack_params, ignored)) {
                    ++required_tail;
                }
            }
            if (actuals.size() < actual_index + required_tail) {
                return false;
            }
            const size_t pack_size = actuals.size() - actual_index - required_tail;
            std::vector<TypeRef> pack(actuals.begin() + actual_index,
                                      actuals.begin() + actual_index + pack_size);
            const auto [found, inserted] = bindings.packs.emplace(pack_name, pack);
            if (!inserted && !specialization_packs_equivalent(found->second, pack)) {
                return false;
            }
            actual_index += pack_size;
            continue;
        }
        if (actual_index >= actuals.size() ||
            !bind_specialization_pattern(patterns[pattern_index], actuals[actual_index], params,
                                         pack_params, bindings, score)) {
            return false;
        }
        ++actual_index;
    }
    return actual_index == actuals.size();
}

bool bind_specialization_pattern(const TypeRef& pattern, const TypeRef& actual,
                                 const std::set<std::string>& params,
                                 const std::set<std::string>& pack_params,
                                 GenericTypeBindings& bindings, int& score) {
    const std::string pattern_name = type_ref_head_name(pattern);
    if ((pattern.kind == TypeKind::Named || pattern.kind == TypeKind::Qualified) &&
        params.contains(pattern_name)) {
        return bind_specialization_scalar(pattern_name, actual, bindings);
    }
    if (pattern.kind == TypeKind::Template && params.contains(pattern_name)) {
        if (actual.kind != TypeKind::Template) {
            return false;
        }
        TypeRef constructor = actual;
        constructor.children.clear();
        if (!bind_specialization_scalar(pattern_name, constructor, bindings)) {
            return false;
        }
        return bind_specialization_children(pattern.children, actual.children, params, pack_params,
                                            bindings, score);
    }
    if (pattern.kind == TypeKind::Value) {
        ++score;
        const std::optional<std::string> actual_value = native_literal_value(actual);
        return actual_value && specialization_values_equivalent(pattern.value, *actual_value);
    }
    if (pattern.kind != actual.kind) {
        return false;
    }
    if ((pattern.kind == TypeKind::Named || pattern.kind == TypeKind::Qualified ||
         pattern.kind == TypeKind::Template || pattern.kind == TypeKind::Associated ||
         pattern.kind == TypeKind::AssociatedTemplate) &&
        type_ref_head_name(pattern) != type_ref_head_name(actual)) {
        return false;
    }
    if (pattern.kind == TypeKind::Named || pattern.kind == TypeKind::Qualified ||
        pattern.kind == TypeKind::Template || pattern.kind == TypeKind::Associated ||
        pattern.kind == TypeKind::AssociatedTemplate) {
        ++score;
    }
    return bind_specialization_children(pattern.children, actual.children, params, pack_params,
                                        bindings, score);
}

bool specialization_requirement_well_formed(TypeRef type, const Symbols& symbols,
                                            const GenericTypeBindings& bindings, size_t depth = 0) {
    if (depth > 16) {
        return false;
    }
    type = substitute_native_specialization_type(type, bindings);
    for (TypeRef& child : type.children) {
        if (!specialization_requirement_well_formed(child, symbols, bindings, depth + 1)) {
            return false;
        }
    }
    if (type.kind == TypeKind::Associated || type.kind == TypeKind::AssociatedTemplate) {
        TypeRef resolved = resolve_associated_type_ref(symbols, type);
        if (type_ref_text(resolved) == type_ref_text(type)) {
            return false;
        }
        return specialization_requirement_well_formed(std::move(resolved), symbols,
                                                      GenericTypeBindings{}, depth + 1);
    }
    if (type.kind == TypeKind::Template && symbols.alias_type_refs.contains(type.name)) {
        return specialization_requirement_well_formed(resolve_alias_ref(symbols, std::move(type)),
                                                      symbols, GenericTypeBindings{}, depth + 1);
    }
    return true;
}

} // namespace

TypeRef substitute_native_specialization_type(const TypeRef& type,
                                              const GenericTypeBindings& bindings) {
    return substitute_generic_type_ref(substitute_type_ref(type, bindings.scalar), bindings);
}

const ClassDecl* native_specialized_class_for_owner(const Symbols& symbols, const TypeRef& owner,
                                                    GenericTypeBindings& bindings) {
    const std::string owner_name = receiver_class_name(symbols, owner);
    const auto found = symbols.native_class_specializations.find(owner_name);
    if (found == symbols.native_class_specializations.end()) {
        return nullptr;
    }
    std::vector<TypeRef> actual_args = template_arg_refs_from_type(owner);
    if (const ClassDecl* primary = class_for_receiver_type(symbols, owner);
        primary != nullptr && !primary->generic_params.empty()) {
        actual_args = generic_args_with_defaults(primary->generic_params,
                                                 primary->generic_default_args, actual_args);
    }
    const ClassDecl* selected = nullptr;
    int selected_score = -1;
    bool ambiguous = false;
    GenericTypeBindings selected_bindings;
    for (const ClassDecl& candidate : found->second) {
        std::set<std::string> params;
        std::set<std::string> pack_params;
        for (const std::string& param : candidate.generic_params) {
            const std::string name = generic_param_base_name(param);
            params.insert(name);
            if (generic_param_is_pack(param)) {
                pack_params.insert(name);
            }
        }
        GenericTypeBindings candidate_bindings;
        int score = candidate.native_partial_specialization ? 0 : 1000;
        bool matches =
            bind_specialization_children(candidate.native_specialization_args, actual_args, params,
                                         pack_params, candidate_bindings, score);
        if (matches && std::ranges::any_of(candidate.native_specialization_requirements,
                                           [&](const TypeRef& requirement) {
                                               return !specialization_requirement_well_formed(
                                                   requirement, symbols, candidate_bindings);
                                           })) {
            matches = false;
        }
        if (!matches || score < selected_score) {
            continue;
        }
        if (score == selected_score) {
            ambiguous = true;
            continue;
        }
        selected = &candidate;
        selected_score = score;
        selected_bindings = std::move(candidate_bindings);
        ambiguous = false;
    }
    if (ambiguous) {
        return nullptr;
    }
    bindings = std::move(selected_bindings);
    return selected;
}

std::optional<TypeRef>
native_constexpr_value_ref(const Expr& expr, const std::map<std::string, TypeRef>& substitutions,
                           const SourceLocation& location) {
    if (expr.kind == ExprKind::Name) {
        const auto found = substitutions.find(expr.name);
        const std::optional<std::string> literal =
            found == substitutions.end() ? std::nullopt : native_literal_value(found->second);
        if (literal) {
            TypeRef value;
            value.kind = TypeKind::Value;
            value.value = *literal;
            value.location = location;
            return value;
        }
        return std::nullopt;
    }
    if (expr.kind == ExprKind::BoolLiteral || expr.kind == ExprKind::IntLiteral) {
        TypeRef value;
        value.kind = TypeKind::Value;
        value.value = expr.value;
        value.location = location;
        return value;
    }
    if (expr.kind == ExprKind::Unary && expr.op == "-" && expr.children.size() == 1 &&
        expr.children.front().kind == ExprKind::IntLiteral) {
        TypeRef value;
        value.kind = TypeKind::Value;
        value.value = "-" + expr.children.front().value;
        value.location = location;
        return value;
    }
    return std::nullopt;
}

} // namespace dudu
