#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/match_patterns.hpp"
#include "dudu/core/shape_value_expr.hpp"
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
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

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
    assert(player.fields[0].type_ref.children.size() == 3);
    assert(player.fields[0].type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(player.fields[0].type_ref.children[0].name == "array");
    assert(player.fields[0].type_ref.children[1].kind == dudu::TypeKind::Value);
    assert(player.fields[0].type_ref.children[1].value == "4");
    assert(player.fields[0].type_ref.children[2].kind == dudu::TypeKind::Value);
    assert(player.fields[0].type_ref.children[2].value == "4");

    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& update = module.functions[0];
    assert(update.return_type_ref.kind == dudu::TypeKind::Pointer);
    assert(update.params[0].type_ref.kind == dudu::TypeKind::Reference);
    assert(update.params[1].type_ref.kind == dudu::TypeKind::Template);
    assert(update.params[1].type_ref.children[0].name == "str");
    assert(dudu::has_stmt_type_ref(update.statements[0]));
    const dudu::TypeRef& statement_type = dudu::stmt_type_ref(update.statements[0]);
    assert(statement_type.kind == dudu::TypeKind::Const);
    assert(statement_type.range.start.line == 16);
    assert(statement_type.range.start.column > update.statements[0].location.column);
    assert(statement_type.children[0].kind == dudu::TypeKind::Template);

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
    const dudu::TypeRef shaped_tensor = dudu::parse_type_text("Tensor[f32][dyn, 784]");
    assert(shaped_tensor.kind == dudu::TypeKind::Shaped);
    assert(dudu::type_ref_text(shaped_tensor) == "Tensor[f32][dyn, 784]");
    assert(dudu::lower_cpp_type(shaped_tensor) == "Tensor<float>");
    const dudu::TypeRef arithmetic_shape = dudu::parse_type_text("Tensor[T][B, C * H * W]");
    assert(arithmetic_shape.kind == dudu::TypeKind::Shaped);
    assert(arithmetic_shape.children.size() == 3);
    assert(arithmetic_shape.children[1].kind == dudu::TypeKind::Named);
    assert(arithmetic_shape.children[1].name == "B");
    assert(arithmetic_shape.children[2].kind == dudu::TypeKind::Value);
    assert(arithmetic_shape.children[2].value == "C * H * W");
    assert(dudu::type_ref_text(arithmetic_shape) == "Tensor[T][B, C * H * W]");
    const dudu::Expr shape_call = dudu::parse_expr_text("flatten_static[i32, 2, 3, 2, 2](x)");
    assert(shape_call.kind == dudu::ExprKind::TemplateCall);
    assert(dudu::expr_template_type_args(shape_call).size() == 5);
    assert(dudu::type_ref_text(dudu::expr_template_type_args(shape_call)[3]) == "2");
    dudu::FunctionDecl shape_fn;
    shape_fn.name = "flatten_static";
    shape_fn.generic_params = {"T", "B", "C", "H", "W"};
    shape_fn.return_type_ref = arithmetic_shape;
    const dudu::FunctionSignature shape_signature = dudu::instantiate_generic_signature(
        shape_fn,
        {dudu::parse_type_text("i32"), dudu::parse_type_text("2"), dudu::parse_type_text("3"),
         dudu::parse_type_text("2"), dudu::parse_type_text("2")});
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(shape_signature)) ==
           "Tensor[i32][2, 12]");
    const std::map<std::string, dudu::TypeRef> module_type_substitutions{
        {"Tensor", dudu::named_type_ref("tensor.Tensor")},
        {"T", dudu::named_type_ref("f32")},
    };
    const dudu::TypeRef substituted_shaped_tensor = dudu::substitute_type_ref(
        dudu::parse_type_text("Tensor[T][dyn, 2]"), module_type_substitutions);
    assert(dudu::type_ref_text(substituted_shaped_tensor) == "tensor.Tensor[f32][dyn, 2]");
    assert(dudu::substitute_type_ref_text(dudu::parse_type_text("Tensor[T][dyn, 2]"),
                                          {{"Tensor", "tensor.Tensor"}, {"T", "f32"}}) ==
           "tensor.Tensor[f32][dyn, 2]");
    const dudu::TypeRef shaped_box = dudu::parse_type_text("Box[list[i32]][3]");
    assert(shaped_box.kind == dudu::TypeKind::Shaped);
    assert(dudu::lower_cpp_type(shaped_box) == "Box<std::vector<int32_t>>");
    bool rejected_array_shorthand = false;
    try {
        const dudu::ModuleAst bad_array = dudu::parse_source("class Player:\n"
                                                             "    hp: i32\n"
                                                             "\n"
                                                             "def bad():\n"
                                                             "    players: Player[3][4]\n",
                                                             "bad_array_shorthand.dd");
        dudu::analyze_module(bad_array, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected_array_shorthand =
            std::string(error.what()).find("malformed type syntax") != std::string::npos;
    }
    assert(rejected_array_shorthand);
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
    assert(inferred_array.type_ref.children.size() == 3);
    assert(inferred_array.type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(inferred_array.type_ref.children[0].name == "array");
    assert(inferred_array.type_ref.children[1].kind == dudu::TypeKind::Value);
    assert(inferred_array.type_ref.children[1].value == "2");
    assert(inferred_array.type_ref.children[2].kind == dudu::TypeKind::Value);
    assert(inferred_array.type_ref.children[2].value == "2");
    assert(dudu::lower_cpp_type(inferred_array.type_ref) ==
           "std::array<std::array<int32_t, 2>, 2>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("array[f32][4, 4]")) ==
           "std::array<std::array<float, 4>, 4>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("array[f32][2 * 2, 3 + 1]")) ==
           "std::array<std::array<float, 4>, 4>");
    assert(dudu::type_ref_equivalent(dudu::parse_type_text("array[f32][2 * 2, 3 + 1]"),
                                     dudu::parse_type_text("array[f32][4, 4]")));
    assert(dudu::type_ref_equivalent(dudu::parse_type_text("array[f32][4, 4]"),
                                     dudu::parse_type_text("array[f32][4,4]")));
    assert(dudu::type_ref_same_shape(dudu::parse_type_text("array[f32][4, 4]"),
                                     dudu::parse_type_text("array[f32][4,4]")));
    assert(dudu::lower_cpp_type_spelling("array[i32][3]") == "std::array<int32_t, 3>");
    assert(dudu::lower_cpp_type_spelling("array[f32][4, 4]") ==
           "std::array<std::array<float, 4>, 4>");
}

void test_array_literal_shape_inference_is_rank_independent() {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"[1, 2, 3]", "array[i32][3]"},
        {"[[1, 2], [3, 4]]", "array[i32][2, 2]"},
        {"[[[1, 2]], [[3, 4]]]", "array[i32][2, 1, 2]"},
        {"[[[[1, 2]]], [[[3, 4]]]]", "array[i32][2, 1, 1, 2]"},
        {"[[], []]", "array[i32][2, 0]"},
    };
    for (const auto& [literal, expected] : cases) {
        const dudu::ArrayShapeInference inferred = dudu::infer_array_literal_shape_type(
            dudu::parse_type_text("array[i32]"), dudu::parse_expr_text(literal));
        assert(inferred.status == dudu::ArrayShapeStatus::Inferred);
        assert(dudu::type_ref_text(inferred.type_ref) == expected);
    }

    const dudu::Expr ragged = dudu::parse_expr_text("[[1, 2], [3]]");
    const dudu::ArrayShapeInference bad = dudu::infer_array_literal_shape_type(
        dudu::parse_type_text("array[i32]"), ragged);
    assert(bad.status == dudu::ArrayShapeStatus::RaggedLiteral);
    assert(bad.error_location.column == ragged.children[1].location.column);

    const dudu::Expr wrong = dudu::parse_expr_text("[[1, 2, 3], [4, 5, 6]]");
    const dudu::SourceLocation mismatch =
        dudu::array_shape_mismatch_location(wrong, {2, 2}, {2, 3});
    assert(mismatch.column == wrong.children.front().location.column);
}

void test_shape_value_arithmetic_normalizes_and_folds() {
    assert(dudu::shape_value_expr_valid("(H + W) * K / 2 % 7"));
    assert(!dudu::shape_value_expr_valid("H ** 2"));
    assert(dudu::normalize_shape_value_expr("(H+W)*K") == "(H + W) * K");
    assert(dudu::shape_value_expr_substitute("N / 4 + N % 4", {{"N", "10"}}) == "4");
    assert(dudu::shape_value_expr_substitute("(H - K + 1) * W",
                                             {{"H", "5"}, {"K", "3"}, {"W", "4"}}) == "12");
    assert(dudu::shape_value_expr_equivalent("(2 + 2) * 3", "12"));
}

} // namespace

int main() {
    try {
        test_type_ast_shape();
        test_array_literal_shape_inference_is_rank_independent();
        test_shape_value_arithmetic_normalizes_and_folds();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
