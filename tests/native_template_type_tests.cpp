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
#include "dudu/native/native_header_ast_parse_internal.hpp"
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

void test_native_header_types_split_cpp_templates() {
    assert(dudu::dudu_type("char *") == "*char");
    assert(dudu::dudu_type("const char *") == "cstr");
    assert(dudu::dudu_type("const vec<L, T, Q> &") == "&const[vec[L, T, Q]]");
    assert(dudu::dudu_type("_Args &&...") == "&&_Args...");
    assert(dudu::dudu_type("typename std::remove_reference<_Tp>::type &&") ==
           "&&std.remove_reference[_Tp].type");
    assert(dudu::dudu_type("tuple<typename __decay_and_strip<_Elements>::__type...>") ==
           "tuple[__decay_and_strip[_Elements].__type...]");
    assert(dudu::dudu_type("typename iterator_traits<_IIter>::difference_type") ==
           "iterator_traits[_IIter].difference_type");
    assert(dudu::dudu_type(
               "typename __gnu_cxx::__alloc_traits<_Alloc>::template rebind<_Tp>::other") ==
           "__gnu_cxx.__alloc_traits[_Alloc].rebind[_Tp].other");
    assert(dudu::dudu_type(
               "typename std::pointer_traits<_Ptr>::template rebind<const _Tp>") ==
           "std.pointer_traits[_Ptr].rebind[const[_Tp]]");
    assert(dudu::dudu_type("int (*)(float, const char *)") == "fn(f32, cstr) -> i32");
    assert(dudu::dudu_type("void (*)(void)") == "fn() -> void");
    assert(dudu::dudu_type("std::size_t") == "usize");
    assert(dudu::dudu_type("std::ptrdiff_t") == "isize");
    assert(dudu::dudu_type("__remove_const(const _Tp)") == "__remove_const[const[_Tp]]");
    assert(dudu::dudu_type("__remove_volatile(_Tp)") == "__remove_volatile[_Tp]");
    assert(dudu::signature_params("int (void)").empty());
    assert(dudu::signature_params("T (const vec<L, T, Q> &, const vec<L, T, Q> &)") ==
           std::vector<std::string>({"&const[vec[L, T, Q]]", "&const[vec[L, T, Q]]"}));
    assert(dudu::signature_params("decltype(auto) (const entity_type, Args &&...)") ==
           std::vector<std::string>({"const[entity_type]", "&&Args..."}));
    assert(dudu::signature_return_type("decltype(auto) (const entity_type, Args &&...)") ==
           "auto");
    const std::string nested_noexcept =
        "auto (_Tp *, _Args &&...) noexcept(noexcept(new _Tp())) -> decltype(new _Tp())";
    assert(dudu::signature_params(nested_noexcept) ==
           std::vector<std::string>({"*_Tp", "&&_Args..."}));
    assert(dudu::signature_return_type(nested_noexcept) == "auto");

    const dudu::TypeRef associated = dudu::parse_type_text("meta.Result[T].value_type");
    assert(associated.kind == dudu::TypeKind::Associated);
    assert(associated.name == "value_type");
    assert(associated.children.size() == 1);
    assert(associated.children.front().kind == dudu::TypeKind::Template);
    assert(dudu::type_ref_text(associated) == "meta.Result[T].value_type");
    assert(dudu::lower_cpp_type(associated) == "typename meta::Result<T>::value_type");

    const dudu::TypeRef nested =
        dudu::parse_type_text("meta.Traits[A].rebind[T].other");
    assert(nested.kind == dudu::TypeKind::Associated);
    assert(nested.children.size() == 1);
    assert(nested.children.front().kind == dudu::TypeKind::AssociatedTemplate);
    assert(nested.children.front().name == "rebind");
    assert(nested.children.front().children.size() == 2);
    assert(dudu::type_ref_text(nested) == "meta.Traits[A].rebind[T].other");
    assert(dudu::lower_cpp_type(nested) ==
           "typename meta::Traits<A>::template rebind<T>::other");

    const dudu::TypeRef nested_const =
        dudu::parse_type_text("std.pointer_traits[Ptr].rebind[const[T]]");
    assert(nested_const.kind == dudu::TypeKind::AssociatedTemplate);
    assert(nested_const.name == "rebind");
    assert(nested_const.children.size() == 2);
    assert(nested_const.children.front().kind == dudu::TypeKind::Template);
    assert(nested_const.children[1].kind == dudu::TypeKind::Const);
    assert(dudu::lower_cpp_type(nested_const) ==
           "typename std::pointer_traits<Ptr>::template rebind<const T>");

    const dudu::TypeRef dependent_value =
        dudu::native_ast_parse::parse_native_type_text(
            "Traits.unique_keys.value", {}, {"Traits"});
    assert(dependent_value.kind == dudu::TypeKind::Associated);
    assert(dependent_value.name == "value");
    assert(dependent_value.children.size() == 1);
    assert(dependent_value.children.front().kind == dudu::TypeKind::Associated);
    assert(dependent_value.children.front().name == "unique_keys");
    assert(dudu::type_ref_text(dependent_value) == "Traits.unique_keys.value");

}

