#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/language_server_completion.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_local_context.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_semantic_tokens.hpp"
#include "dudu/match_patterns.hpp"
#include "dudu/native_header_types.hpp"
#include "dudu/native_signature_match.hpp"
#include "dudu/native_signature_substitution.hpp"
#include "dudu/native_signature_templates.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"
#include "dudu/sema_builtin_methods.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_expr_internal.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/sema_method_templates.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_methods_internal.hpp"
#include "dudu/sema_native.hpp"
#include "dudu/type_compat.hpp"
#include "dudu/type_compat_native.hpp"

#include <cassert>
#include <cctype>
#include <exception>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<int> semantic_token_data(const std::string& json) {
    std::vector<int> out;
    for (size_t i = 0; i < json.size();) {
        if (std::isdigit(static_cast<unsigned char>(json[i])) == 0) {
            ++i;
            continue;
        }
        int value = 0;
        while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i])) != 0) {
            value = value * 10 + json[i] - '0';
            ++i;
        }
        out.push_back(value);
    }
    return out;
}

bool has_semantic_token(const std::vector<int>& data, int type, int modifier) {
    for (size_t i = 0; i + 4 < data.size(); i += 5) {
        if (data[i + 3] == type && (data[i + 4] & modifier) == modifier) {
            return true;
        }
    }
    return false;
}

dudu::Json completion_params(int line, int character) {
    dudu::Json line_json;
    line_json.value = static_cast<double>(line);
    dudu::Json character_json;
    character_json.value = static_cast<double>(character);
    dudu::Json position_json;
    position_json.value = dudu::JsonObject{{"line", line_json}, {"character", character_json}};
    dudu::Json params;
    params.value = dudu::JsonObject{{"position", position_json}};
    return params;
}

void test_ast_assignment_display_types() {
    const dudu::TypeRef bool_type = dudu::parse_type_text("bool");
    const dudu::TypeRef missing_type;
    assert(dudu::assignment_error(bool_type, dudu::parse_expr_text("123"), missing_type) ==
           "cannot assign number to bool without an explicit cast");
    assert(dudu::assignment_error(bool_type, dudu::parse_expr_text("\"hi\""), missing_type) ==
           "cannot assign str to bool without an explicit cast");
    assert(dudu::assignment_error(bool_type, dudu::parse_expr_text("value"), missing_type) ==
           "cannot assign  to bool without an explicit cast");

    dudu::Expr unknown;
    unknown.kind = dudu::ExprKind::Unknown;
    assert(dudu::assignment_error(bool_type, unknown, missing_type) ==
           "cannot assign  to bool without an explicit cast");
}

void test_missing_expression_is_not_unknown() {
    const dudu::Expr parsed_empty = dudu::parse_expr_text("");
    assert(parsed_empty.kind == dudu::ExprKind::Missing);
    assert(dudu::expr_missing(parsed_empty));

    dudu::Expr unknown;
    unknown.kind = dudu::ExprKind::Unknown;
    assert(!dudu::expr_missing(unknown));
}

void test_type_compat_uses_type_ast_for_pointers() {
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*const[void]"),
                                         dudu::parse_type_text("*i32")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("* const[void]"),
                                         dudu::parse_type_text("*i32")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*const[i32]"),
                                         dudu::parse_type_text("*i32")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*i32"),
                                         dudu::parse_type_text("*&i32")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("&const[i32]"),
                                         dudu::parse_type_text("i32")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("i32"),
                                         dudu::parse_type_text("&const[i32]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("fn(i32) -> void"),
                                         dudu::parse_type_text("fn(i32)")));

    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*const[void]"),
                                         dudu::parse_type_text("*list[i32]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*struct sqlite3"),
                                         dudu::parse_type_text("*sqlite3")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*const[list[i32]]"),
                                         dudu::parse_type_text("*list[i32]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("&const[list[i32]]"),
                                         dudu::parse_type_text("list[i32]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("list[fn(i32)]"),
                                         dudu::parse_type_text("list[fn(i32) -> void]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("str"),
                                         dudu::parse_type_text("std.string")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("std.string"),
                                         dudu::parse_type_text("std::string")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("std.vector.size_type"),
                                         dudu::parse_type_text("usize")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("std.vector.const_iterator"),
                                         dudu::parse_type_text("std.vector.iterator")));
    assert(!dudu::type_assignment_allowed(dudu::parse_type_text("std.vector.iterator"),
                                          dudu::parse_type_text("i32")));
    assert(dudu::native_associated_operator_operand_is_dependent(
        dudu::parse_type_text("std.vector.const_reference")));
    assert(!dudu::native_associated_operator_operand_is_dependent(dudu::parse_type_text("i32")));
    assert(!dudu::type_assignment_allowed(dudu::parse_type_text("list[i32]"),
                                          dudu::parse_type_text("list[str]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("array[list[i32]][4]"),
                                         dudu::parse_type_text("array[list[i32]][4]")));
    assert(!dudu::type_assignment_allowed(dudu::parse_type_text("array[list[i32]][4]"),
                                          dudu::parse_type_text("array[list[str]][4]")));
    assert(!dudu::type_assignment_allowed(dudu::parse_type_text("array[list[i32]][4]"),
                                          dudu::parse_type_text("array[list[i32]][8]")));

    const dudu::Expr name_expr = dudu::parse_expr_text("value");
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("str"), name_expr,
                                         dudu::parse_type_text("std.string")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("str"), name_expr,
                                         dudu::parse_type_text("basic_string[char]")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("str"), name_expr,
                                         dudu::parse_type_text("basic_string[i8]")));
    dudu::TypeRef atomic_template;
    atomic_template.kind = dudu::TypeKind::Template;
    atomic_template.name = "atomic";
    atomic_template.children.push_back(dudu::parse_type_text("i32"));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("atomic[i32]"), atomic_template));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("difference_type"), name_expr,
                                         dudu::parse_type_text("i32")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("cstr"),
                                         dudu::parse_expr_text("\"hello\""),
                                         dudu::parse_type_text("str")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("array[list[i32]][4]"), name_expr,
                                         dudu::parse_type_text("array[list[i32]][4]")));
    assert(!dudu::assignment_type_allowed(dudu::parse_type_text("array[list[i32]][4]"), name_expr,
                                          dudu::parse_type_text("array[list[str]][4]")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("std.unique_ptr[Node]"), name_expr,
                                         dudu::parse_type_text("__detail.__unique_ptr_t[Node]")));
    assert(!dudu::assignment_type_allowed(dudu::parse_type_text("f32"), name_expr,
                                          dudu::parse_type_text("__m128")));

    assert(dudu::assignment_type_allowed(dudu::parse_type_text("Result[i32, str]"),
                                         dudu::parse_expr_text("Ok(7)"),
                                         dudu::parse_type_text("Ok[i32]")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("Result[i32, str]"),
                                         dudu::parse_expr_text("Err(\"bad\")"),
                                         dudu::parse_type_text("Err[str]")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("Option[i32]"),
                                         dudu::parse_expr_text("7"), dudu::parse_type_text("i32")));
    assert(!dudu::assignment_type_allowed(dudu::parse_type_text("Option[i32]"),
                                          dudu::parse_expr_text("\"bad\""),
                                          dudu::parse_type_text("str")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("f32"),
                                         dudu::parse_expr_text("f32(1)"),
                                         dudu::parse_type_text("i32")));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("list[i32]"),
                                         dudu::parse_expr_text("list[i32](raw)"),
                                         dudu::parse_type_text("auto")));

    dudu::TypeRef malformed_expected = dudu::parse_type_text("Bad[");
    assert(malformed_expected.kind == dudu::TypeKind::Unknown);
    assert(malformed_expected.malformed);
    bool malformed_lower_failed = false;
    try {
        (void)dudu::lower_cpp_type(malformed_expected);
    } catch (const dudu::CompileError&) {
        malformed_lower_failed = true;
    }
    assert(malformed_lower_failed);
}

void test_can_assign_resolves_alias_type_refs() {
    dudu::Symbols symbols;
    symbols.alias_type_refs["Meters"] = dudu::parse_type_text("f32");
    symbols.alias_type_refs["Distance"] = dudu::parse_type_text("Meters");
    const dudu::FunctionScope scope(symbols);
    const dudu::Expr value = dudu::parse_expr_text("value");
    assert(dudu::can_assign_ast(scope, dudu::parse_type_text("Distance"), value,
                                dudu::parse_type_text("Meters")));
}

