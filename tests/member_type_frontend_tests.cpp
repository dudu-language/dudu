#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_semantic_tokens.hpp"
#include "dudu/core/match_patterns.hpp"
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

    assert(dudu::type_ref_head_name(dudu::unwrap_receiver_type_ref(
               symbols, dudu::parse_type_text("*const[AliasBox]"))) == "Box");
    assert(dudu::type_ref_head_name(dudu::unwrap_receiver_type_ref(
               symbols, dudu::parse_type_text("*ConstAliasBox"))) == "Box");
    assert(dudu::receiver_class_name(symbols, dudu::parse_type_text("&struct sqlite3")) ==
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

} // namespace

int main() {
    try {
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