void test_native_template_binding_resolves_alias_type_refs() {
    dudu::Symbols symbols;
    symbols.alias_type_refs["FloatList"] = dudu::parse_type_text("list[f32]");
    dudu::NativeTemplateBindings bindings;
    const dudu::NativeTemplateParameterNames t_param = {"T"};
    assert(dudu::bind_native_template_type_ast(symbols, dudu::parse_type_text("list[T]"),
                                               dudu::parse_type_text("FloatList"), t_param,
                                               bindings));
    assert(bindings.at("T").kind == dudu::TypeKind::Named);
    assert(bindings.at("T").name == "f32");
    assert(dudu::substitute_type_ref_text(bindings.at("T"), {}) == "f32");

    bindings.clear();
    assert(dudu::bind_native_template_type_ast(
        dudu::parse_type_text("Pair[T, T]"), dudu::parse_type_text("Pair[struct sqlite3, sqlite3]"),
        t_param, bindings));
    assert(bindings.at("T").kind == dudu::TypeKind::Named);
    assert(bindings.at("T").name == "struct sqlite3");

    symbols.alias_type_refs["_RAIter"] = dudu::parse_type_text("WrongImportedAlias");
    bindings.clear();
    const dudu::NativeTemplateParameterNames iterator_param = {"_RAIter"};
    assert(dudu::bind_native_template_type_ast(symbols, dudu::parse_type_text("_RAIter"),
                                               dudu::parse_type_text("Iterator[i32]"),
                                               iterator_param, bindings));
    assert(dudu::type_ref_text(bindings.at("_RAIter")) == "Iterator[i32]");
}

void test_bound_native_template_pack_substitution_uses_type_refs() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"T"};
    signature.template_param_is_value = {false};
    signature.variadic = true;
    signature.min_params = 0;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("T"), {});
    dudu::set_signature_param_types(signature, {pack_param});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("tuple[T...]"));

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
    signature.template_param_is_value = {false, false};
    signature.variadic = true;
    signature.min_params = 0;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("T"), {});
    dudu::set_signature_param_types(signature, {pack_param, dudu::parse_type_text("U")});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("tuple[T...]"));

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

void test_template_value_expression_stays_one_argument() {
    const dudu::TypeRef type = dudu::parse_type_text(
        "std.conditional_t[Policy == deletion_policy.in_place, Fast, Slow]");
    assert(type.kind == dudu::TypeKind::Template);
    assert(type.children.size() == 3);
    assert(type.children[0].kind == dudu::TypeKind::Value);
    assert(type.children[0].value == "Policy == deletion_policy.in_place");
    assert(type.children[1].name == "Fast");
    assert(type.children[2].name == "Slow");
}

