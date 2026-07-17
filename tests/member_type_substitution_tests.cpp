#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/match_patterns.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_semantic_tokens.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_header_ast_parse_internal.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/native/native_signature_templates.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_inheritance.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/sema_native.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <cassert>
#include <cctype>
#include <exception>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace {

void test_receiver_template_substitution_uses_type_ast() {
    dudu::ClassDecl container;
    container.name = "Container";
    container.generic_params = {"T"};
    dudu::TypeAliasDecl value_type;
    value_type.name = "value_type";
    value_type.type_ref = dudu::parse_type_text("T");
    container.type_aliases.push_back(value_type);
    dudu::TypeAliasDecl element_type;
    element_type.name = "element_type";
    element_type.type_ref = dudu::parse_type_text("T");
    container.type_aliases.push_back(element_type);
    auto substitute = [&](std::string_view type, std::vector<dudu::TypeRef> receiver_args) {
        return dudu::substitute_type_ref_text(
            dudu::substitute_receiver_template_type(dudu::parse_type_text(type), container,
                                                    receiver_args),
            {});
    };
    assert(substitute("list[value_type]", {dudu::parse_type_text("i32")}) == "list[i32]");
    assert(substitute("fn(value_type) -> element_type", {dudu::parse_type_text("f32")}) ==
           "fn(f32) -> f32");
    assert(substitute("std::vector<value_type>", {dudu::parse_type_text("i32")}) ==
           "std::vector<i32>");
    assert(dudu::type_ref_text(dudu::substitute_type_ref(
               dudu::parse_type_text("Sequence.value_type"),
               {{"Sequence", dudu::parse_type_text("std.vector[i32]")}})) ==
           "std.vector[i32].value_type");
    const dudu::TypeRef associated = dudu::substitute_type_ref(
        dudu::parse_type_text("Sequence.const_reference"),
        {{"Sequence", dudu::parse_type_text("std.vector[i32]")}});
    assert(associated.kind == dudu::TypeKind::Associated);
    assert(associated.children.size() == 1);
    assert(dudu::type_ref_text(associated.children.front()) == "std.vector[i32]");

    const dudu::TypeRef vector_type = dudu::parse_type_text("std::vector<value_type>");
    const dudu::TypeRef vector_substituted = dudu::substitute_receiver_template_type(
        vector_type, container, {dudu::parse_type_text("i32")});
    assert(dudu::substitute_type_ref_text(vector_substituted, {}) == "std::vector<i32>");

    const std::vector<dudu::TypeRef> receiver_arg_refs =
        dudu::template_arg_refs_from_type(dudu::parse_type_text("dict[str, list[i32]]"));
    assert(receiver_arg_refs.size() == 2);
    assert(dudu::substitute_type_ref_text(receiver_arg_refs[1], {}) == "list[i32]");
}

void test_nested_member_template_alias_uses_outer_and_inner_args() {
    dudu::ClassDecl outer;
    outer.name = "Meta";
    outer.generic_params = {"A"};

    dudu::ClassDecl nested;
    nested.name = "Meta.rebind";
    nested.generic_params = {"T"};
    dudu::TypeAliasDecl other;
    other.name = "other";
    other.type_ref = dudu::parse_type_text("Pair[A, T]");
    nested.type_aliases.push_back(std::move(other));

    dudu::Symbols symbols;
    symbols.classes.emplace(outer.name, &outer);
    symbols.classes.emplace(nested.name, &nested);

    const dudu::TypeRef input =
        dudu::parse_type_text("Meta[i32].rebind[f32].other");
    assert(dudu::receiver_class_name(symbols, input.children.front()) == "Meta.rebind");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(symbols, input)) ==
           "Pair[i32, f32]");
}

void test_native_member_template_alias_uses_imported_alias_metadata() {
    dudu::Symbols symbols;
    symbols.alias_type_refs["std.pointer_traits.rebind"] = dudu::parse_type_text("*U");
    symbols.alias_generic_params["std.pointer_traits.rebind"] = {"U"};

    const dudu::TypeRef input =
        dudu::parse_type_text("std.pointer_traits[*i32].rebind[const[f32]]");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(symbols, input)) ==
           "*const[f32]");
}

