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
#include <string_view>
#include <vector>

namespace {

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

    const dudu::Expr matrix_slice = dudu::parse_expr_text("matrix[:, 1]");
    assert(dudu::assignment_error(dudu::parse_type_text("array_view[i32][4]"), matrix_slice,
                                  dudu::parse_type_text("array_view[i32][3]")) ==
           "cannot assign array_view[i32][3] to array_view[i32][4] without an explicit cast; "
           "shape mismatch: expected [4], got [3] (axis 0 expected 4, got 3)");
    assert(dudu::assignment_error(dudu::parse_type_text("array_view[i32][dyn]"), matrix_slice,
                                  dudu::parse_type_text("array_view[i32][3]")) ==
           "cannot assign array_view[i32][3] to array_view[i32][dyn] without an explicit cast");
    assert(dudu::assignment_error(dudu::parse_type_text("array_view[i32][3, 1]"), matrix_slice,
                                  dudu::parse_type_text("array_view[i32][3]")) ==
           "cannot assign array_view[i32][3] to array_view[i32][3, 1] without an explicit cast; "
           "shape mismatch: expected rank 2 [3, 1], got rank 1 [3]");
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
    assert(!dudu::assignment_type_allowed(dudu::parse_type_text("std.unique_ptr[Node]"), name_expr,
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

void test_native_dependent_unknowns_require_matching_known_identity() {
    const auto iterator_type = [](bool explicit_unknown, std::string element) {
        dudu::TypeRef metadata;
        metadata.kind = dudu::TypeKind::Template;
        metadata.name = "__impl.Meta";
        if (explicit_unknown) {
            metadata.children.push_back({});
        }
        dudu::TypeRef pointer;
        pointer.kind = dudu::TypeKind::Associated;
        pointer.name = "pointer";
        pointer.children.push_back(std::move(metadata));
        dudu::TypeRef iterator;
        iterator.kind = dudu::TypeKind::Template;
        iterator.name = "__impl.Iterator";
        iterator.children.push_back(std::move(pointer));
        iterator.children.push_back(dudu::parse_type_text("native.Container[" + element + "]"));
        return iterator;
    };

    dudu::Symbols symbols;
    symbols.native_type_identity_by_binding["native.Container"] = "usr:native.Container";
    const dudu::Expr value = dudu::parse_expr_text("value");
    assert(dudu::assignment_type_allowed(symbols, iterator_type(false, "i32"), value,
                                         iterator_type(true, "i32")));
    assert(!dudu::assignment_type_allowed(symbols, iterator_type(false, "i32"), value,
                                          iterator_type(true, "str")));

    dudu::Symbols no_native_identity;
    assert(!dudu::assignment_type_allowed(no_native_identity, iterator_type(false, "i32"), value,
                                          iterator_type(true, "i32")));
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

    assert(!dudu::builtin_cpp_method_signature(
        symbols, dudu::parse_type_text("std::vector<std::string>"), "push_back", signature));

    signature = {};
    assert(dudu::builtin_cpp_method_signature(symbols, dudu::parse_type_text("atomic[u64]"), "load",
                                              signature));
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(signature)) == "u64");

    signature = {};
    assert(dudu::builtin_cpp_method_signature(
        symbols, dudu::parse_type_text("std::atomic<uint64_t>"), "load", signature));
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(signature)) == "u64");
}

void test_native_header_types_split_cpp_templates() {
    assert(dudu::dudu_type("char *") == "*char");
    assert(dudu::dudu_type("const char *") == "cstr");
    assert(dudu::dudu_type("const vec<L, T, Q> &") == "&const[vec[L, T, Q]]");
    assert(dudu::dudu_type("_Args &&...") == "&_Args...");
    assert(dudu::dudu_type("typename std::remove_reference<_Tp>::type &&") ==
           "&std.remove_reference[_Tp].type");
    assert(dudu::dudu_type("tuple<typename __decay_and_strip<_Elements>::__type...>") ==
           "tuple[__decay_and_strip[_Elements].__type...]");
    assert(dudu::dudu_type("typename iterator_traits<_IIter>::difference_type") ==
           "iterator_traits[_IIter].difference_type");
    assert(dudu::dudu_type("int (*)(float, const char *)") == "fn(f32, cstr) -> i32");
    assert(dudu::dudu_type("void (*)(void)") == "fn() -> void");
    assert(dudu::signature_params("int (void)").empty());
    assert(dudu::signature_params("T (const vec<L, T, Q> &, const vec<L, T, Q> &)") ==
           std::vector<std::string>({"&const[vec[L, T, Q]]", "&const[vec[L, T, Q]]"}));

    const dudu::TypeRef associated = dudu::parse_type_text("meta.Result[T].value_type");
    assert(associated.kind == dudu::TypeKind::Associated);
    assert(associated.name == "value_type");
    assert(associated.children.size() == 1);
    assert(associated.children.front().kind == dudu::TypeKind::Template);
    assert(dudu::type_ref_text(associated) == "meta.Result[T].value_type");
    assert(dudu::lower_cpp_type(associated) == "typename meta::Result<T>::value_type");
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

    symbols.alias_type_refs["_RAIter"] = dudu::parse_type_text("WrongImportedAlias");
    bindings.clear();
    assert(dudu::bind_native_template_type_ast(symbols, dudu::parse_type_text("_RAIter"),
                                               dudu::parse_type_text("Iterator[i32]"), bindings));
    assert(dudu::type_ref_text(bindings.at("_RAIter")) == "Iterator[i32]");
}

