#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/format/format.hpp"
#include "dudu/format/format_path.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_alloc.hpp"
#include "dudu/sema/sema_expr_cpp_escape_calls.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/type_compat.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

dudu::TypeRef infer_emitted_local_type_ref_for_test(
    std::string_view expr_text, const std::map<std::string, std::string>& locals,
    const std::map<std::string, dudu::TypeRef>& function_returns) {
    std::map<std::string, dudu::TypeRef> local_type_refs;
    for (const auto& [name, type] : locals) {
        local_type_refs[name] = dudu::parse_type_text(type);
    }
    const dudu::Symbols symbols;
    return dudu::infer_emitted_local_type_ref(dudu::parse_expr_text(expr_text), local_type_refs,
                                              function_returns, &symbols);
}

void expect_emitted_local_type(std::string_view expr_text,
                               const std::map<std::string, std::string>& locals,
                               const std::map<std::string, dudu::TypeRef>& function_returns,
                               std::string_view expected) {
    const dudu::TypeRef inferred =
        infer_emitted_local_type_ref_for_test(expr_text, locals, function_returns);
    assert(dudu::has_type_ref(inferred));
    assert(dudu::type_ref_equivalent(inferred, dudu::parse_type_text(expected)) ||
           dudu::substitute_type_ref_text(inferred, {}) == expected);
}

void test_allocation_type_ref_diagnostics() {
    dudu::Symbols symbols;
    const dudu::FunctionScope scope(symbols);
    const dudu::SourceLocation location{
        .file = dudu::SourceFileName("cpp_escape_alloc.dd"), .line = 7, .column = 12};

    const std::vector<dudu::TypeRef> type_args = {dudu::parse_type_text("list[i32]", location)};
    const std::optional<dudu::TypeRef> allocation =
        dudu::infer_allocation_call_type_ref(symbols, &location, "new", type_args, 0);
    assert(allocation.has_value());
    assert(allocation->kind == dudu::TypeKind::Pointer);
    assert(allocation->children.size() == 1);
    assert(allocation->children.front().kind == dudu::TypeKind::Template);
    assert(dudu::substitute_type_ref_text(*allocation, {}) == "*list[i32]");
    assert(dudu::substitute_type_ref_text(
               dudu::infer_cpp_escape_expr_ref(scope, "new[list[i32]]()", &location), {}) ==
           "*list[i32]");
    assert(dudu::substitute_type_ref_text(
               dudu::infer_cpp_escape_expr_ref(scope, "*struct State(None)", &location), {}) ==
           "*struct State");
    const std::optional<dudu::EscapeCall> parsed_template_call =
        dudu::parsed_escape_call(dudu::parse_expr_text("Box[i32](7)", location));
    assert(parsed_template_call.has_value());
    assert(parsed_template_call->callee == "Box");
    assert(parsed_template_call->callee_type_ref.kind == dudu::TypeKind::Template);
    assert(dudu::substitute_type_ref_text(parsed_template_call->callee_type_ref, {}) == "Box[i32]");

    bool rejected = false;
    try {
        (void)dudu::infer_cpp_escape_expr_ref(scope, "new[list[MissingType]]()", &location);
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 7);
        assert(error.location().column > location.column);
        assert(std::string(error.what()).find("unknown allocation type: MissingType") !=
               std::string::npos);
        rejected = true;
    }
    assert(rejected);
}

void test_emitted_local_index_type_inference() {
    const std::map<std::string, std::string> locals = {
        {"flag", "bool"},
        {"count", "i32"},
        {"scale", "f32"},
        {"values", "list[i32]"},
        {"names", "dict[str, Player]"},
        {"matrix", "array[f32][3, 4]"},
    };
    const std::map<std::string, dudu::TypeRef> functions;

    expect_emitted_local_type("values[0]", locals, functions, "i32");
    expect_emitted_local_type("names[key]", locals, functions, "Player");
    expect_emitted_local_type("matrix[1]", locals, functions, "array[f32][4]");
    expect_emitted_local_type("matrix[1, 2]", locals, functions, "f32");
}