void test_class_scoped_alias_template_uses_its_arguments() {
    dudu::ClassDecl owner;
    owner.name = "native.Owner";
    owner.generic_params = {"Outer"};
    owner.generic_default_args = {dudu::parse_type_text("i32")};

    dudu::TypeAliasDecl rebound;
    rebound.name = "rebound";
    rebound.generic_params = {"Inner"};
    rebound.type_ref = dudu::parse_type_text("Pair[Outer, Inner]");
    owner.type_aliases.push_back(std::move(rebound));

    dudu::Symbols symbols;
    symbols.classes.emplace(owner.name, &owner);

    const dudu::TypeRef input = dudu::parse_type_text("native.Owner.rebound[f32]");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(symbols, input)) ==
           "Pair[i32, f32]");
}

void test_native_generic_method_defers_member_alias_resolution_until_bound() {
    dudu::ClassDecl owner;
    owner.name = "native.Owner";
    owner.native_declaration = true;
    owner.generic_params = {"Outer"};
    owner.generic_default_args = {dudu::parse_type_text("i32")};

    dudu::TypeAliasDecl rebound;
    rebound.name = "rebound";
    rebound.generic_params = {"Inner"};
    rebound.type_ref = dudu::parse_type_text("Pair[Outer, Inner]");
    owner.type_aliases.push_back(std::move(rebound));

    dudu::FunctionDecl make;
    make.name = "make";
    make.generic_params = {"Inner"};
    make.return_type_ref = dudu::parse_type_text("native.Owner.rebound[Inner]");
    owner.methods.push_back(std::move(make));

    dudu::Symbols symbols;
    symbols.classes.emplace(owner.name, &owner);
    const auto candidates =
        dudu::method_signatures_for_type(symbols, dudu::parse_type_text(owner.name), "make", {});
    assert(candidates.size() == 1);
    const std::string unbound_return =
        dudu::type_ref_text(dudu::signature_return_type_ref(candidates.front()));
    if (unbound_return != "native.Owner[i32].rebound[Inner]") {
        throw std::runtime_error("unbound member alias resolved as " + unbound_return);
    }

    const dudu::FunctionSignature bound = dudu::substitute_explicit_template_signature(
        symbols, candidates.front(), {dudu::parse_type_text("f32")});
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(bound)) == "Pair[i32, f32]");
}

void test_native_index_aliases_use_pointer_sized_dudu_types() {
    dudu::ClassDecl container;
    container.name = "NativeContainer";
    dudu::TypeAliasDecl size_type;
    size_type.name = "size_type";
    size_type.type_ref = dudu::parse_type_text("u64");
    container.type_aliases.push_back(std::move(size_type));
    dudu::TypeAliasDecl difference_type;
    difference_type.name = "difference_type";
    difference_type.type_ref = dudu::parse_type_text("i64");
    container.type_aliases.push_back(std::move(difference_type));

    dudu::Symbols symbols;
    symbols.classes.emplace(container.name, &container);

    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(
               symbols, dudu::parse_type_text("NativeContainer.size_type"))) == "usize");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(
               symbols, dudu::parse_type_text("NativeContainer.difference_type"))) == "isize");
}

void test_native_single_qualifier_transforms() {
    dudu::Symbols symbols;
    auto transform = [](std::string name, dudu::TypeRef argument) {
        dudu::TypeRef type;
        type.kind = dudu::TypeKind::NativeTransform;
        type.name = std::move(name);
        type.children.push_back(std::move(argument));
        return type;
    };

    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(
               symbols, transform("__remove_const", dudu::parse_type_text("const[i32]")))) ==
           "i32");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(
               symbols, transform("__remove_const", dudu::parse_type_text("i32")))) == "i32");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(
               symbols,
               transform("__remove_volatile", dudu::parse_type_text("volatile[f32]")))) ==
           "f32");
}