void test_core_type_helpers_use_type_ast() {
    assert(dudu::base_type(dudu::parse_type_text("*const[i32]")) == "const");
    assert(dudu::base_type(dudu::parse_type_text("&Player")) == "Player");
    assert(dudu::base_type(dudu::parse_type_text("array[f32][4, 4]")) == "array");
    assert(dudu::base_type(dudu::parse_type_text("fn(i32) -> bool")) == "fn");
    assert(dudu::base_type(dudu::parse_type_text("struct sqlite3")) == "struct sqlite3");
    dudu::TypeRef spelled_pointer;
    spelled_pointer.kind = dudu::TypeKind::Pointer;
    spelled_pointer.children.push_back(dudu::named_type_ref("Player"));
    assert(dudu::base_type(spelled_pointer) == "Player");
    assert(dudu::type_ref_head_name(spelled_pointer) == "*");
    assert(dudu::type_ref_head_name(dudu::wrapped_type_ref(
               dudu::TypeKind::Const, dudu::named_type_ref("Player"))) == "const");
    assert(dudu::type_ref_head_name(dudu::parse_type_text("array[f32][4, 4]")) == "array");
    dudu::TypeRef malformed_pointer;
    malformed_pointer.kind = dudu::TypeKind::Pointer;
    assert(dudu::base_type(malformed_pointer).empty());
    bool malformed_pointer_render_failed = false;
    try {
        (void)dudu::substitute_type_ref_text(malformed_pointer, {});
    } catch (const dudu::CompileError&) {
        malformed_pointer_render_failed = true;
    }
    assert(malformed_pointer_render_failed);
    dudu::TypeRef malformed_reference;
    malformed_reference.kind = dudu::TypeKind::Reference;
    assert(dudu::base_type(malformed_reference).empty());
    dudu::TypeRef malformed_array;
    malformed_array.kind = dudu::TypeKind::FixedArray;
    assert(dudu::base_type(malformed_array).empty());
    dudu::TypeRef malformed_named;
    malformed_named.kind = dudu::TypeKind::Unknown;
    assert(dudu::base_type(malformed_named).empty());
    assert(!dudu::has_type_ref(malformed_named));
    assert(dudu::substitute_type_ref_text(
               dudu::explicit_array_element_type_ref(dudu::parse_type_text("array[list[i32]][4]")),
               {}) == "list[i32]");
    assert(dudu::type_ref_equivalent(dudu::parse_type_text("fn(list[i32]) -> tuple[i32, f32]"),
                                     dudu::parse_type_text("fn(list[i32]) -> tuple[i32, f32]")));
    assert(!dudu::type_ref_equivalent(dudu::parse_type_text("fn(list[i32]) -> tuple[i32, f32]"),
                                      dudu::parse_type_text("fn(list[f32]) -> tuple[i32, f32]")));

    const std::vector<dudu::TypeRef> tuple =
        dudu::template_type_arg_refs(dudu::parse_type_text("tuple[i32, list[str]]"), "tuple");
    assert(tuple.size() == 2);
    assert(dudu::substitute_type_ref_text(tuple[0], {}) == "i32");
    assert(dudu::substitute_type_ref_text(tuple[1], {}) == "list[str]");

    const std::vector<dudu::TypeRef> aliased_tuple = dudu::template_type_arg_refs_with_aliases(
        dudu::parse_type_text("Pair"), "tuple",
        {{"Pair", dudu::parse_type_text("tuple[i32, f32]")}});
    assert(aliased_tuple.size() == 2);
    assert(dudu::substitute_type_ref_text(aliased_tuple[0], {}) == "i32");
    assert(dudu::substitute_type_ref_text(aliased_tuple[1], {}) == "f32");

    const std::vector<dudu::TypeRef> list_args =
        dudu::template_type_arg_refs(dudu::parse_type_text("list[*Player]"), "list");
    assert(list_args.size() == 1);
    assert(dudu::substitute_type_ref_text(list_args.front(), {}) == "*Player");

    const std::vector<dudu::TypeRef> dict_args =
        dudu::template_type_arg_refs(dudu::parse_type_text("dict[str, list[i32]]"), "dict");
    assert(dict_args.size() == 2);
    assert(dudu::substitute_type_ref_text(dict_args.front(), {}) == "str");
    assert(dudu::template_type_arg_refs(dudu::parse_type_text("*Player"), "list").empty());

    const dudu::TypeRef cpp_vector = dudu::parse_type_text("std::vector<std::string>");
    assert(cpp_vector.kind == dudu::TypeKind::Template);
    assert(cpp_vector.name == "std::vector");
    assert(cpp_vector.children.size() == 1);
    assert(dudu::substitute_type_ref_text(cpp_vector.children[0], {}) == "std::string");

    const dudu::TypeRef cpp_atomic = dudu::parse_type_text("std::atomic<uint64_t>");
    assert(cpp_atomic.kind == dudu::TypeKind::Template);
    assert(cpp_atomic.name == "std::atomic");
    assert(cpp_atomic.children.size() == 1);
    assert(dudu::substitute_type_ref_text(cpp_atomic.children[0], {}) == "uint64_t");
}

void test_builtin_method_signature_uses_type_ast() {
    dudu::Symbols symbols;
    symbols.alias_type_refs["Players"] = dudu::parse_type_text("list[*Player]");
    dudu::FunctionSignature signature;
    assert(dudu::builtin_cpp_method_signature(symbols, dudu::parse_type_text("list[*Player]"),
                                              "append", signature));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).kind == dudu::TypeKind::Pointer);
    assert(dudu::signature_return_type_ref(signature).name == "void");

    signature = {};
    assert(dudu::builtin_cpp_method_signature(symbols, dudu::parse_type_text("Players"), "append",
                                              signature));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).kind == dudu::TypeKind::Pointer);
    assert(dudu::signature_return_type_ref(signature).name == "void");

    signature = {};
    assert(dudu::builtin_cpp_method_signature(
        symbols, dudu::parse_type_text("std::vector<std::string>"), "push_back", signature));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_text(signature, 0) == "std::string");
    assert(dudu::signature_return_type_ref(signature).name == "void");

    signature = {};
    assert(dudu::builtin_cpp_method_signature(
        symbols, dudu::parse_type_text("std::atomic<uint64_t>"), "load", signature));
    assert(dudu::signature_return_type_text(signature) == "uint64_t");
}

void test_native_header_types_split_cpp_templates() {
    assert(dudu::dudu_type("const vec<L, T, Q> &") == "&const[vec[L, T, Q]]");
    assert(dudu::dudu_type("_Args &&...") == "&_Args...");
    assert(dudu::dudu_type("typename std::remove_reference<_Tp>::type &&") ==
           "&std.remove_reference[_Tp]");
    assert(dudu::dudu_type("tuple<typename __decay_and_strip<_Elements>::__type...>") ==
           "tuple[__decay_and_strip[_Elements]...]");
    assert(dudu::dudu_type("typename iterator_traits<_IIter>::difference_type") ==
           "difference_type");
    assert(dudu::signature_params("T (const vec<L, T, Q> &, const vec<L, T, Q> &)") ==
           std::vector<std::string>({"&const[vec[L, T, Q]]", "&const[vec[L, T, Q]]"}));
}

void test_native_template_binding_resolves_alias_type_refs() {
    dudu::Symbols symbols;
    symbols.alias_type_refs["FloatList"] = dudu::parse_type_text("list[f32]");
    dudu::NativeTemplateBindings bindings;
    assert(dudu::bind_native_template_type_ast(symbols, dudu::parse_type_text("list[T]"),
                                               dudu::parse_type_text("FloatList"), bindings));
    assert(bindings.at("T").kind == dudu::TypeKind::Named);
    assert(bindings.at("T").name == "f32");
    assert(dudu::substitute_type_ref_text(bindings.at("T"), {}) == "f32");

    bindings.clear();
    assert(dudu::bind_native_template_type_ast(
        dudu::parse_type_text("Pair[T, T]"), dudu::parse_type_text("Pair[struct sqlite3, sqlite3]"),
        bindings));
    assert(bindings.at("T").kind == dudu::TypeKind::Named);
    assert(bindings.at("T").name == "struct sqlite3");
}

void test_bound_native_template_pack_substitution_uses_type_refs() {
    dudu::FunctionSignature signature;
    signature.template_params = {"T"};
    signature.variadic = true;
    signature.min_params = 0;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("T"), {});
    dudu::set_signature_param_types(signature, {pack_param});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("tuple[T]"));

    dudu::NativePackBindingMap packs;
    packs["T"] = {dudu::parse_type_text("i32"), dudu::parse_type_text("f32")};

    const dudu::FunctionSignature substituted =
        dudu::substitute_bound_template_signature(signature, {}, packs);
    assert(dudu::signature_param_count(substituted) == 2);
    assert(dudu::signature_param_type_ref(substituted, 0).kind == dudu::TypeKind::Named);
    assert(dudu::signature_param_type_ref(substituted, 0).name == "i32");
    assert(dudu::signature_param_type_ref(substituted, 1).kind == dudu::TypeKind::Named);
    assert(dudu::signature_param_type_ref(substituted, 1).name == "f32");
}

void test_bound_native_template_substitution_is_per_field() {
    dudu::FunctionSignature signature;
    signature.template_params = {"T", "U"};
    signature.variadic = true;
    signature.min_params = 0;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("T"), {});
    dudu::set_signature_param_types(signature, {pack_param, dudu::parse_type_text("U")});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("tuple[T]"));

    dudu::NativeTemplateBindings bindings;
    const dudu::TypeRef messy_native_binding =
        dudu::native_template_binding_type_ref("typename __decay_and_strip<U>::__type");
    bindings["U"] = messy_native_binding;

    dudu::NativePackBindingMap packs;
    packs["T"] = {dudu::parse_type_text("i32"), dudu::parse_type_text("f32")};

    const dudu::FunctionSignature substituted =
        dudu::substitute_bound_template_signature(signature, bindings, packs);
    assert(dudu::signature_param_count(substituted) == 3);
    assert(dudu::signature_param_type_ref(substituted, 0).name == "i32");
    assert(dudu::signature_param_type_ref(substituted, 1).name == "f32");
    const dudu::TypeRef& return_type = dudu::signature_return_type_ref(substituted);
    assert(return_type.kind == dudu::TypeKind::Template);
    assert(return_type.name == "tuple");
    assert(return_type.children.size() == 2);
    assert(return_type.children[0].name == "i32");
    assert(return_type.children[1].name == "f32");
}

void test_native_variadic_bare_pack_uses_type_ref_shape() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.variadic = true;
    signature.min_params = 1;
    dudu::TypeRef bare_pack;
    bare_pack.kind = dudu::TypeKind::PackExpansion;
    dudu::set_signature_param_types(signature, {dudu::parse_type_text("i32"), bare_pack});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("i32"));
    symbols.native_function_signatures["native_printf"] = {signature};
    dudu::FunctionScope scope(symbols);

    const std::vector<dudu::Expr> args = {dudu::parse_expr_text("1"), dudu::parse_expr_text("2"),
                                          dudu::parse_expr_text("3")};
    auto infer = [](const dudu::FunctionScope&, const dudu::Expr&, const dudu::SourceLocation*) {
        return dudu::parse_type_text("i32");
    };
    auto can_assign = [](const dudu::TypeRef& expected, const dudu::Expr&,
                         const dudu::TypeRef& got) {
        return dudu::type_assignment_allowed(expected, got);
    };
    const std::optional<dudu::FunctionSignature> matched =
        dudu::match_native_signature(scope, "native_printf", args, nullptr, infer, can_assign);
    assert(matched.has_value());
    assert(dudu::signature_param_count(*matched) == 2);
    assert(dudu::signature_param_type_ref(*matched, 1).kind == dudu::TypeKind::PackExpansion);
}

void test_explicit_native_template_value_args_use_type_refs() {
    dudu::FunctionSignature signature;
    signature.template_params = {"__i", "T"};
    dudu::set_signature_param_types(signature, {dudu::parse_type_text("tuple[T]")});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("T"));

    const dudu::FunctionSignature substituted = dudu::substitute_explicit_template_signature(
        signature, {dudu::parse_type_text("1"), dudu::parse_type_text("i32")});
    assert(dudu::signature_param_count(substituted) == 1);
    assert(dudu::substitute_type_ref_text(dudu::signature_param_type_ref(substituted, 0), {}) ==
           "tuple[i32]");
    assert(dudu::signature_return_type_ref(substituted).kind == dudu::TypeKind::Named);
    assert(dudu::signature_return_type_ref(substituted).name == "i32");

    dudu::FunctionSignature malformed_signature;
    dudu::TypeRef malformed_placeholder = dudu::parse_type_text("tuple[");
    dudu::set_signature_param_types(malformed_signature, {malformed_placeholder});
    dudu::set_signature_return_type(malformed_signature, dudu::parse_type_text("void"));
    const dudu::FunctionSignature malformed_substituted =
        dudu::substitute_explicit_template_signature(malformed_signature,
                                                     {dudu::parse_type_text("f32")});
    assert(dudu::signature_param_type_ref(malformed_substituted, 0).kind ==
           dudu::TypeKind::Unknown);
    assert(dudu::signature_return_type_ref(malformed_substituted).name == "void");
}

