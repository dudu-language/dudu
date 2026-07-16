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
    assert(dudu::type_assignment_allowed(
        dudu::parse_type_text("usize"),
        dudu::parse_type_text("std.basic_string._Alloc_traits.size_type")));
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