void test_native_template_match_infers_nested_type_pack() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"Index", "T..."};
    signature.template_param_is_value = {true, false};
    signature.min_params = 1;
    dudu::set_signature_param_types(
        signature, {dudu::parse_type_text("&std.tuple[T...]")});
    dudu::set_signature_return_type(
        signature, dudu::parse_type_text("&std._Nth_type[Index, T...].type"));
    symbols.native_function_signatures["get"] = {signature};

    dudu::FunctionScope scope(symbols);
    scope.local_type_refs["value"] =
        dudu::parse_type_text("std.tuple[std.string, i32]");
    const std::optional<dudu::FunctionSignature> matched =
        dudu::match_native_signature(
            scope, "get", {dudu::parse_type_text("1")},
            {dudu::parse_expr_text("value")}, nullptr);
    assert(matched.has_value());
    const std::string return_type =
        dudu::type_ref_text(dudu::signature_return_type_ref(*matched));
    if (return_type != "&std._Nth_type[1, std.string, i32].type") {
        throw std::runtime_error("nested native type pack resolved as " + return_type);
    }
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

void test_native_variadic_pack_keeps_leading_template_binding() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"T", "Rest"};
    signature.template_param_is_value = {false, false};
    signature.variadic = true;
    signature.min_params = 1;
    dudu::TypeRef pack =
        dudu::pack_expansion_type_ref(dudu::parse_type_text("Rest"), {});
    dudu::set_signature_param_types(
        signature, {dudu::parse_type_text("T"), std::move(pack)});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("T"));
    symbols.native_function_signatures["fold_sum"] = {signature};
    dudu::FunctionScope scope(symbols);

    const std::vector<dudu::Expr> args = {
        dudu::parse_expr_text("1"), dudu::parse_expr_text("2"),
        dudu::parse_expr_text("3"), dudu::parse_expr_text("4")};
    const std::optional<dudu::FunctionSignature> matched =
        dudu::match_native_signature(scope, "fold_sum", {}, args, nullptr);

    assert(matched.has_value());
    assert(dudu::signature_return_type_ref(*matched).name == "i32");
    assert(dudu::signature_param_count(*matched) == 4);
    for (size_t index = 0; index < 4; ++index) {
        assert(dudu::signature_param_type_ref(*matched, index).name == "i32");
    }
}

void test_explicit_native_template_value_args_use_type_refs() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"__i", "T"};
    signature.template_param_is_value = {true, false};
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
    signature.template_param_is_value = {false, false};
    signature.variadic = true;
    signature.min_params = 1;
    dudu::TypeRef pack_param = dudu::pack_expansion_type_ref(dudu::parse_type_text("&Args"), {});
    dudu::set_signature_param_types(signature, {pack_param});
    dudu::set_signature_return_type(signature, dudu::parse_type_text("Box[T]"));

    const dudu::FunctionSignature substituted = dudu::substitute_explicit_template_signature(
        symbols, signature, {dudu::parse_type_text("Node")});
    assert(dudu::signature_param_count(substituted) == 1);
    assert(dudu::signature_param_type_ref(substituted, 0).kind == dudu::TypeKind::PackExpansion);
    assert(dudu::native_template_pack_placeholder(
               dudu::signature_param_type_ref(substituted, 0),
               dudu::native_type_template_parameters(signature)) ==
           std::optional<std::string>{"Args"});
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(substituted)) == "Box[Node]");
}

void test_native_match_finalizes_unbound_packs_as_empty() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    signature.template_params = {"T", "Other...", "Exclude..."};
    signature.template_param_is_value = {false, false, false};
    signature.min_params = 0;
    dudu::set_signature_return_type(
        signature,
        dudu::parse_type_text("View[Get[T, Other...], Without[Exclude...]]"));
    symbols.native_function_signatures["make_view"] = {signature};

    dudu::FunctionScope scope(symbols);
    const std::optional<dudu::FunctionSignature> matched = dudu::match_native_signature(
        scope, "make_view", {dudu::parse_type_text("Position")}, {}, nullptr);
    assert(matched.has_value());
    const std::string result =
        dudu::type_ref_text(dudu::signature_return_type_ref(*matched));
    assert(result.find("Position") != std::string::npos);
    assert(result.find("Other") == std::string::npos);
    assert(result.find("Exclude") == std::string::npos);
    assert(result.find("...") == std::string::npos);
}