void test_explicit_native_template_keeps_unbound_pack_params() {
    dudu::FunctionSignature signature;
    signature.template_params = {"T", "Args"};
    signature.variadic = true;
    signature.min_params = 1;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("&Args"), {});
    dudu::set_signature_param_types(signature, {pack_param});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("Box[T]"));

    const dudu::FunctionSignature substituted =
        dudu::substitute_explicit_template_signature(signature, {dudu::parse_type_text("Node")});
    assert(dudu::signature_param_count(substituted) == 1);
    assert(dudu::signature_param_type_ref(substituted, 0).kind == dudu::TypeKind::PackExpansion);
    assert(dudu::native_template_pack_placeholder(dudu::signature_param_type_ref(substituted, 0)) ==
           std::optional<std::string>{"Args"});
    assert(dudu::signature_return_type_text(substituted) == "Box[Node]");
}

void test_receiver_template_substitution_uses_type_ast() {
    auto substitute = [](std::string_view type, std::vector<dudu::TypeRef> receiver_args) {
        return dudu::substitute_type_ref_text(
            dudu::substitute_receiver_template_type(dudu::parse_type_text(type), receiver_args),
            {});
    };
    assert(substitute("list[value_type]", {dudu::parse_type_text("i32")}) == "list[i32]");
    assert(substitute("fn(value_type) -> element_type", {dudu::parse_type_text("f32")}) ==
           "fn(f32) -> f32");
    assert(substitute("std::vector<value_type>", {dudu::parse_type_text("i32")}) ==
           "std::vector<i32>");

    const dudu::TypeRef vector_type = dudu::parse_type_text("std::vector<value_type>");
    const dudu::TypeRef vector_substituted =
        dudu::substitute_receiver_template_type(vector_type, {dudu::parse_type_text("i32")});
    assert(dudu::substitute_type_ref_text(vector_substituted, {}) == "std::vector<i32>");

    const std::vector<dudu::TypeRef> receiver_arg_refs =
        dudu::template_arg_refs_from_type(dudu::parse_type_text("dict[str, list[i32]]"));
    assert(receiver_arg_refs.size() == 2);
    assert(dudu::substitute_type_ref_text(receiver_arg_refs[1], {}) == "list[i32]");
}

void test_inherited_method_signature_uses_type_ast() {
    dudu::ClassDecl owner;
    owner.name = "Box";
    owner.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "replace";
    method.params.push_back({"self", dudu::parse_type_text("Box[T]"), {}});
    method.params.push_back({"value", dudu::parse_type_text("T"), {}});
    method.return_type_ref = dudu::parse_type_text("T");

    const dudu::FunctionSignature signature =
        dudu::inherited_method_signature_for_type(owner, dudu::parse_type_text("Box[i32]"), method);
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).name == "i32");
    assert(dudu::signature_return_type_ref(signature).name == "i32");
}

void test_find_inherited_method_uses_type_ast_receiver() {
    dudu::ClassDecl owner;
    owner.name = "Box";
    owner.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "replace";
    method.params.push_back({"self", dudu::parse_type_text("Box[T]"), {}});
    method.params.push_back({"value", dudu::parse_type_text("T"), {}});
    method.return_type_ref = dudu::parse_type_text("T");
    owner.methods.push_back(method);

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &owner);

    const std::optional<dudu::InheritedMethod> found =
        dudu::find_inherited_method(symbols, dudu::parse_type_text("Box[i32]"), "replace");
    assert(found);
    assert(found->method == &owner.methods.front());
    assert(dudu::signature_param_count(found->signature) == 1);
    assert(dudu::signature_param_type_ref(found->signature, 0).name == "i32");
    assert(dudu::signature_return_type_ref(found->signature).name == "i32");
}