void test_native_partial_specialization_alias_uses_bound_pattern_args() {
    dudu::ClassDecl primary;
    primary.name = "meta.rebind";
    primary.generic_params = {"T", "U", "Enable"};
    primary.generic_min_args = 2;
    primary.generic_default_args = {
        {}, {}, dudu::parse_type_text("void"),
    };

    dudu::ClassDecl specialization;
    specialization.name = primary.name;
    specialization.generic_params = {"T", "U"};
    specialization.native_specialization_args = {
        dudu::parse_type_text("T"),
        dudu::parse_type_text("U"),
        dudu::parse_type_text("void"),
    };
    specialization.native_partial_specialization = true;
    dudu::TypeAliasDecl result;
    result.name = "type";
    dudu::TypeRef member_template;
    member_template.kind = dudu::TypeKind::AssociatedTemplate;
    member_template.name = "rebind";
    member_template.children = {
        dudu::parse_type_text("T"),
        dudu::parse_type_text("U"),
    };
    result.type_ref.kind = dudu::TypeKind::Associated;
    result.type_ref.name = "other";
    result.type_ref.children.push_back(std::move(member_template));
    specialization.type_aliases.push_back(std::move(result));

    dudu::Symbols symbols;
    symbols.classes.emplace(primary.name, &primary);
    symbols.native_class_specializations[primary.name].push_back(specialization);

    const dudu::TypeRef input =
        dudu::parse_type_text("meta.rebind[Allocator[i32], i32].type");
    const std::string resolved =
        dudu::type_ref_text(dudu::resolve_associated_type_ref(symbols, input));
    if (resolved != "Allocator[i32].rebind[i32].other") {
        throw std::runtime_error("partial specialization alias resolved as " + resolved);
    }
}

void test_native_partial_specialization_alias_binds_top_level_pack() {
    dudu::ClassDecl primary;
    primary.name = "meta.select_pack";
    primary.generic_params = {"Index", "T..."};

    dudu::ClassDecl specialization;
    specialization.name = primary.name;
    specialization.generic_params = {"T0", "Rest..."};
    specialization.native_specialization_args = {
        dudu::parse_type_text("0"),
        dudu::parse_type_text("T0"),
        dudu::parse_type_text("Rest..."),
    };
    specialization.native_partial_specialization = true;
    dudu::TypeAliasDecl result;
    result.name = "type";
    result.type_ref = dudu::parse_type_text("T0");
    specialization.type_aliases.push_back(std::move(result));

    dudu::Symbols symbols;
    symbols.classes.emplace(primary.name, &primary);
    symbols.native_class_specializations[primary.name].push_back(specialization);

    const dudu::TypeRef input =
        dudu::parse_type_text("meta.select_pack[0, i32, f32].type");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(symbols, input)) == "i32");
}

void test_native_partial_specialization_exposes_inherited_methods() {
    dudu::ClassDecl base;
    base.name = "meta.Base";
    base.native_declaration = true;
    base.generic_params = {"T"};
    dudu::FunctionDecl begin;
    begin.name = "begin";
    begin.return_type_ref = dudu::parse_type_text("T");
    base.methods.push_back(std::move(begin));

    dudu::ClassDecl primary;
    primary.name = "meta.Container";
    primary.native_declaration = true;
    primary.generic_params = {"T", "Enable"};
    primary.generic_min_args = 1;
    primary.generic_default_args = {{}, dudu::parse_type_text("void")};

    dudu::ClassDecl specialization;
    specialization.name = primary.name;
    specialization.native_declaration = true;
    specialization.generic_params = {"T"};
    specialization.native_specialization_args = {
        dudu::parse_type_text("T"),
        dudu::parse_type_text("void"),
    };
    specialization.native_partial_specialization = true;
    dudu::BaseClassDecl inherited;
    inherited.type_ref = dudu::parse_type_text("meta.Base[T]");
    specialization.base_class_refs.push_back(std::move(inherited));

    dudu::Symbols symbols;
    symbols.classes.emplace(base.name, &base);
    symbols.classes.emplace(primary.name, &primary);
    symbols.native_class_specializations[primary.name].push_back(std::move(specialization));

    const std::vector<dudu::FunctionSignature> methods = dudu::method_signatures_for_type(
        symbols, dudu::parse_type_text("meta.Container[i32]"), "begin");
    assert(methods.size() == 1);
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(methods.front())) == "i32");
}