void test_native_reference_and_deleted_overload_ranking() {
    dudu::Symbols symbols;
    symbols.types.insert("Widget");

    dudu::FunctionSignature mutable_ref;
    dudu::set_signature_param_types(mutable_ref, {dudu::parse_type_text("&Widget")});
    dudu::set_signature_return_type(mutable_ref, dudu::parse_type_text("i32"));

    dudu::FunctionSignature const_ref;
    dudu::set_signature_param_types(const_ref, {dudu::parse_type_text("&const[Widget]")});
    dudu::set_signature_return_type(const_ref, dudu::parse_type_text("i64"));

    dudu::FunctionSignature rvalue_ref;
    dudu::set_signature_param_types(rvalue_ref, {dudu::parse_type_text("&&Widget")});
    dudu::set_signature_return_type(rvalue_ref, dudu::parse_type_text("u32"));

    dudu::FunctionSignature const_rvalue_ref;
    dudu::set_signature_param_types(const_rvalue_ref,
                                    {dudu::parse_type_text("&&const[Widget]")});
    dudu::set_signature_return_type(const_rvalue_ref, dudu::parse_type_text("f32"));

    dudu::FunctionSignature deleted = mutable_ref;
    deleted.deleted = true;
    dudu::set_signature_return_type(deleted, dudu::parse_type_text("u64"));

    symbols.native_function_signatures["pick"] = {
        const_ref, const_rvalue_ref, rvalue_ref, deleted, mutable_ref};
    dudu::FunctionScope scope(symbols);
    scope.local_type_refs["value"] = dudu::parse_type_text("Widget");
    dudu::SourceLocation location;
    location.line = 1;
    location.column = 1;

    const std::optional<dudu::FunctionSignature> lvalue = dudu::match_native_signature(
        scope, "pick", {}, {dudu::parse_expr_text("value")}, &location);
    assert(lvalue.has_value());
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(*lvalue)) == "i32");

    const std::optional<dudu::FunctionSignature> rvalue = dudu::match_native_signature(
        scope, "pick", {}, {dudu::parse_expr_text("move(value)")}, &location);
    assert(rvalue.has_value());
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(*rvalue)) == "u32");

    dudu::FunctionSignature method;
    method.receiver_type_ref = dudu::parse_type_text("const[Widget]");
    dudu::set_signature_return_type(method, dudu::parse_type_text("i32"));
    dudu::FunctionSignature deleted_method = method;
    deleted_method.deleted = true;
    dudu::set_signature_return_type(deleted_method, dudu::parse_type_text("i64"));

    const std::optional<dudu::FunctionSignature> selected_method =
        dudu::match_native_method_signature(scope, "value.read",
                                            {deleted_method, method}, {},
                                            dudu::parse_expr_text("value"), {}, &location);
    assert(selected_method.has_value());
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(*selected_method)) == "i32");

    bool rejected_deleted_method = false;
    try {
        (void)dudu::match_native_method_signature(scope, "value.read", {deleted_method}, {},
                                                  dudu::parse_expr_text("value"), {}, &location);
    } catch (const dudu::CompileError& error) {
        rejected_deleted_method =
            std::string(error.what()).find("function is deleted") != std::string::npos;
    }
    assert(rejected_deleted_method);
}

} // namespace

int main() {
    try {
        test_native_header_types_split_cpp_templates();
        test_native_template_binding_resolves_alias_type_refs();
        test_bound_native_template_pack_substitution_uses_type_refs();
        test_bound_native_template_substitution_is_per_field();
        test_template_value_expression_stays_one_argument();
        test_native_template_match_infers_nested_type_pack();
        test_native_variadic_bare_pack_uses_type_ref_shape();
        test_native_variadic_pack_keeps_leading_template_binding();
        test_explicit_native_template_value_args_use_type_refs();
        test_explicit_native_template_keeps_unbound_pack_params();
        test_native_match_finalizes_unbound_packs_as_empty();
        test_native_reference_and_deleted_overload_ranking();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