void test_instance_storage_uses_type_ast_receiver() {
    dudu::ClassDecl owner;
    owner.name = "Box";
    owner.generic_params = {"T"};
    owner.fields.push_back({"value", dudu::parse_type_text("T"), {}, {}});

    dudu::ClassDecl wrapper;
    wrapper.name = "Wrapper";
    wrapper.base_class_refs.push_back({dudu::parse_type_text("Box[i32]"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &owner);
    symbols.classes.emplace("Wrapper", &wrapper);

    assert(dudu::class_type_has_instance_storage(symbols, dudu::parse_type_text("Box[i32]")));
    assert(dudu::class_type_has_instance_storage(symbols, dudu::parse_type_text("Wrapper")));
}

void test_native_base_assignable_uses_type_ast() {
    dudu::ClassDecl base;
    base.name = "Base";

    dudu::ClassDecl derived;
    derived.name = "Derived";
    derived.base_class_refs.push_back({dudu::parse_type_text("Base"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Base", &base);
    symbols.classes.emplace("Derived", &derived);

    assert(dudu::native_base_assignable(symbols, dudu::parse_type_text("*Base"),
                                        dudu::parse_type_text("*Derived")));
    assert(dudu::native_base_assignable(symbols, dudu::parse_type_text("&Base"),
                                        dudu::parse_type_text("&Derived")));
    assert(!dudu::native_base_assignable(symbols, dudu::parse_type_text("Base"),
                                         dudu::parse_type_text("Derived")));
}

void test_native_base_assignable_resolves_alias_type_refs() {
    dudu::ClassDecl base;
    base.name = "Base";

    dudu::ClassDecl derived;
    derived.name = "Derived";
    derived.base_class_refs.push_back({dudu::parse_type_text("Base"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Base", &base);
    symbols.classes.emplace("Derived", &derived);
    symbols.alias_type_refs["BaseAlias"] = dudu::parse_type_text("Base");
    symbols.alias_type_refs["DerivedAlias"] = dudu::parse_type_text("Derived");

    assert(dudu::native_base_assignable(symbols, dudu::parse_type_text("*BaseAlias"),
                                        dudu::parse_type_text("*DerivedAlias")));
    assert(dudu::native_base_assignable(symbols, dudu::parse_type_text("*const[BaseAlias]"),
                                        dudu::parse_type_text("*const[DerivedAlias]")));
}

void test_foreign_cpp_type_name_resolves_alias_type_refs() {
    dudu::Symbols symbols;
    symbols.native_types.insert("std.vector");
    symbols.alias_type_refs["VecAlias"] = dudu::parse_type_text("std.vector");
    symbols.alias_type_refs["ConstVecAlias"] = dudu::parse_type_text("const[VecAlias]");

    assert(dudu::foreign_cpp_type_name(symbols, dudu::parse_type_text("VecAlias")));
    assert(dudu::foreign_cpp_type_name(symbols, dudu::parse_type_text("ConstVecAlias")));
}

void test_unwrap_receiver_uses_type_ast() {
    dudu::Symbols symbols;
    symbols.alias_type_refs["AliasBox"] = dudu::parse_type_text("Box[i32]");
    symbols.alias_type_refs["ConstAliasBox"] = dudu::parse_type_text("const[AliasBox]");

    assert(dudu::unwrap_receiver_type(symbols, dudu::parse_type_text("*const[AliasBox]")) == "Box");
    assert(dudu::unwrap_receiver_type(symbols, dudu::parse_type_text("*ConstAliasBox")) == "Box");
    assert(dudu::unwrap_receiver_type(symbols, dudu::parse_type_text("&struct sqlite3")) ==
           "sqlite3");
}

void test_inherited_field_lookup_uses_type_ast_receiver() {
    dudu::ClassDecl box;
    box.name = "Box";
    box.generic_params = {"T"};
    box.fields.push_back({"value", dudu::parse_type_text("T"), {}, {}});

    dudu::ClassDecl wrapper;
    wrapper.name = "Wrapper";
    wrapper.base_class_refs.push_back({dudu::parse_type_text("Box[f32]"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &box);
    symbols.classes.emplace("Wrapper", &wrapper);

    const std::optional<dudu::TypeRef> field =
        dudu::field_type_ref_for_class(symbols, wrapper, dudu::parse_type_text("Wrapper"), "value");
    assert(field);
    assert(dudu::substitute_type_ref_text(*field, {}) == "f32");
}

void test_result_field_lookup_resolves_alias_type_refs() {
    dudu::Symbols symbols;
    symbols.alias_type_refs["Parsed"] = dudu::parse_type_text("Result[i32, str]");
    symbols.alias_type_refs["ParsedAlias"] = dudu::parse_type_text("Parsed");

    const std::optional<dudu::TypeRef> value =
        dudu::field_type_ref_for_type(symbols, dudu::parse_type_text("Parsed"), "value");
    const std::optional<dudu::TypeRef> err =
        dudu::field_type_ref_for_type(symbols, dudu::parse_type_text("Parsed"), "err");
    assert(value);
    assert(err);
    assert(dudu::substitute_type_ref_text(*value, {}) == "i32");
    assert(dudu::substitute_type_ref_text(*err, {}) == "str");
    const std::optional<dudu::TypeRef> aliased_value =
        dudu::field_type_ref_for_type(symbols, dudu::parse_type_text("ParsedAlias"), "value");
    assert(aliased_value);
    assert(dudu::substitute_type_ref_text(*aliased_value, {}) == "i32");
}

void test_swizzle_lookup_uses_type_ast_receiver() {
    dudu::ClassDecl vec2;
    vec2.name = "Vec2";
    vec2.fields.push_back({"x", dudu::parse_type_text("f32"), {}, {}});
    vec2.fields.push_back({"y", dudu::parse_type_text("f32"), {}, {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Vec2", &vec2);

    const std::optional<dudu::TypeRef> swizzle =
        dudu::swizzle_type_ref_for_type(symbols, dudu::parse_type_text("*const[Vec2]"), "xy");
    assert(swizzle);
    assert(dudu::substitute_type_ref_text(*swizzle, {}) == "Vec2");
}

void test_method_signature_lookup_uses_type_ast_receiver() {
    dudu::ClassDecl box;
    box.name = "Box";
    box.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "replace";
    method.params.push_back({"self", dudu::parse_type_text("Box[T]"), {}});
    method.params.push_back({"value", dudu::parse_type_text("T"), {}});
    method.return_type_ref = dudu::parse_type_text("T");
    box.methods.push_back(method);

    dudu::ClassDecl wrapper;
    wrapper.name = "Wrapper";
    wrapper.base_class_refs.push_back({dudu::parse_type_text("Box[f32]"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &box);
    symbols.classes.emplace("Wrapper", &wrapper);

    dudu::FunctionSignature signature;
    assert(dudu::method_signature_for_type(symbols, dudu::parse_type_text("Wrapper"), "replace",
                                           signature, nullptr));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).name == "f32");
    assert(dudu::signature_return_type_ref(signature).name == "f32");
}

void test_method_signature_list_uses_type_ast_receiver() {
    dudu::ClassDecl box;
    box.name = "Box";
    box.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "replace";
    method.params.push_back({"self", dudu::parse_type_text("Box[T]"), {}});
    method.params.push_back({"value", dudu::parse_type_text("T"), {}});
    method.return_type_ref = dudu::parse_type_text("T");
    box.methods.push_back(method);

    dudu::ClassDecl wrapper;
    wrapper.name = "Wrapper";
    wrapper.base_class_refs.push_back({dudu::parse_type_text("Box[f32]"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &box);
    symbols.classes.emplace("Wrapper", &wrapper);

    const std::vector<dudu::FunctionSignature> signatures =
        dudu::method_signatures_for_type(symbols, dudu::parse_type_text("Wrapper"), "replace");
    assert(signatures.size() == 1);
    assert(dudu::signature_param_count(signatures[0]) == 1);
    assert(dudu::signature_param_type_ref(signatures[0], 0).name == "f32");
    assert(dudu::signature_return_type_ref(signatures[0]).name == "f32");
}

void test_static_method_signature_lookup_uses_type_ast_receiver() {
    dudu::ClassDecl box;
    box.name = "Box";
    box.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "make";
    method.return_type_ref = dudu::parse_type_text("T");
    box.methods.push_back(method);

    dudu::ClassDecl wrapper;
    wrapper.name = "Wrapper";
    wrapper.base_class_refs.push_back({dudu::parse_type_text("Box[f32]"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &box);
    symbols.classes.emplace("Wrapper", &wrapper);

    dudu::FunctionSignature signature;
    assert(dudu::static_method_signature_for_type(symbols, dudu::parse_type_text("Wrapper"), "make",
                                                  signature, nullptr));
    assert(dudu::signature_param_count(signature) == 0);
    assert(dudu::signature_return_type_ref(signature).name == "f32");
}

void test_inferred_generic_method_uses_type_ast_receiver() {
    dudu::ClassDecl box;
    box.name = "Box";
    box.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "wrap";
    method.generic_params = {"U"};
    method.params.push_back({"self", dudu::parse_type_text("Box[T]"), {}});
    method.params.push_back({"value", dudu::parse_type_text("U"), {}});
    method.return_type_ref = dudu::parse_type_text("U");
    box.methods.push_back(method);

    dudu::ClassDecl wrapper;
    wrapper.name = "Wrapper";
    wrapper.base_class_refs.push_back({dudu::parse_type_text("Box[f32]"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &box);
    symbols.classes.emplace("Wrapper", &wrapper);
    dudu::FunctionScope scope(symbols);
    scope.local_type_refs.emplace("value", dudu::parse_type_text("f32"));
    const std::vector<dudu::Expr> args = {dudu::parse_expr_text("value")};

    const std::optional<dudu::FunctionSignature> signature =
        dudu::inferred_generic_method_signature_for_type(scope, dudu::parse_type_text("Wrapper"),
                                                         "wrap", args, nullptr);
    assert(signature);
    assert(dudu::signature_param_count(*signature) == 1);
    assert(dudu::substitute_type_ref_text(dudu::signature_param_type_ref(*signature, 0), {}) ==
           "f32");
    assert(dudu::substitute_type_ref_text(dudu::signature_return_type_ref(*signature), {}) ==
           "f32");
}

void test_expected_generic_method_uses_type_ast_receiver() {
    dudu::ClassDecl box;
    box.name = "Box";
    box.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "make";
    method.generic_params = {"U"};
    method.params.push_back({"self", dudu::parse_type_text("Box[T]"), {}});
    method.return_type_ref = dudu::parse_type_text("U");
    box.methods.push_back(method);

    dudu::ClassDecl wrapper;
    wrapper.name = "Wrapper";
    wrapper.base_class_refs.push_back({dudu::parse_type_text("Box[f32]"), {}});

    dudu::Symbols symbols;
    symbols.classes.emplace("Box", &box);
    symbols.classes.emplace("Wrapper", &wrapper);
    const dudu::FunctionScope scope(symbols);

    const std::optional<dudu::FunctionSignature> signature =
        dudu::inferred_generic_method_signature_for_type(
            scope, dudu::parse_type_text("Wrapper"), "make", {},
            std::optional<dudu::TypeRef>{dudu::parse_type_text("str")}, nullptr);
    assert(signature);
    assert(dudu::signature_param_count(*signature) == 0);
    assert(dudu::substitute_type_ref_text(dudu::signature_return_type_ref(*signature), {}) ==
           "str");
}

void test_auto_member_call_receiver_uses_type_ast() {
    dudu::Symbols symbols;
    dudu::FunctionScope scope(symbols);
    scope.local_type_refs.emplace("thing", dudu::named_type_ref("auto"));
    const dudu::Expr call = dudu::parse_expr_text("thing.anything(1)");

    const std::optional<dudu::TypeRef> result =
        dudu::direct_member_call_type_ref(scope, call, nullptr);
    assert(result);
    assert(dudu::type_ref_is_auto(*result));
}

void test_auto_member_expr_receiver_uses_type_ast() {
    dudu::Symbols symbols;
    dudu::FunctionScope scope(symbols);
    scope.local_type_refs.emplace("thing", dudu::named_type_ref("auto"));
    const dudu::Expr member = dudu::parse_expr_text("thing.anything");

    const std::optional<dudu::TypeRef> result =
        dudu::member_expr_direct_type_ref(scope, member, nullptr);
    assert(result);
    assert(dudu::type_ref_is_auto(*result));
}

void test_native_semantic_tokens() {
    dudu::ModuleAst module =
        dudu::parse_source("import c \"native.h\"\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    event: DuduNativeEvent\n"
                           "    if DUDU_NATIVE_CHECK():\n"
                           "        return dudu_native_add(DUDU_NATIVE_MAGIC, event.type)\n"
                           "    return 0\n",
                           "native_semantic_tokens.dd");
    dudu::ModuleAst native_symbols = module;
    native_symbols.native_types.push_back({.name = "DuduNativeEvent",
                                           .native_spelling = "DuduNativeEvent",
                                           .type_ref = dudu::parse_type_text("DuduNativeEvent"),
                                           .location = {}});
    native_symbols.native_values.push_back({.name = "DUDU_NATIVE_MAGIC",
                                            .native_spelling = "i32",
                                            .type_ref = dudu::parse_type_text("i32"),
                                            .location = {}});
    native_symbols.native_functions.push_back(
        {.name = "dudu_native_add",
         .template_params = {},
         .param_native_spellings = {"i32", "i32"},
         .param_type_refs = {dudu::parse_type_text("i32"), dudu::parse_type_text("i32")},
         .return_native_spelling = "i32",
         .return_type_ref = dudu::parse_type_text("i32"),
         .location = {}});
    native_symbols.native_macros.push_back(
        {.name = "DUDU_NATIVE_CHECK", .arity = 0, .function_like = true, .location = {}});

    const std::vector<int> data =
        semantic_token_data(dudu::semantic_tokens_json(module, native_symbols));
    constexpr int native_modifier = 16;
    assert(has_semantic_token(data, 1, native_modifier));
    assert(has_semantic_token(data, 4, native_modifier));
    assert(has_semantic_token(data, 6, native_modifier | 4));
    assert(has_semantic_token(data, 10, native_modifier));
}

void test_ast_constructor_assignment_aliases() {
    const dudu::ModuleAst module = dudu::parse_source("type Scores = dict[str, i32]\n"
                                                      "type Values = list[i32]\n"
                                                      "\n"
                                                      "class Bag:\n"
                                                      "    names: Scores\n"
                                                      "    values: Values\n"
                                                      "\n"
                                                      "def make_bag() -> Bag:\n"
                                                      "    first: Bag = Bag({}, [])\n"
                                                      "    second: Bag = Bag(names={}, values=[])\n"
                                                      "    return first\n",
                                                      "ast_constructor_assignment.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_ast_index_receiver_type_inference() {
    const dudu::ModuleAst module = dudu::parse_source("def make_values() -> list[i32]:\n"
                                                      "    return [1, 2]\n"
                                                      "\n"
                                                      "def make_matrix() -> array[i32][2, 2]:\n"
                                                      "    matrix: array[i32] = [[1, 2], [3, 4]]\n"
                                                      "    return matrix\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    first: i32 = make_values()[0]\n"
                                                      "    second: i32 = make_matrix()[1][0]\n"
                                                      "    return first + second\n",
                                                      "ast_index_receiver.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_statement_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    total: i32 = 0\n"
                                                      "    for item: i32 in values:\n"
                                                      "        total += item\n"
                                                      "    if total == 0:\n"
                                                      "        total += 42\n"
                                                      "    else:\n"
                                                      "        total = 1\n"
                                                      "    return total\n",
                                                      "statement_ast.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 5);
    assert(main.statements[0].kind == dudu::StmtKind::VarDecl);
    assert(main.statements[0].name == "total");
    assert(dudu::substitute_type_ref_text(main.statements[0].type_ref, {}) == "i32");
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(main.statements[0].value_expr.value == "0");
    assert(main.statements[1].kind == dudu::StmtKind::For);
    assert(main.statements[1].name == "item");
    assert(dudu::substitute_type_ref_text(main.statements[1].type_ref, {}) == "i32");
    assert(main.statements[1].iterable_expr.kind == dudu::ExprKind::Name);
    assert(main.statements[1].iterable_expr.name == "values");
    assert(main.statements[1].children.size() == 1);
    assert(main.statements[1].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(main.statements[1].children[0].target_expr.kind == dudu::ExprKind::Name);
    assert(main.statements[1].children[0].target_expr.name == "total");
    assert(main.statements[1].children[0].compound_op == dudu::CompoundAssignOp::Add);
    assert(main.statements[1].children[0].value_expr.kind == dudu::ExprKind::Name);
    assert(main.statements[1].children[0].value_expr.name == "item");
    assert(main.statements[2].kind == dudu::StmtKind::If);
    assert(main.statements[2].condition_expr.kind == dudu::ExprKind::Binary);
    assert(main.statements[2].condition_expr.op == "==");
    assert(main.statements[2].condition_expr.children.size() == 2);
    assert(main.statements[2].condition_expr.children[0].kind == dudu::ExprKind::Name);
    assert(main.statements[2].condition_expr.children[0].name == "total");
    assert(main.statements[2].condition_expr.children[1].kind == dudu::ExprKind::IntLiteral);
    assert(main.statements[2].condition_expr.children[1].value == "0");
    assert(main.statements[2].children.size() == 1);
    assert(main.statements[2].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(main.statements[3].kind == dudu::StmtKind::Else);
    assert(main.statements[3].children.size() == 1);
    assert(main.statements[3].children[0].kind == dudu::StmtKind::Assign);
    assert(main.statements[3].children[0].target_expr.kind == dudu::ExprKind::Name);
    assert(main.statements[3].children[0].target_expr.name == "total");
    assert(main.statements[3].children[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(main.statements[3].children[0].value_expr.value == "1");
    assert(main.statements[4].kind == dudu::StmtKind::Return);
    assert(main.statements[4].value_expr.kind == dudu::ExprKind::Name);
    assert(main.statements[4].value_expr.name == "total");
}

void test_var_decl_name_must_be_identifier() {
    bool rejected = false;
    try {
        (void)dudu::parse_source("def main() -> i32:\n"
                                 "    player.hp: i32 = 1\n"
                                 "    return 0\n",
                                 "invalid_decl_name.dd");
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(std::string(error.what()).find("expected : after declaration name") !=
               std::string::npos);
        rejected = true;
    }
    assert(rejected);
}

void test_except_binding_name_must_be_identifier() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    try:\n"
                                                      "        return 1\n"
                                                      "    except err: Error:\n"
                                                      "        return 0\n",
                                                      "except_binding.dd");
    const dudu::Stmt& except = module.functions.front().statements[1];
    assert(except.kind == dudu::StmtKind::Except);
    assert(except.name == "err");
    assert(dudu::type_ref_text(except.type_ref) == "Error");

    bool rejected = false;
    try {
        (void)dudu::parse_source("def main() -> i32:\n"
                                 "    try:\n"
                                 "        return 1\n"
                                 "    except err.value: Error:\n"
                                 "        return 0\n",
                                 "invalid_except_binding.dd");
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 4);
        assert(std::string(error.what()).find("expected : after declaration name") !=
               std::string::npos);
        rejected = true;
    }
    assert(rejected);
}

void test_unsupported_statement_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    with open(\"data\"):\n"
                                                      "        return 0\n",
                                                      "unsupported_statement_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 1);
    assert(main.statements[0].kind == dudu::StmtKind::Unsupported);
    assert(main.statements[0].unsupported_feature == dudu::UnsupportedFeature::ContextManagers);
}

void test_unsupported_def_expression_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    return def local(): 1\n",
                                                      "unsupported_def_expression_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 1);
    assert(main.statements[0].kind == dudu::StmtKind::Return);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::DefExpression);
}

void test_unsupported_comprehension_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    values = [x for x in items]\n"
                                                      "    names = {x: x for x in items}\n",
                                                      "unsupported_comprehension_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 2);
    assert(main.statements[0].kind == dudu::StmtKind::Assign);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::Comprehension);
    assert(main.statements[1].kind == dudu::StmtKind::Assign);
    assert(main.statements[1].value_expr.kind == dudu::ExprKind::Comprehension);
}

void test_unsupported_dynamic_call_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    value = eval(\"1\")\n"
                                                      "    exec(\"value = 1\")\n"
                                                      "    return getattr(value, \"x\")\n",
                                                      "unsupported_dynamic_call_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 3);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(main.statements[0].value_expr) == "eval");
    assert(main.statements[1].expr.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(main.statements[1].expr) == "exec");
    assert(main.statements[2].value_expr.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(main.statements[2].value_expr) == "getattr");
}

void test_literal_ast_values() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    enabled = True\n"
                                                      "    mask = 0x80\n"
                                                      "    title = 'hi \"there\"'\n"
                                                      "    blob = \"\"\"line\nnext\"\"\"\n"
                                                      "    return mask\n",
                                                      "literal_values.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 5);
    assert(main.statements[0].kind == dudu::StmtKind::Assign);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::BoolLiteral);
    assert(main.statements[0].value_expr.value == "True");
    assert(main.statements[1].kind == dudu::StmtKind::Assign);
    assert(main.statements[1].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(main.statements[1].value_expr.value == "0x80");
    assert(dudu::display_expr(main.statements[1].value_expr) == "0x80");
    assert(main.statements[2].value_expr.kind == dudu::ExprKind::StringLiteral);
    assert(main.statements[2].value_expr.value == "hi \"there\"");
    assert(dudu::display_expr(main.statements[2].value_expr) == "\"hi \\\"there\\\"\"");
    assert(dudu::lower_cpp_expr_ast(main.statements[2].value_expr, {}) == "\"hi \\\"there\\\"\"");
    assert(main.statements[3].value_expr.kind == dudu::ExprKind::StringLiteral);
    assert(main.statements[3].value_expr.value == "line\nnext");
    assert(dudu::display_expr(main.statements[3].value_expr) == "\"line\\nnext\"");
    assert(dudu::lower_cpp_expr_ast(main.statements[3].value_expr, {}) == "\"line\\nnext\"");
    dudu::Expr malformed_binary;
    malformed_binary.kind = dudu::ExprKind::Binary;
    assert(dudu::display_expr(malformed_binary) == "<malformed binary expression>");
}

void test_expression_ast_shape() {
    const dudu::ModuleAst module =
        dudu::parse_source("def main() -> i32:\n"
                           "    answer: i32 = add(20, values[0] + 2)\n"
                           "    if not ready or count < 3:\n"
                           "        player.inventory[slot].name = Vec4[f32](1.0, 0.0, 0.0, 1.0)\n"
                           "    values: list[i32] = [1, 2, 3]\n"
                           "    flags: i32 = mask & (1 << bit)\n"
                           "    *ptr = &values[0]\n"
                           "    point: Point = Point(x=1, y=2)\n"
                           "    hex_mask: i32 = 0x80\n"
                           "    view: span[i32] = values[1:3]\n"
                           "    pending = await fetch()\n"
                           "    produced = yield answer\n"
                           "    return answer\n",
                           "expression_ast.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 11);

    const dudu::Stmt& answer = main.statements[0];
    assert(answer.kind == dudu::StmtKind::VarDecl);
    assert(answer.value_expr.kind == dudu::ExprKind::Call);
    assert(answer.value_expr.name.empty());
    assert(dudu::direct_callee_name(answer.value_expr) == "add");
    assert(answer.value_expr.callee.size() == 1);
    assert(answer.value_expr.callee[0].kind == dudu::ExprKind::Name);
    assert(answer.value_expr.callee[0].name == "add");
    assert(answer.value_expr.range.start.line == 2);
    assert(answer.value_expr.range.start.column > answer.location.column);
    assert(answer.value_expr.children.size() == 2);
    assert(answer.value_expr.children[0].kind == dudu::ExprKind::IntLiteral);
    assert(answer.value_expr.children[0].range.start.line == 2);
    assert(answer.value_expr.children[0].range.start.column > answer.value_expr.range.start.column);
    assert(answer.value_expr.children[1].kind == dudu::ExprKind::Binary);
    assert(answer.value_expr.children[1].range.start.line == 2);
    assert(answer.value_expr.children[1].range.start.column >
           answer.value_expr.children[0].range.start.column);
    assert(answer.value_expr.children[1].op == "+");
    assert(answer.value_expr.children[1].children[0].kind == dudu::ExprKind::Index);

    const dudu::Stmt& branch = main.statements[1];
    assert(branch.kind == dudu::StmtKind::If);
    assert(branch.condition_expr.kind == dudu::ExprKind::Binary);
    assert(branch.condition_expr.op == "or");
    assert(branch.condition_expr.children[0].kind == dudu::ExprKind::Unary);
    assert(branch.condition_expr.children[0].op == "not");
    assert(branch.condition_expr.children[0].children[0].range.start.column >
           branch.condition_expr.children[0].range.start.column);
    assert(branch.condition_expr.children[1].kind == dudu::ExprKind::Binary);
    assert(branch.condition_expr.children[1].op == "<");
    assert(branch.condition_expr.children[1].children[1].range.start.column >
           branch.condition_expr.children[1].children[0].range.start.column);
    assert(branch.children.size() == 1);

    const dudu::Stmt& assign = branch.children[0];
    assert(assign.kind == dudu::StmtKind::Assign);
    assert(assign.target_expr.kind == dudu::ExprKind::Member);
    assert(assign.target_expr.name == "name");
    assert(assign.target_expr.children[0].kind == dudu::ExprKind::Index);
    assert(assign.value_expr.kind == dudu::ExprKind::TemplateCall);
    assert(assign.value_expr.name.empty());
    assert(dudu::direct_callee_name(assign.value_expr) == "Vec4");
    assert(assign.value_expr.callee.size() == 1);
    assert(assign.value_expr.callee[0].kind == dudu::ExprKind::Name);
    assert(assign.value_expr.callee[0].name == "Vec4");
    assert(assign.value_expr.template_args.size() == 1);
    assert(assign.value_expr.template_args[0].kind == dudu::ExprKind::Name);
    assert(assign.value_expr.template_args[0].name == "f32");
    assert(assign.value_expr.template_args[0].range.start.column >
           assign.value_expr.range.start.column);
    assert(assign.value_expr.template_type_args.size() == 1);
    assert(assign.value_expr.template_type_args[0].kind == dudu::TypeKind::Named);
    assert(assign.value_expr.template_type_args[0].name == "f32");
    assert(assign.value_expr.template_type_args[0].range.start.column >
           assign.value_expr.range.start.column);
    assert(assign.value_expr.children.size() == 4);
    assert(assign.value_expr.children[0].range.start.column > assign.value_expr.range.start.column);

    const dudu::Stmt& values = main.statements[2];
    assert(values.kind == dudu::StmtKind::VarDecl);
    assert(values.value_expr.kind == dudu::ExprKind::ListLiteral);
    assert(values.value_expr.children.size() == 3);
    assert(values.value_expr.children[0].range.start.column > values.value_expr.range.start.column);
    assert(values.value_expr.children[1].range.start.column >
           values.value_expr.children[0].range.start.column);
    assert(values.value_expr.children[2].range.start.column >
           values.value_expr.children[1].range.start.column);
    assert(values.value_expr.range.start.line == 5);
    assert(values.value_expr.range.start.column > values.location.column);

    const dudu::Stmt& flags = main.statements[3];
    assert(flags.kind == dudu::StmtKind::VarDecl);
    assert(flags.value_expr.kind == dudu::ExprKind::Binary);
    assert(flags.value_expr.op == "&");
    assert(flags.value_expr.children[1].kind == dudu::ExprKind::Binary);
    assert(flags.value_expr.children[1].op == "<<");
    assert(flags.value_expr.children[1].range.start.column >
           flags.value_expr.children[0].range.start.column);
    assert(flags.value_expr.children[1].children[1].range.start.column >
           flags.value_expr.children[1].children[0].range.start.column);

    const dudu::Stmt& pointer_assign = main.statements[4];
    assert(pointer_assign.kind == dudu::StmtKind::Assign);
    assert(pointer_assign.target_expr.kind == dudu::ExprKind::Unary);
    assert(pointer_assign.target_expr.op == "*");
    assert(pointer_assign.target_expr.children[0].range.start.column >
           pointer_assign.target_expr.range.start.column);
    assert(pointer_assign.value_expr.kind == dudu::ExprKind::Unary);
    assert(pointer_assign.value_expr.op == "&");
    assert(pointer_assign.value_expr.children[0].kind == dudu::ExprKind::Index);
    assert(pointer_assign.value_expr.children[0].range.start.column >
           pointer_assign.value_expr.range.start.column);

    const dudu::Stmt& point = main.statements[5];
    assert(point.kind == dudu::StmtKind::VarDecl);
    assert(point.value_expr.kind == dudu::ExprKind::Call);
    assert(point.value_expr.callee.size() == 1);
    assert(point.value_expr.callee[0].kind == dudu::ExprKind::Name);
    assert(point.value_expr.callee[0].name == "Point");
    assert(point.value_expr.children.size() == 2);
    assert(point.value_expr.children[0].kind == dudu::ExprKind::NamedArg);
    assert(point.value_expr.children[0].name == "x");
    assert(point.value_expr.children[0].children.size() == 1);
    assert(point.value_expr.children[0].children[0].kind == dudu::ExprKind::IntLiteral);
    assert(point.value_expr.children[1].kind == dudu::ExprKind::NamedArg);
    assert(point.value_expr.children[1].name == "y");

    const dudu::Stmt& hex_mask = main.statements[6];
    assert(hex_mask.kind == dudu::StmtKind::VarDecl);
    assert(hex_mask.value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(hex_mask.value_expr.value == "0x80");

    const dudu::Stmt& view = main.statements[7];
    assert(view.kind == dudu::StmtKind::VarDecl);
    assert(view.value_expr.kind == dudu::ExprKind::Index);
    assert(view.value_expr.children.size() == 2);
    assert(view.value_expr.children[1].kind == dudu::ExprKind::Slice);
    assert(view.value_expr.children[1].range.start.column > view.value_expr.range.start.column);
    assert(view.value_expr.children[1].children.size() == 2);
    assert(view.value_expr.children[1].children[0].kind == dudu::ExprKind::IntLiteral);
    assert(view.value_expr.children[1].children[1].kind == dudu::ExprKind::IntLiteral);
    assert(view.value_expr.children[1].children[0].range.start.column ==
           view.value_expr.children[1].range.start.column);
    assert(view.value_expr.children[1].children[1].range.start.column >
           view.value_expr.children[1].children[0].range.start.column);

    const dudu::Stmt& pending = main.statements[8];
    assert(pending.kind == dudu::StmtKind::Assign);
    assert(pending.value_expr.kind == dudu::ExprKind::Await);
    assert(pending.value_expr.children.size() == 1);
    assert(pending.value_expr.children[0].kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(pending.value_expr.children[0]) == "fetch");
    assert(pending.value_expr.children[0].range.start.column >
           pending.value_expr.range.start.column);

    const dudu::Stmt& produced = main.statements[9];
    assert(produced.kind == dudu::StmtKind::Assign);
    assert(produced.value_expr.kind == dudu::ExprKind::Yield);
    assert(produced.value_expr.children.size() == 1);
    assert(produced.value_expr.children[0].kind == dudu::ExprKind::Name);
    assert(produced.value_expr.children[0].name == "answer");
    assert(produced.value_expr.children[0].range.start.column >
           produced.value_expr.range.start.column);
}

void test_cpp_escape_ast_payloads() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    value: i32 = cpp(\"19\")\n"
                                                      "    cpp(\"value += 23;\")\n"
                                                      "    return value\n",
                                                      "cpp_escape_ast.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 3);

    const dudu::Stmt& value = main.statements[0];
    assert(value.kind == dudu::StmtKind::VarDecl);
    assert(value.value_expr.kind == dudu::ExprKind::CppEscape);
    assert(value.value_expr.value == "19");

    const dudu::Stmt& statement_escape = main.statements[1];
    assert(statement_escape.kind == dudu::StmtKind::CppEscape);
    assert(statement_escape.cpp_lines.size() == 1);
    assert(statement_escape.cpp_lines[0] == "value += 23;");
    assert(statement_escape.range.start.column == 5);
    assert(statement_escape.range.end.column > statement_escape.range.start.column);
}

void test_dereference_postfix_expression_shape() {
    const dudu::Expr deref = dudu::parse_expr_text("*self.out");
    assert(deref.kind == dudu::ExprKind::Unary);
    assert(deref.op == "*");
    assert(deref.children.size() == 1);
    assert(deref.children[0].kind == dudu::ExprKind::Member);
    assert(deref.children[0].name == "out");

    const dudu::Expr cast = dudu::parse_expr_text("*struct State(user_data)");
    assert(cast.kind == dudu::ExprKind::Call);
    assert(cast.name.empty());
    assert(dudu::direct_callee_name(cast) == "*struct State");
    assert(dudu::type_ref_head_name(cast.type_ref) == "struct State");

    const dudu::Expr template_cast = dudu::parse_expr_text("*list[MissingType](ptr)");
    assert(template_cast.kind == dudu::ExprKind::TemplateCall);
    assert(template_cast.name.empty());
    assert(dudu::direct_callee_name(template_cast) == "*list");
    assert(template_cast.type_ref.kind == dudu::TypeKind::Template);
    assert(template_cast.type_ref.name == "list");
    assert(template_cast.type_ref.children.size() == 1);
    assert(template_cast.type_ref.children[0].name == "MissingType");
    assert(template_cast.template_type_args.size() == 1);

    const dudu::Expr qualified_template_cast = dudu::parse_expr_text("*std.vector[i32](raw_data)");
    assert(qualified_template_cast.kind == dudu::ExprKind::TemplateCall);
    assert(dudu::direct_callee_name(qualified_template_cast) == "*std.vector");
    assert(qualified_template_cast.type_ref.kind == dudu::TypeKind::Template);
    assert(qualified_template_cast.type_ref.name == "std.vector");
    assert(qualified_template_cast.type_ref.children.size() == 1);
    assert(qualified_template_cast.template_args.size() == 1);
    assert(qualified_template_cast.template_args[0].kind == dudu::ExprKind::Name);
    assert(qualified_template_cast.template_args[0].name == "i32");
    assert(qualified_template_cast.template_type_args.size() == 1);
    assert(dudu::substitute_type_ref_text(qualified_template_cast.template_type_args[0], {}) ==
           "i32");
}

void test_decorator_expression_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("@section(\".boot+fast\")\n"
                                                      "def boot() -> void:\n"
                                                      "    pass\n"
                                                      "\n"
                                                      "class Vec2:\n"
                                                      "    x: i32\n"
                                                      "\n"
                                                      "    @operator(\"+\")\n"
                                                      "    def add(self, other: Vec2) -> Vec2:\n"
                                                      "        return self\n"
                                                      "\n"
                                                      "@test.should_panic(\"bad + input\")\n"
                                                      "def panics():\n"
                                                      "    raise \"bad + input\"\n",
                                                      "decorator_ast.dd");

    assert(module.functions.size() == 2);
    assert(module.functions[0].decorators.size() == 1);
    const dudu::Expr& section = module.functions[0].decorators[0].expr;
    assert(section.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(section) == "section");
    assert(section.range.start.line == 1);
    assert(section.range.start.column == 2);
    assert(section.children.size() == 1);
    assert(section.children[0].kind == dudu::ExprKind::StringLiteral);
    assert(section.children[0].value == ".boot+fast");

    assert(module.classes.size() == 1);
    assert(module.classes[0].methods.size() == 1);
    assert(module.classes[0].methods[0].decorators.size() == 1);
    const dudu::Expr& op = module.classes[0].methods[0].decorators[0].expr;
    assert(op.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(op) == "operator");
    assert(op.children.size() == 1);
    assert(op.children[0].kind == dudu::ExprKind::StringLiteral);
    assert(op.children[0].value == "+");

    assert(module.functions[1].decorators.size() == 1);
    const dudu::Expr& panic = module.functions[1].decorators[0].expr;
    assert(panic.kind == dudu::ExprKind::Call);
    assert(!panic.callee.empty());
    assert(panic.callee.front().kind == dudu::ExprKind::Member);
    assert(panic.children.size() == 1);
    assert(panic.children[0].kind == dudu::ExprKind::StringLiteral);
    assert(panic.children[0].value == "bad + input");
}

void test_type_ast_shape() {
    const dudu::ModuleAst module =
        dudu::parse_source("type PlayerList = list[*Player]\n"
                           "\n"
                           "MAX_PLAYERS: i32 = 4 * 2\n"
                           "static_assert(MAX_PLAYERS == 8)\n"
                           "\n"
                           "enum Mode: u8\n"
                           "    Idle = 0\n"
                           "    Running = 1 + 1\n"
                           "\n"
                           "class Player:\n"
                           "    count: static[i32] = 0\n"
                           "    DEFAULT_HP: i32 = MAX_PLAYERS + 34\n"
                           "    transform: array[f32][4, 4]\n"
                           "\n"
                           "def update(player: &Player, names: list[str]) -> *Player:\n"
                           "    local: const[list[i32]] = [1, 2]\n"
                           "    return None\n",
                           "type_ast.dd");
    assert(module.aliases.size() == 1);
    assert(module.aliases[0].type_ref.kind == dudu::TypeKind::Template);
    assert(module.aliases[0].type_ref.name == "list");
    assert(module.aliases[0].type_ref.children.size() == 1);
    assert(module.aliases[0].type_ref.children[0].kind == dudu::TypeKind::Pointer);

    assert(module.constants.size() == 1);
    assert(module.constants[0].value_expr.kind == dudu::ExprKind::Binary);
    assert(module.constants[0].value_expr.op == "*");
    assert(module.static_asserts.size() == 1);
    assert(module.static_asserts[0].expression_expr.kind == dudu::ExprKind::Binary);
    assert(module.static_asserts[0].expression_expr.op == "==");

    assert(module.enums.size() == 1);
    assert(module.enums[0].underlying_type_ref.kind == dudu::TypeKind::Named);
    assert(module.enums[0].underlying_type_ref.name == "u8");
    assert(module.enums[0].values.size() == 2);
    assert(module.enums[0].values[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(module.enums[0].values[1].value_expr.kind == dudu::ExprKind::Binary);
    assert(module.enums[0].values[1].value_expr.op == "+");

    assert(module.classes.size() == 1);
    const dudu::ClassDecl& player = module.classes[0];
    assert(player.static_fields.size() == 1);
    assert(dudu::type_ref_text(player.static_fields[0].type_ref) == "i32");
    assert(player.static_fields[0].type_ref.kind == dudu::TypeKind::Named);
    assert(player.static_fields[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(player.constants.size() == 1);
    assert(player.constants[0].value_expr.kind == dudu::ExprKind::Binary);
    assert(player.constants[0].value_expr.op == "+");
    assert(player.fields.size() == 1);
    assert(player.fields[0].type_ref.kind == dudu::TypeKind::FixedArray);
    assert(player.fields[0].type_ref.value == "4, 4");
    assert(player.fields[0].type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(player.fields[0].type_ref.children[0].name == "array");

    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& update = module.functions[0];
    assert(update.return_type_ref.kind == dudu::TypeKind::Pointer);
    assert(update.params[0].type_ref.kind == dudu::TypeKind::Reference);
    assert(update.params[1].type_ref.kind == dudu::TypeKind::Template);
    assert(update.params[1].type_ref.children[0].name == "str");
    assert(update.statements[0].type_ref.kind == dudu::TypeKind::Const);
    assert(update.statements[0].type_ref.range.start.line == 16);
    assert(update.statements[0].type_ref.range.start.column > update.statements[0].location.column);
    assert(update.statements[0].type_ref.children[0].kind == dudu::TypeKind::Template);

    assert(dudu::lower_cpp_type(dudu::parse_type_text("list[*Player]")) == "std::vector<Player*>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("*const[i32]")) == "const int32_t*");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[*i32]")) == "int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[*const[i32]]")) ==
           "const int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("&const[Player]")) == "const Player&");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[&Player]")) == "Player&");
    dudu::TypeRef structured_named;
    structured_named.kind = dudu::TypeKind::Named;
    structured_named.name = "Player";
    assert(dudu::lower_cpp_type(structured_named) == "Player");
    dudu::TypeRef spelled_pointer_type;
    spelled_pointer_type.kind = dudu::TypeKind::Pointer;
    spelled_pointer_type.children.push_back(dudu::named_type_ref("Player"));
    assert(dudu::lower_cpp_type(spelled_pointer_type) == "Player*");
    assert(dudu::lower_cpp_type_spelling("*const[i32]") == "const int32_t*");
    assert(dudu::lower_cpp_type_spelling("const[*i32]") == "int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("fn(i32, f32) -> bool")) ==
           "std::add_pointer_t<bool(int32_t, float)>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("fn(i32)")) ==
           "std::add_pointer_t<void(int32_t)>");
    assert(dudu::lower_cpp_type_spelling("fn(i32, f32) -> bool") ==
           "std::add_pointer_t<bool(int32_t, float)>");
    assert(dudu::lower_cpp_type_spelling("list[fn(i32) -> bool]") ==
           "std::vector<std::add_pointer_t<bool(int32_t)>>");
    assert(dudu::lower_cpp_type_spelling("dict[str, list[fn(i32) -> bool]]") ==
           "std::unordered_map<std::string, "
           "std::vector<std::add_pointer_t<bool(int32_t)>>>");
    assert(dudu::lower_cpp_type_spelling("std.function[fn(i32) -> bool]") ==
           "std::function<bool(int32_t)>");
    assert(dudu::lower_cpp_type_spelling("Box[list[i32]]") == "Box<std::vector<int32_t>>");
    assert(dudu::lower_cpp_type_spelling("array[Box[list[i32]]][3]") ==
           "std::array<Box<std::vector<int32_t>>, 3>");
    assert(dudu::parse_type_text("Player[3][4]").kind == dudu::TypeKind::Unknown);
    assert(dudu::parse_type_text("Box[list[i32]][3]").kind == dudu::TypeKind::Unknown);
    assert(dudu::lower_raw_template_call_arg("fn(i32) -> bool", {}) == "bool(int32_t)");
    dudu::FunctionSignature signature;
    assert(dudu::parse_function_type(dudu::parse_type_text("fn(i32, f32) -> bool"), signature));
    assert(dudu::signature_param_count(signature) == 2);
    assert(dudu::signature_param_type_ref(signature, 0).name == "i32");
    assert(dudu::signature_param_type_ref(signature, 1).name == "f32");
    assert(dudu::signature_return_type_ref(signature).name == "bool");
    const dudu::TypeRef signature_ref = dudu::function_type_ref(signature);
    assert(signature_ref.kind == dudu::TypeKind::Function);
    assert(signature_ref.children.size() == 3);
    assert(signature_ref.children[0].name == "bool");
    assert(signature_ref.children[1].name == "i32");
    assert(signature_ref.children[2].name == "f32");
    assert(dudu::substitute_type_ref_text(signature_ref, {}) == "fn(i32, f32) -> bool");
    assert(dudu::parse_function_type(dudu::parse_type_text("fn(i32)"), signature));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).name == "i32");
    assert(dudu::signature_return_type_ref(signature).name == "void");
    assert(dudu::parse_function_type(dudu::parse_type_text("std.function[fn(i32) -> i32]"),
                                     signature));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).name == "i32");
    assert(dudu::signature_return_type_ref(signature).name == "i32");
    const dudu::TypeRef c_tag = dudu::parse_type_text("*struct sqlite3");
    assert(c_tag.kind == dudu::TypeKind::Pointer);
    assert(c_tag.children[0].kind == dudu::TypeKind::Named);
    assert(c_tag.children[0].name == "struct sqlite3");
    const dudu::TypeRef nested_callback =
        dudu::parse_type_text("fn(fn(i32) -> i32, fn(i32) -> i32) -> fn(i32) -> i32");
    assert(nested_callback.kind == dudu::TypeKind::Function);
    assert(nested_callback.children.size() == 3);
    assert(nested_callback.children[0].kind == dudu::TypeKind::Function);
    assert(nested_callback.children[1].kind == dudu::TypeKind::Function);
    assert(nested_callback.children[2].kind == dudu::TypeKind::Function);
    const dudu::TypeRef nested = dudu::substitute_type_ref(
        dudu::parse_type_text("fn(list[T]) -> T"), {{"T", dudu::named_type_ref("f32")}});
    assert(dudu::substitute_type_ref_text(nested, {}) == "fn(list[f32]) -> f32");
    dudu::TypeRef malformed_placeholder;
    malformed_placeholder.kind = dudu::TypeKind::Unknown;
    const dudu::TypeRef malformed_substituted =
        dudu::substitute_type_ref(malformed_placeholder, {{"T", dudu::named_type_ref("f32")}});
    assert(malformed_substituted.kind == dudu::TypeKind::Unknown);
    assert(!dudu::has_type_ref(malformed_substituted));
    assert(dudu::lower_cpp_type(player.fields[0].type_ref) ==
           "std::array<std::array<float, 4>, 4>");
    const dudu::ArrayShapeInference inferred_array = dudu::infer_array_literal_shape_type(
        dudu::parse_type_text("array[i32]"), dudu::parse_expr_text("[[1, 2], [3, 4]]"));
    assert(inferred_array.status == dudu::ArrayShapeStatus::Inferred);
    assert(dudu::substitute_type_ref_text(inferred_array.type_ref, {}) == "array[i32][2, 2]");
    assert(dudu::substitute_type_ref_text(inferred_array.element_type_ref, {}) == "i32");
    assert(inferred_array.type_ref.kind == dudu::TypeKind::FixedArray);
    assert(inferred_array.type_ref.children.size() == 1);
    assert(inferred_array.type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(inferred_array.type_ref.children[0].name == "array");
    assert(dudu::lower_cpp_type(inferred_array.type_ref) ==
           "std::array<std::array<int32_t, 2>, 2>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("array[f32][4, 4]")) ==
           "std::array<std::array<float, 4>, 4>");
    assert(dudu::lower_cpp_type_spelling("array[i32][3]") == "std::array<int32_t, 3>");
    assert(dudu::lower_cpp_type_spelling("array[f32][4, 4]") ==
           "std::array<std::array<float, 4>, 4>");
}

void test_malformed_static_field_type_is_rejected() {
    bool threw = false;
    try {
        (void)dudu::parse_source("class Counter:\n"
                                 "    count: static[] = 0\n",
                                 "bad_static_type.dd");
    } catch (const dudu::CompileError& error) {
        threw = std::string(error.what()).find("malformed static field type") != std::string::npos;
    }
    assert(threw);
}

void test_malformed_declaration_type_syntax_is_rejected() {
    bool threw = false;
    try {
        (void)dudu::parse_source("def bad_type() -> i32:\n"
                                 "    value: * = 1\n"
                                 "    return value\n",
                                 "bad_decl_type_syntax.dd");
    } catch (const dudu::CompileError& error) {
        threw = std::string(error.what()).find("malformed type syntax") != std::string::npos;
    }
    assert(threw);
}

void test_generic_decl_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("class Box[T]:\n"
                                                      "    value: T\n"
                                                      "\n"
                                                      "    def get(self) -> T:\n"
                                                      "        return self.value\n"
                                                      "\n"
                                                      "def id[T](value: T) -> T:\n"
                                                      "    return value\n",
                                                      "generic_decl_shape.dd");
    assert(module.classes.size() == 1);
    assert(module.classes[0].name == "Box");
    assert(module.classes[0].generic_params == std::vector<std::string>{"T"});
    assert(module.classes[0].methods.size() == 1);
    assert(dudu::type_ref_text(module.classes[0].methods[0].return_type_ref) == "T");
    assert(module.functions.size() == 1);
    assert(module.functions[0].name == "id");
    assert(module.functions[0].generic_params == std::vector<std::string>{"T"});
}

void test_payload_enum_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("enum Message:\n"
                                                      "    Quit\n"
                                                      "\n"
                                                      "    Move:\n"
                                                      "        x: i32\n"
                                                      "        y: i32\n"
                                                      "\n"
                                                      "    Write(str)\n",
                                                      "payload_enum_shape.dd");
    assert(module.enums.size() == 1);
    const dudu::EnumDecl& message = module.enums[0];
    assert(message.values.size() == 3);
    assert(message.values[0].name == "Quit");
    assert(message.values[0].payload_fields.empty());
    assert(message.values[1].name == "Move");
    assert(!message.values[1].tuple_payload);
    assert(message.values[1].payload_fields.size() == 2);
    assert(message.values[1].payload_fields[0].name == "x");
    assert(message.values[1].payload_fields[0].type_ref.kind == dudu::TypeKind::Named);
    assert(message.values[2].name == "Write");
    assert(message.values[2].tuple_payload);
    assert(message.values[2].payload_fields.size() == 1);
    assert(message.values[2].payload_fields[0].name == "_0");
    assert(dudu::type_ref_text(message.values[2].payload_fields[0].type_ref) == "str");
}

void test_match_case_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def handle(msg: Message) -> i32:\n"
                                                      "    match msg:\n"
                                                      "        case Message.Quit:\n"
                                                      "            return 0\n"
                                                      "        case Message.Move(x, y) if x > 0:\n"
                                                      "            return y\n"
                                                      "        case _:\n"
                                                      "            return 1\n",
                                                      "match_case_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& handle = module.functions.front();
    assert(handle.statements.size() == 1);
    const dudu::Stmt& match = handle.statements[0];
    assert(match.kind == dudu::StmtKind::Match);
    assert(match.condition_expr.kind == dudu::ExprKind::Name);
    assert(match.condition_expr.name == "msg");
    assert(match.children.size() == 3);

    const dudu::Stmt& quit = match.children[0];
    assert(quit.kind == dudu::StmtKind::Case);
    assert(quit.pattern_expr.kind == dudu::ExprKind::Member);
    assert(quit.pattern_expr.name == "Quit");
    assert(quit.pattern_expr.children.size() == 1);
    assert(quit.pattern_expr.children[0].kind == dudu::ExprKind::Name);
    assert(quit.pattern_expr.children[0].name == "Message");
    assert(quit.children.size() == 1);
    assert(quit.children[0].kind == dudu::StmtKind::Return);

    const dudu::Stmt& move = match.children[1];
    assert(move.kind == dudu::StmtKind::Case);
    assert(move.pattern_expr.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(move.pattern_expr) == "Message.Move");
    assert(move.pattern_expr.children.size() == 2);
    assert(move.pattern_expr.children[0].kind == dudu::ExprKind::Name);
    assert(move.pattern_expr.children[0].name == "x");
    assert(move.pattern_expr.children[1].kind == dudu::ExprKind::Name);
    assert(move.pattern_expr.children[1].name == "y");
    assert(move.guard_expr.kind == dudu::ExprKind::Binary);
    assert(move.guard_expr.op == ">");
    assert(move.guard_expr.children.size() == 2);
    assert(move.guard_expr.children[0].kind == dudu::ExprKind::Name);
    assert(move.guard_expr.children[0].name == "x");
    assert(move.guard_expr.children[1].kind == dudu::ExprKind::IntLiteral);
    assert(move.guard_expr.children[1].value == "0");

    const dudu::Stmt& wildcard = match.children[2];
    assert(wildcard.kind == dudu::StmtKind::Case);
    assert(wildcard.guard_expr.kind == dudu::ExprKind::Missing);
    assert(wildcard.pattern_expr.kind == dudu::ExprKind::Name);
    assert(wildcard.pattern_expr.name == "_");
}

void test_wrapper_match_type_uses_type_ast() {
    const dudu::WrapperMatchType result =
        dudu::wrapper_match_type(dudu::parse_type_text("Result[list[i32], Option[str]]"));
    assert(result.kind == dudu::WrapperMatchKind::Result);
    assert(result.arg_refs.size() == 2);
    assert(result.arg_refs[0].kind == dudu::TypeKind::Template);
    assert(result.arg_refs[0].name == "list");
    assert(result.arg_refs[1].kind == dudu::TypeKind::Template);
    assert(result.arg_refs[1].name == "Option");

    const dudu::WrapperMatchType option =
        dudu::wrapper_match_type(dudu::parse_type_text("Option[Result[i32, str]]"));
    assert(option.kind == dudu::WrapperMatchKind::Option);
    assert(option.arg_refs.size() == 1);
    assert(option.arg_refs[0].kind == dudu::TypeKind::Template);
    assert(option.arg_refs[0].name == "Result");
}

void test_member_completion_target_uses_tokens() {
    const std::string source = "def main() -> i32:\n"
                               "    player: Player = Player()\n"
                               "    player.\n"
                               "    module.value.\n"
                               "    player.hp\n";
    const dudu::Document doc{
        .uri = "file:///completion.dd",
        .path = "/tmp/completion.dd",
        .text = source,
    };

    dudu::Json params = completion_params(2, 11);
    assert(dudu::member_completion_target(doc, &params) == "player");

    params = completion_params(3, 17);
    assert(dudu::member_completion_target(doc, &params) == "module.value");

    params = completion_params(4, 13);
    assert(dudu::member_completion_target(doc, &params) == "player");
}

void test_member_candidate_types_use_type_refs() {
    dudu::ModuleAst module;
    module.aliases.push_back(dudu::TypeAliasDecl{.name = "ViewCamera",
                                                 .cpp_name = "",
                                                 .type_ref = dudu::named_type_ref("Camera"),
                                                 .origin_module = "",
                                                 .location = {}});
    module.native_types.push_back(
        dudu::NativeTypeDecl{.name = "NativeView",
                             .native_spelling = "",
                             .type_ref = dudu::named_type_ref("NativeCamera"),
                             .location = {}});
    module.native_types.push_back(
        dudu::NativeTypeDecl{.name = "TaggedView",
                             .native_spelling = "",
                             .type_ref = dudu::named_type_ref("struct NativeTaggedCamera"),
                             .location = {}});

    const std::set<std::string> dudu_candidates =
        dudu::member_candidate_types(module, dudu::named_type_ref("ViewCamera"));
    assert(dudu_candidates.contains("ViewCamera"));
    assert(dudu_candidates.contains("Camera"));

    const std::set<std::string> native_candidates =
        dudu::member_candidate_types(module, dudu::named_type_ref("NativeView"));
    assert(native_candidates.contains("NativeView"));
    assert(native_candidates.contains("NativeCamera"));

    const std::set<std::string> tagged_candidates =
        dudu::member_candidate_types(module, dudu::named_type_ref("TaggedView"));
    assert(tagged_candidates.contains("TaggedView"));
    assert(tagged_candidates.contains("struct NativeTaggedCamera"));
    assert(tagged_candidates.contains("NativeTaggedCamera"));
}

void test_signature_help_call_site_uses_tokens() {
    const std::string source = "def add(a: i32, b: i32) -> i32:\n"
                               "    return a + b\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    return add(max(1, 2), 3)\n";
    const dudu::Document doc{
        .uri = "file:///signature.dd",
        .path = "/tmp/signature.dd",
        .text = source,
    };

    dudu::Json params = completion_params(4, 26);
    const std::string help = dudu::signature_help_json(&doc, &params);
    assert(help.find("add(a: i32, b: i32) -> i32") != std::string::npos);
    assert(help.find("\"activeParameter\":1") != std::string::npos);
}

void test_ast_expr_path_at_cursor() {
    const std::string source = "def main() -> i32:\n"
                               "    player.hp.current\n";
    const dudu::Document doc{
        .uri = "file:///path.dd",
        .path = "/tmp/path.dd",
        .text = source,
    };

    dudu::Json params = completion_params(1, 17);
    const std::optional<dudu::ExprPath> path = dudu::ast_expr_path_at(doc, &params);
    assert(path);
    assert(path->segments.size() == 3);
    assert(path->segments[0].text == "player");
    assert(path->segments[1].text == "hp");
    assert(path->segments[2].text == "current");
}

} // namespace

int main() {
    try {
        test_ast_assignment_display_types();
        test_missing_expression_is_not_unknown();
        test_type_compat_uses_type_ast_for_pointers();
        test_can_assign_resolves_alias_type_refs();
        test_core_type_helpers_use_type_ast();
        test_builtin_method_signature_uses_type_ast();
        test_native_header_types_split_cpp_templates();
        test_native_template_binding_resolves_alias_type_refs();
        test_bound_native_template_pack_substitution_uses_type_refs();
        test_bound_native_template_substitution_is_per_field();
        test_native_variadic_bare_pack_uses_type_ref_shape();
        test_explicit_native_template_value_args_use_type_refs();
        test_explicit_native_template_keeps_unbound_pack_params();
        test_receiver_template_substitution_uses_type_ast();
        test_inherited_method_signature_uses_type_ast();
        test_find_inherited_method_uses_type_ast_receiver();
        test_instance_storage_uses_type_ast_receiver();
        test_native_base_assignable_uses_type_ast();
        test_native_base_assignable_resolves_alias_type_refs();
        test_foreign_cpp_type_name_resolves_alias_type_refs();
        test_unwrap_receiver_uses_type_ast();
        test_inherited_field_lookup_uses_type_ast_receiver();
        test_result_field_lookup_resolves_alias_type_refs();
        test_swizzle_lookup_uses_type_ast_receiver();
        test_method_signature_lookup_uses_type_ast_receiver();
        test_method_signature_list_uses_type_ast_receiver();
        test_static_method_signature_lookup_uses_type_ast_receiver();
        test_inferred_generic_method_uses_type_ast_receiver();
        test_expected_generic_method_uses_type_ast_receiver();
        test_auto_member_call_receiver_uses_type_ast();
        test_auto_member_expr_receiver_uses_type_ast();
        test_native_semantic_tokens();
        test_ast_constructor_assignment_aliases();
        test_ast_index_receiver_type_inference();
        test_statement_ast_shape();
        test_var_decl_name_must_be_identifier();
        test_except_binding_name_must_be_identifier();
        test_unsupported_statement_ast_shape();
        test_unsupported_def_expression_ast_shape();
        test_unsupported_comprehension_ast_shape();
        test_unsupported_dynamic_call_ast_shape();
        test_literal_ast_values();
        test_expression_ast_shape();
        test_cpp_escape_ast_payloads();
        test_dereference_postfix_expression_shape();
        test_decorator_expression_ast_shape();
        test_type_ast_shape();
        test_malformed_static_field_type_is_rejected();
        test_malformed_declaration_type_syntax_is_rejected();
        test_generic_decl_ast_shape();
        test_payload_enum_ast_shape();
        test_match_case_ast_shape();
        test_wrapper_match_type_uses_type_ast();
        test_member_completion_target_uses_tokens();
        test_member_candidate_types_use_type_refs();
        test_signature_help_call_site_uses_tokens();
        test_ast_expr_path_at_cursor();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