void test_index_type_inference_uses_type_ast() {
    dudu::Symbols symbols;
    const dudu::SourceLocation location{
        .file = dudu::SourceFileName("index_types.dd"), .line = 1, .column = 1};
    symbols.alias_type_refs["Ints"] = dudu::parse_type_text("list[i32]", location);
    symbols.alias_type_refs["ItemAlias"] = dudu::parse_type_text("Item", location);
    symbols.alias_type_refs["AliasItems"] = dudu::parse_type_text("list[ItemAlias]", location);
    const dudu::TypeRef pointer_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("*const[Item]", location),
        dudu::parse_expr_text("0", location), "items");
    assert(dudu::substitute_type_ref_text(pointer_item, {}) == "Item");
    const dudu::TypeRef indexed_bag_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("Bag[Item]", location),
        dudu::parse_expr_text("0", location), "bag");
    assert(dudu::substitute_type_ref_text(indexed_bag_item, {}) == "Item");
    const dudu::TypeRef dict_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("dict[str, Item]", location),
        dudu::parse_expr_text("key", location), "items");
    assert(dudu::substitute_type_ref_text(dict_item, {}) == "Item");
    const dudu::TypeRef aliased_list_item =
        dudu::indexed_type_ref_from_type(symbols, location, dudu::parse_type_text("Ints", location),
                                         dudu::parse_expr_text("0", location), "ints");
    assert(dudu::substitute_type_ref_text(aliased_list_item, {}) == "i32");
    const dudu::TypeRef nested_alias_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("AliasItems", location),
        dudu::parse_expr_text("0", location), "items");
    assert(dudu::substitute_type_ref_text(nested_alias_item, {}) == "Item");

    symbols.native_types.insert("Vec");
    symbols.alias_type_refs["cv.Vec3b"] = dudu::parse_type_text("Vec[u8, 3]", location);
    const dudu::TypeRef native_fixed_vec_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("&cv.Vec3b", location),
        dudu::parse_expr_text("0", location), "pixel");
    assert(dudu::substitute_type_ref_text(native_fixed_vec_item, {}) == "u8");
    assert(dudu::comparison_rhs_allowed(symbols, ">", dudu::parse_type_text("&u8", location),
                                        dudu::parse_expr_text("0", location),
                                        dudu::parse_type_text("i32", location)));
    assert(dudu::binary_rhs_allowed(
        symbols, "-", dudu::parse_type_text("std.basic_string.size_type", location),
        dudu::parse_expr_text("offset", location), dudu::parse_type_text("usize", location)));
    assert(dudu::binary_rhs_allowed(
        symbols, "+", dudu::parse_type_text("std.vector.difference_type", location),
        dudu::parse_expr_text("1", location), dudu::parse_type_text("i32", location)));

    const dudu::TypeRef matrix_type = dudu::parse_type_text("array[list[i32]][3, 4]");
    const dudu::TypeRef row_type = dudu::indexed_type_ref_from_type(
        symbols, location, matrix_type, dudu::parse_expr_text("1", location), "matrix");
    assert(row_type.kind == dudu::TypeKind::FixedArray);
    assert(dudu::substitute_type_ref_text(row_type, {}) == "array[list[i32]][4]");

    const dudu::TypeRef cell_type = dudu::indexed_type_ref_from_type(
        symbols, location, matrix_type, dudu::parse_expr_text("1, 2", location), "matrix");
    assert(cell_type.kind == dudu::TypeKind::Template);
    assert(cell_type.name == "list");
    assert(dudu::substitute_type_ref_text(cell_type, {}) == "list[i32]");

    const dudu::TypeRef dense_matrix_type = dudu::parse_type_text("array[i32][3, 4]");
    const dudu::TypeRef column_type = dudu::indexed_type_ref_from_type(
        symbols, location, dense_matrix_type, dudu::parse_expr_text(":, 1", location), "matrix");
    assert(column_type.kind == dudu::TypeKind::Shaped);
    assert(dudu::substitute_type_ref_text(column_type, {}) == "array_view[i32][3]");

    const dudu::TypeRef row_span_type = dudu::indexed_type_ref_from_type(
        symbols, location, dense_matrix_type, dudu::parse_expr_text("1, :", location), "matrix");
    assert(row_span_type.kind == dudu::TypeKind::Shaped);
    assert(dudu::substitute_type_ref_text(row_span_type, {}) == "array_view[i32][4]");

    const dudu::TypeRef patch_type =
        dudu::indexed_type_ref_from_type(symbols, location, dense_matrix_type,
                                         dudu::parse_expr_text("1:3, 2:4", location), "matrix");
    assert(patch_type.kind == dudu::TypeKind::Shaped);
    assert(dudu::substitute_type_ref_text(patch_type, {}) == "array_view[i32][2, 2]");

    const dudu::TypeRef vector_type = dudu::parse_type_text("array[i32][7]");
    const dudu::TypeRef stepped_type = dudu::indexed_type_ref_from_type(
        symbols, location, vector_type, dudu::parse_expr_text("0:6:2", location), "values");
    assert(stepped_type.kind == dudu::TypeKind::Shaped);
    assert(dudu::substitute_type_ref_text(stepped_type, {}) == "array_view[i32][3]");

    const dudu::TypeRef open_stepped_type = dudu::indexed_type_ref_from_type(
        symbols, location, vector_type, dudu::parse_expr_text("1::2", location), "values");
    assert(open_stepped_type.kind == dudu::TypeKind::Shaped);
    assert(dudu::substitute_type_ref_text(open_stepped_type, {}) == "array_view[i32][3]");

    const dudu::TypeRef clipped_type = dudu::indexed_type_ref_from_type(
        symbols, location, vector_type, dudu::parse_expr_text("1:99", location), "values");
    assert(clipped_type.kind == dudu::TypeKind::Shaped);
    assert(dudu::substitute_type_ref_text(clipped_type, {}) == "array_view[i32][6]");

    const std::map<std::string, dudu::TypeRef> local_type_refs = {
        {"bag", dudu::parse_type_text("Bag[Item]", location)},
    };
    const std::optional<dudu::TypeRef> bag_item =
        dudu::iterable_value_type_ref(local_type_refs, "bag");
    assert(bag_item);
    assert(dudu::substitute_type_ref_text(*bag_item, {}) == "Item");

    const dudu::ModuleAst module = dudu::parse_source("class Tensor:\n"
                                                      "    @operator(\"[]\")\n"
                                                      "    def get(self, index: i32) -> f32:\n"
                                                      "        return 1.0\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    tensor = Tensor()\n"
                                                      "    value: f32 = tensor[0]\n"
                                                      "    return i32(value)\n",
                                                      "index_types.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_explicit_shape_value_generic_call_inference() {
    const dudu::ModuleAst module =
        dudu::parse_source("class Tensor[T]:\n"
                           "    data: list[T]\n"
                           "\n"
                           "    def flatten(self) -> Tensor[T]:\n"
                           "        return self\n"
                           "\n"
                           "def flatten_static[T, B, C, H, W](\n"
                           "    value: Tensor[T][B, C, H, W],\n"
                           ") -> Tensor[T][B, C * H * W]:\n"
                           "    flat = value.flatten()\n"
                           "    return assume_shape[Tensor[T][B, C * H * W]](flat)\n",
                           "shape_generic_call.dd");
    dudu::Symbols symbols = dudu::collect_symbols(module);
    dudu::check_declarations(module, symbols);
    dudu::FunctionScope scope(symbols);
    scope.local_type_refs["x"] = dudu::parse_type_text("Tensor[i32][2, 3, 2, 2]");
    const dudu::Expr call = dudu::parse_expr_text("flatten_static[i32, 2, 3, 2, 2](x)");
    const dudu::TypeRef got = dudu::infer_expr_type_ast(scope, call, nullptr);
    assert(dudu::type_ref_text(got) == "Tensor[i32][2, 12]");
    const dudu::SourceLocation location{
        .file = dudu::SourceFileName("shape_generic_call.dd"), .line = 9, .column = 12};
    const dudu::TypeRef checked_got = dudu::infer_expr_type_ast(scope, call, &location);
    assert(dudu::type_ref_text(checked_got) == "Tensor[i32][2, 12]");
    const dudu::TypeRef emitted_got =
        dudu::infer_emitted_local_type_ref(call, scope.local_type_refs, {}, &symbols);
    assert(dudu::type_ref_text(emitted_got) == "Tensor[i32][2, 12]");
}