void test_incomplete_native_specialization_does_not_mask_primary() {
    dudu::ClassDecl primary;
    primary.name = "meta.Value";
    primary.native_declaration = true;
    primary.generic_params = {"T"};
    dudu::FunctionDecl get;
    get.name = "get";
    get.return_type_ref = dudu::parse_type_text("T");
    primary.methods.push_back(std::move(get));

    dudu::ClassDecl incomplete;
    incomplete.name = primary.name;
    incomplete.native_declaration = true;
    incomplete.native_specialization_args = {dudu::parse_type_text("i32")};

    dudu::Symbols symbols;
    symbols.classes.emplace(primary.name, &primary);
    symbols.native_class_specializations[primary.name].push_back(std::move(incomplete));

    const std::vector<dudu::FunctionSignature> methods = dudu::method_signatures_for_type(
        symbols, dudu::parse_type_text("meta.Value[i32]"), "get");
    assert(methods.size() == 1);
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(methods.front())) == "i32");
}

void test_native_specialization_matches_alias_default() {
    dudu::ClassDecl primary;
    primary.name = "meta.Traits";
    primary.native_declaration = true;
    primary.generic_params = {"T", "Enable"};
    primary.generic_min_args = 1;
    primary.generic_default_args = {{}, dudu::parse_type_text("meta.Void[]")};

    dudu::ClassDecl specialization;
    specialization.name = primary.name;
    specialization.native_declaration = true;
    specialization.generic_params = {"T"};
    specialization.native_specialization_args = {
        dudu::parse_type_text("T"), dudu::parse_type_text("void")};
    specialization.native_partial_specialization = true;
    dudu::TypeAliasDecl result;
    result.name = "result";
    result.type_ref = dudu::parse_type_text("T");
    specialization.type_aliases.push_back(std::move(result));

    dudu::Symbols symbols;
    symbols.classes.emplace(primary.name, &primary);
    symbols.native_class_specializations[primary.name].push_back(std::move(specialization));
    symbols.alias_type_refs.emplace("meta.Void", dudu::parse_type_text("void"));
    symbols.alias_generic_params.emplace("meta.Void", std::vector<std::string>{"T..."});

    const dudu::TypeRef result_type = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("meta.Traits[i32].result"));
    assert(dudu::type_ref_text(result_type) == "i32");
}

void test_inherited_alias_lookup_skips_normalized_unrelated_base() {
    dudu::ClassDecl missing;
    missing.name = "meta.Missing";
    missing.generic_params = {"T"};

    dudu::ClassDecl present;
    present.name = "meta.Present";
    present.generic_params = {"T"};
    dudu::TypeAliasDecl mapped;
    mapped.name = "mapped_type";
    mapped.type_ref = dudu::parse_type_text("T");
    present.type_aliases.push_back(std::move(mapped));

    dudu::ClassDecl owner;
    owner.name = "meta.Owner";
    owner.generic_params = {"T"};
    dudu::BaseClassDecl unrelated;
    unrelated.type_ref = dudu::parse_type_text("meta.Missing[meta.Identity[T]]");
    owner.base_class_refs.push_back(std::move(unrelated));
    dudu::BaseClassDecl defining;
    defining.type_ref = dudu::parse_type_text("meta.Present[T]");
    owner.base_class_refs.push_back(std::move(defining));

    dudu::Symbols symbols;
    symbols.classes.emplace(missing.name, &missing);
    symbols.classes.emplace(present.name, &present);
    symbols.classes.emplace(owner.name, &owner);
    symbols.alias_type_refs.emplace("meta.Identity", dudu::parse_type_text("U"));
    symbols.alias_generic_params.emplace("meta.Identity", std::vector<std::string>{"U"});

    const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("meta.Owner[i32].mapped_type"));
    assert(dudu::type_ref_text(resolved) == "i32");
}