void test_bound_native_template_pack_substitution_uses_type_refs() {
    dudu::Symbols symbols;
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
        dudu::substitute_bound_template_signature(symbols, signature, {}, packs);
    assert(dudu::signature_param_count(substituted) == 2);
    assert(dudu::signature_param_type_ref(substituted, 0).kind == dudu::TypeKind::Named);
    assert(dudu::signature_param_type_ref(substituted, 0).name == "i32");
    assert(dudu::signature_param_type_ref(substituted, 1).kind == dudu::TypeKind::Named);
    assert(dudu::signature_param_type_ref(substituted, 1).name == "f32");
}

void test_bound_native_template_substitution_is_per_field() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"T", "U"};
    signature.variadic = true;
    signature.min_params = 0;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("T"), {});
    dudu::set_signature_param_types(signature, {pack_param, dudu::parse_type_text("U")});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("tuple[T]"));

    dudu::NativeTemplateBindings bindings;
    const dudu::TypeRef messy_native_binding =
        dudu::parse_type_text("typename __decay_and_strip<U>::__type");
    bindings["U"] = messy_native_binding;

    dudu::NativePackBindingMap packs;
    packs["T"] = {dudu::parse_type_text("i32"), dudu::parse_type_text("f32")};

    const dudu::FunctionSignature substituted =
        dudu::substitute_bound_template_signature(symbols, signature, bindings, packs);
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
    const std::optional<dudu::FunctionSignature> matched =
        dudu::match_native_signature(scope, "native_printf", {}, args, nullptr);
    assert(matched.has_value());
    assert(dudu::signature_param_count(*matched) == 2);
    assert(dudu::signature_param_type_ref(*matched, 1).kind == dudu::TypeKind::PackExpansion);
}

void test_explicit_native_template_value_args_use_type_refs() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"__i", "T"};
    dudu::set_signature_param_types(signature, {dudu::parse_type_text("tuple[T]")});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("T"));

    const dudu::FunctionSignature substituted = dudu::substitute_explicit_template_signature(
        symbols, signature, {dudu::parse_type_text("1"), dudu::parse_type_text("i32")});
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
        dudu::substitute_explicit_template_signature(symbols, malformed_signature,
                                                     {dudu::parse_type_text("f32")});
    assert(dudu::signature_param_type_ref(malformed_substituted, 0).kind ==
           dudu::TypeKind::Unknown);
    assert(dudu::signature_return_type_ref(malformed_substituted).name == "void");
}

void test_explicit_native_template_keeps_unbound_pack_params() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"T", "Args"};
    signature.variadic = true;
    signature.min_params = 1;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("&Args"), {});
    dudu::set_signature_param_types(signature, {pack_param});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("Box[T]"));

    const dudu::FunctionSignature substituted = dudu::substitute_explicit_template_signature(
        symbols, signature, {dudu::parse_type_text("Node")});
    assert(dudu::signature_param_count(substituted) == 1);
    assert(dudu::signature_param_type_ref(substituted, 0).kind == dudu::TypeKind::PackExpansion);
    assert(dudu::native_template_pack_placeholder(dudu::signature_param_type_ref(substituted, 0)) ==
           std::optional<std::string>{"Args"});
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(substituted)) == "Box[Node]");
}

} // namespace

int main() {
    try {
        test_ast_assignment_display_types();
        test_missing_expression_is_not_unknown();
        test_type_compat_uses_type_ast_for_pointers();
        test_native_dependent_unknowns_require_matching_known_identity();
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