void test_direct_call_return_type_inference_uses_type_ast() {
    const dudu::ModuleAst module =
        dudu::parse_source("def make_matrix() -> array[i32][2, 2]:\n"
                           "    out: array[i32][2, 2] = [[1, 2], [3, 4]]\n"
                           "    return out\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    matrix = make_matrix()\n"
                           "    value: i32 = matrix[1, 1]\n"
                           "    return value\n",
                           "call_return_type.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_emitted_local_expression_type_inference() {
    const std::map<std::string, std::string> locals = {
        {"flag", "bool"},
        {"count", "i32"},
        {"player", "*const[Player]"},
        {"item_ptr", "*Item"},
        {"queue", "storage[Queue[i32]]"},
        {"scale", "f32"},
    };
    const std::map<std::string, dudu::TypeRef> functions = {
        {"make_matrix", dudu::parse_type_text("array[i32][2, 2]")},
        {"make_values", dudu::parse_type_text("list[i32]")},
        {"make_count", dudu::parse_type_text("i32")},
        {"Player.hp", dudu::parse_type_text("i32")},
        {"Queue.pop", dudu::parse_type_text("i32")},
    };

    expect_emitted_local_type("True", locals, functions, "bool");
    expect_emitted_local_type("1_024", locals, functions, "i32");
    expect_emitted_local_type("1.5", locals, functions, "f64");
    expect_emitted_local_type("\"hi\"", locals, functions, "str");
    expect_emitted_local_type("&count", locals, functions, "*i32");
    expect_emitted_local_type("*&count", locals, functions, "i32");
    expect_emitted_local_type("*item_ptr", locals, functions, "Item");
    expect_emitted_local_type("not flag", locals, functions, "bool");
    expect_emitted_local_type("count + 2", locals, functions, "i32");
    expect_emitted_local_type("scale + 2", locals, functions, "f32");
    expect_emitted_local_type("count < 4", locals, functions, "bool");
    expect_emitted_local_type("make_count()", locals, functions, "i32");
    expect_emitted_local_type("player.hp()", locals, functions, "i32");
    expect_emitted_local_type("queue.pop()", locals, functions, "i32");
    expect_emitted_local_type("make_values()[0]", locals, functions, "i32");
    expect_emitted_local_type("make_matrix()[1][0]", locals, functions, "i32");
    expect_emitted_local_type("[1, 2, 3]", locals, functions, "list[i32]");
    expect_emitted_local_type("{\"one\": 1, \"two\": 2}", locals, functions,
                              "dict[str, i32]");
    expect_emitted_local_type("{\"one\", \"two\"}", locals, functions, "set[str]");
    expect_emitted_local_type("[[1, 2], [3, 4]]", locals, functions, "list[list[i32]]");

    const dudu::ModuleAst module = dudu::parse_source("class lowercase:\n"
                                                      "    value: i32\n",
                                                      "lowercase_constructor_type.dd");
    const dudu::Symbols symbols = dudu::collect_symbols(module);
    const dudu::TypeRef constructed =
        dudu::infer_emitted_local_type_ref(dudu::parse_expr_text("lowercase(1)"), {}, {}, &symbols);
    assert(dudu::type_ref_text(constructed) == "lowercase");
}

void test_collection_literal_binding_inference() {
    const dudu::ModuleAst module = dudu::parse_source(
        "def first(values: list[i32]) -> i32:\n"
        "    return values[0]\n"
        "\n"
        "def fractions() -> list[f32]:\n"
        "    return [1.0, 2]\n"
        "\n"
        "def main() -> i32:\n"
        "    numbers = [1, 2, 3]\n"
        "    scores = {\"ada\": 20, \"bob\": 22}\n"
        "    names = {\"ada\", \"bob\"}\n"
        "    nested = [[1, 2], [3, 4]]\n"
        "    groups = {\"a\": {\"x\": 5}, \"b\": {\"x\": 6}}\n"
        "    wide: dict[str, u64] = {\"count\": 9}\n"
        "    offsets: set[usize] = {1, 2}\n"
        "    mixed: list[variant[i32, str]] = [1, \"two\"]\n"
        "    numbers.append(4)\n"
        "    total = 0\n"
        "    for number in numbers:\n"
        "        total += number\n"
        "    return first(numbers) + scores[\"bob\"] + nested[1][0] + "
        "groups[\"a\"][\"x\"] + total\n",
        "collection_literal_inference.dd");
    dudu::analyze_module(module, {.check_bodies = true});

    const auto expect_rejected = [](std::string_view declaration, std::string_view expected) {
        bool rejected = false;
        try {
            const dudu::ModuleAst bad = dudu::parse_source(
                "def main() -> i32:\n    " + std::string(declaration) + "\n    return 0\n",
                "bad_collection_literal.dd");
            dudu::analyze_module(bad, {.check_bodies = true});
        } catch (const dudu::CompileError& error) {
            rejected = std::string(error.what()).find(expected) != std::string::npos;
        }
        assert(rejected);
    };
    expect_rejected("items = []", "cannot infer type of empty collection literal");
    expect_rejected("items = [1, \"two\"]", "list element type mismatch");
    expect_rejected("items = {\"one\": 1, \"two\": False}", "dict value type mismatch");
    expect_rejected("items = {1, \"two\"}", "set element type mismatch");
}

void test_fixed_array_literal_inference_and_numeric_context() {
    const dudu::ModuleAst module = dudu::parse_source(
        "def main() -> i32:\n"
        "    line: array[i32] = [1, 2, 3]\n"
        "    matrix: array[f32] = [[1.0, 2.0], [3.0, 4.0]]\n"
        "    cube: array[u8] = [[[1, 2]], [[3, 4]]]\n"
        "    volume: array[i64] = [[[[1, 2]]], [[[3, 4]]]]\n"
        "    zero_inner: array[i32] = [[], []]\n"
        "    return line[0] + i32(matrix[1, 1]) + i32(cube[1, 0, 0]) + "
        "i32(volume[1, 0, 0, 0])\n",
        "fixed_array_inference.dd");
    dudu::analyze_module(module, {.check_bodies = true});

    const auto expect_rejected = [](const std::string& source, std::string_view expected,
                                    int expected_line) {
        bool rejected = false;
        try {
            const dudu::ModuleAst bad = dudu::parse_source(source, "bad_fixed_array.dd");
            dudu::analyze_module(bad, {.check_bodies = true});
        } catch (const dudu::CompileError& error) {
            rejected = error.location().line == expected_line &&
                       std::string(error.what()).find(expected) != std::string::npos;
        }
        assert(rejected);
    };
    expect_rejected("def main() -> i32:\n    values: array[i32] = []\n    return 0\n",
                    "array shape cannot be inferred from an empty literal", 2);
    expect_rejected("def main() -> i32:\n"
                    "    values: array[i32] = [\n"
                    "        [1, 2],\n"
                    "        [3],\n"
                    "    ]\n"
                    "    return 0\n",
                    "ragged array literal", 4);
    expect_rejected("def main() -> i32:\n"
                    "    values: array[i32][2, 2] = [\n"
                    "        [1, 2, 3],\n"
                    "        [4, 5, 6],\n"
                    "    ]\n"
                    "    return 0\n",
                    "array literal shape mismatch: expected [2, 2], got [2, 3]", 3);
}

void test_tuple_expression_inference_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    count: i32 = 7\n"
                                                      "    name: str = \"dudu\"\n"
                                                      "    pair: tuple[i32, str] = (count, name)\n"
                                                      "    return count\n",
                                                      "tuple_inference.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_result_constructor_inference_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def ok_value() -> Result[i32, str]:\n"
                                                      "    return Ok(7)\n"
                                                      "\n"
                                                      "def err_value() -> Result[i32, str]:\n"
                                                      "    return Err(\"bad\")\n",
                                                      "result_constructor_inference.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_cpp_range_inference_uses_begin_and_dereference() {
    dudu::ClassDecl iterator;
    iterator.name = "native.Iterator";
    iterator.native_declaration = true;
    dudu::FunctionDecl dereference;
    dereference.name = "operator*";
    dereference.return_type_ref = dudu::parse_type_text("&const[i32]");
    iterator.methods.push_back(std::move(dereference));

    dudu::ClassDecl range;
    range.name = "native.Range";
    range.native_declaration = true;
    dudu::FunctionDecl begin;
    begin.name = "begin";
    begin.return_type_ref = dudu::parse_type_text("native.Iterator");
    range.methods.push_back(std::move(begin));

    dudu::Symbols symbols;
    symbols.classes.emplace(iterator.name, &iterator);
    symbols.classes.emplace(range.name, &range);
    const std::optional<dudu::TypeRef> element = dudu::iterable_type_ref_from_type(
        symbols, dudu::parse_type_text("native.Range"));
    assert(element);
    assert(dudu::type_ref_text(*element) == "i32");
}

} // namespace

int main() {
    try {
        test_allocation_type_ref_diagnostics();
        test_emitted_local_index_type_inference();
        test_index_type_inference_uses_type_ast();
        test_explicit_shape_value_generic_call_inference();
        test_direct_call_return_type_inference_uses_type_ast();
        test_emitted_local_expression_type_inference();
        test_collection_literal_binding_inference();
        test_fixed_array_literal_inference_and_numeric_context();
        test_tuple_expression_inference_uses_type_ast();
        test_result_constructor_inference_uses_type_ast();
        test_cpp_range_inference_uses_begin_and_dereference();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