void test_inherited_alias_uses_dependent_value_default_for_specialization() {
    dudu::ClassDecl bool_constant;
    bool_constant.name = "meta.BoolConstant";
    bool_constant.generic_params = {"Value"};
    dudu::ConstDecl value;
    value.name = "value";
    value.value_expr.kind = dudu::ExprKind::Name;
    value.value_expr.name = "Value";
    bool_constant.static_fields.push_back(std::move(value));

    dudu::ClassDecl traits;
    traits.name = "meta.Traits";
    traits.generic_params = {"Unique"};
    dudu::TypeAliasDecl unique_keys;
    unique_keys.name = "unique_keys";
    unique_keys.type_ref = dudu::parse_type_text("meta.BoolConstant[Unique]");
    traits.type_aliases.push_back(std::move(unique_keys));

    dudu::ClassDecl map_base;
    map_base.name = "meta.MapBase";
    map_base.native_declaration = true;
    map_base.generic_params = {"T", "Traits", "Unique"};
    map_base.generic_min_args = 2;
    map_base.generic_default_args = {
        {}, {}, dudu::native_ast_parse::parse_native_type_text(
                    "Traits.unique_keys.value", {}, {"Traits"})};

    dudu::ClassDecl unique_map;
    unique_map.name = map_base.name;
    unique_map.native_declaration = true;
    unique_map.generic_params = {"T", "Traits"};
    unique_map.native_specialization_args = {
        dudu::parse_type_text("T"), dudu::parse_type_text("Traits"),
        dudu::parse_type_text("true")};
    unique_map.native_partial_specialization = true;
    dudu::TypeAliasDecl mapped;
    mapped.name = "mapped_type";
    mapped.type_ref = dudu::parse_type_text("T");
    unique_map.type_aliases.push_back(std::move(mapped));

    dudu::ClassDecl owner;
    owner.name = "meta.Owner";
    owner.generic_params = {"T", "Traits"};
    dudu::BaseClassDecl base;
    base.type_ref = dudu::parse_type_text("meta.MapBase[T, Traits]");
    owner.base_class_refs.push_back(std::move(base));

    dudu::Symbols symbols;
    symbols.classes.emplace(bool_constant.name, &bool_constant);
    symbols.classes.emplace(traits.name, &traits);
    symbols.classes.emplace(map_base.name, &map_base);
    symbols.classes.emplace(owner.name, &owner);
    symbols.native_class_specializations[map_base.name].push_back(std::move(unique_map));

    const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("meta.Owner[i32, meta.Traits[true]].mapped_type"));
    assert(dudu::type_ref_text(resolved) == "i32");
}

} // namespace

int main() {
    try {
        test_receiver_template_substitution_uses_type_ast();
        test_nested_member_template_alias_uses_outer_and_inner_args();
        test_native_member_template_alias_uses_imported_alias_metadata();
        test_class_scoped_alias_template_uses_its_arguments();
        test_native_generic_method_defers_member_alias_resolution_until_bound();
        test_native_index_aliases_use_pointer_sized_dudu_types();
        test_native_single_qualifier_transforms();
        test_native_partial_specialization_alias_uses_bound_pattern_args();
        test_native_partial_specialization_alias_binds_top_level_pack();
        test_native_partial_specialization_exposes_inherited_methods();
        test_incomplete_native_specialization_does_not_mask_primary();
        test_native_specialization_matches_alias_default();
        test_inherited_alias_lookup_skips_normalized_unrelated_base();
        test_inherited_alias_uses_dependent_value_default_for_specialization();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
