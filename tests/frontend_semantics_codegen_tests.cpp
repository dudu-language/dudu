#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/format/format.hpp"
#include "dudu/format/format_path.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_support.hpp"
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

void test_list_iterator_methods() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    values: list[i32] = [1, 2]\n"
                                                      "    values.begin()\n"
                                                      "    values.end()\n"
                                                      "    return 0\n",
                                                      "list_iter.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_reference_list_indexing() {
    const dudu::ModuleAst module = dudu::parse_source("def write(values: &list[i32]):\n"
                                                      "    values[0] = 5\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    values: list[i32] = [1]\n"
                                                      "    write(values)\n"
                                                      "    return values[0]\n",
                                                      "reference_list_indexing.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_std_map_index_assignment() {
    const dudu::ModuleAst ok = dudu::parse_source("from cpp import map\n"
                                                  "from cpp import string\n"
                                                  "from cpp import unordered_map\n"
                                                  "\n"
                                                  "def main() -> i32:\n"
                                                  "    scores: std.map[std.string, i32]\n"
                                                  "    lookup: std.unordered_map[std.string, i32]\n"
                                                  "    key: std.string\n"
                                                  "    scores[key] = 20\n"
                                                  "    lookup[key] = scores[key] + 2\n"
                                                  "    return lookup[key]\n",
                                                  "std_map_index_assignment.dd");
    dudu::analyze_module(ok, {.check_bodies = true});

    bool rejected = false;
    try {
        const dudu::ModuleAst bad =
            dudu::parse_source("from cpp import string\n"
                               "from cpp import unordered_map\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    scores: std.unordered_map[std.string, i32]\n"
                               "    key: std.string\n"
                               "    scores[key] = True\n"
                               "    return 0\n",
                               "bad_std_map_index_assignment.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected = std::string(error.what()).find("cannot assign bool to i32") != std::string::npos;
    }
    assert(rejected);
}

void test_negative_numeric_literals_contextualize_as_f32_args() {
    const dudu::ModuleAst module = dudu::parse_source("def take_f32(x: f32, y: f32):\n"
                                                      "    pass\n"
                                                      "\n"
                                                      "class Player:\n"
                                                      "    x: f32\n"
                                                      "    y: f32\n"
                                                      "\n"
                                                      "    def move(self, dx: f32, dy: f32):\n"
                                                      "        self.x += dx\n"
                                                      "        self.y += dy\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    take_f32(-2.0, -1.0)\n"
                                                      "    player = Player(x=1.0, y=2.0)\n"
                                                      "    player.move(2.0, -1.0)\n"
                                                      "    return 0\n",
                                                      "negative_numeric_literal_args.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_receiver_reference_semantics() {
    const dudu::ModuleAst ok =
        dudu::parse_source("class Vec3:\n"
                           "    x: f32\n"
                           "\n"
                           "    def mutable_value(self) -> Self:\n"
                           "        return self\n"
                           "\n"
                           "    def mutable_ref(self) -> &Self:\n"
                           "        return self\n"
                           "\n"
                           "    def mutable_const_ref(self) -> &const[Self]:\n"
                           "        return self\n"
                           "\n"
                           "    def const_value(self: &const[Self]) -> Self:\n"
                           "        return self\n"
                           "\n"
                           "    def const_ref(self: &const[Self]) -> &const[Self]:\n"
                           "        return self\n"
                           "\n"
                           "    def length(self: &const[Self]) -> f32:\n"
                           "        return self.x\n"
                           "\n"
                           "    def normalize(self) -> &Self:\n"
                           "        return self\n",
                           "receiver_reference_semantics.dd");
    dudu::analyze_module(ok, {.check_bodies = true});

    bool rejected_value_receiver = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("class Vec3:\n"
                                                       "    x: f32\n"
                                                       "\n"
                                                       "    def bad(self: Vec3) -> f32:\n"
                                                       "        return self.x\n",
                                                       "bad_receiver_value.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected_value_receiver =
            std::string(error.what()).find("self must be a receiver reference") !=
            std::string::npos;
    }
    assert(rejected_value_receiver);

    bool rejected_const_to_mut_ref = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("class Vec3:\n"
                                                       "    x: f32\n"
                                                       "\n"
                                                       "    def bad(self: &const[Self]) -> &Self:\n"
                                                       "        return self\n",
                                                       "bad_const_receiver_mut_ref_return.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected_const_to_mut_ref =
            std::string(error.what())
                .find("return type mismatch: expected &Self, got &const[Self]") !=
            std::string::npos;
    }
    assert(rejected_const_to_mut_ref);

    bool rejected_const_mutation = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("class Vec3:\n"
                                                       "    x: f32\n"
                                                       "\n"
                                                       "    def bad(self: &const[Self]):\n"
                                                       "        self.x = 1.0\n",
                                                       "bad_const_receiver_mutation.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected_const_mutation =
            std::string(error.what()).find("cannot assign to member through const receiver") !=
            std::string::npos;
    }
    assert(rejected_const_mutation);
}

void test_pointer_dereference_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def read(ptr: *const[i32]) -> const[i32]:\n"
                                                      "    return *ptr\n"
                                                      "\n"
                                                      "def write(ptr: *i32):\n"
                                                      "    *ptr = 5\n",
                                                      "pointer_dereference_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_extern_c_signature_uses_type_ast() {
    const dudu::ModuleAst ok =
        dudu::parse_source("@extern_c\n"
                           "def take_struct(value: *struct NativeThing) -> void:\n"
                           "    return\n",
                           "extern_c_struct_pointer.dd");
    dudu::analyze_module(ok, {.check_bodies = true});

    bool rejected = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("@extern_c\n"
                                                       "def bad_ref(value: &i32) -> void:\n"
                                                       "    return\n",
                                                       "extern_c_ref.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("@extern_c parameter type is not C ABI safe: &") !=
            std::string::npos;
    }
    assert(rejected);

    rejected = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("class Player:\n"
                                                       "    hp: i32\n"
                                                       "\n"
                                                       "@extern_c\n"
                                                       "def bad_class(value: Player) -> void:\n"
                                                       "    return\n",
                                                       "extern_c_class.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("@extern_c parameter type is not C ABI safe: Player") !=
            std::string::npos;
    }
    assert(rejected);
}

void test_pointer_arithmetic_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def next_ptr(ptr: *i32) -> *i32:\n"
                                                      "    return ptr + 1\n",
                                                      "pointer_arithmetic_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_base_pointer_assignment_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("class Base:\n"
                                                      "    @virtual\n"
                                                      "    def id(self) -> i32:\n"
                                                      "        return 1\n"
                                                      "\n"
                                                      "class Derived(Base):\n"
                                                      "    @override\n"
                                                      "    def id(self) -> i32:\n"
                                                      "        return 2\n"
                                                      "\n"
                                                      "def assign(value: *Derived) -> *Base:\n"
                                                      "    base: *Base = value\n"
                                                      "    return base\n",
                                                      "base_pointer_assignment_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_duplicate_base_check_resolves_type_aliases() {
    bool rejected = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("class Base:\n"
                                                          "    id: i32\n"
                                                          "\n"
                                                          "type BaseAlias = Base\n"
                                                          "\n"
                                                          "class Derived(Base, BaseAlias):\n"
                                                          "    value: i32\n",
                                                          "duplicate_base_alias.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("duplicate base class: BaseAlias") != std::string::npos;
    }
    assert(rejected);
}

void test_free_function_overload_signatures_are_unique() {
    const dudu::ModuleAst overloads = dudu::parse_source("def select(value: i32) -> i32:\n"
                                                         "    return value\n"
                                                         "\n"
                                                         "def select(value: f32) -> f32:\n"
                                                         "    return value\n",
                                                         "free_function_overloads.dd");
    dudu::analyze_module(overloads, {.check_bodies = true});

    bool rejected = false;
    try {
        const dudu::ModuleAst duplicate = dudu::parse_source("def select(value: i32) -> i32:\n"
                                                             "    return value\n"
                                                             "\n"
                                                             "def select(value: i32) -> i32:\n"
                                                             "    return value + 1\n",
                                                             "duplicate_free_function_overload.dd");
        dudu::analyze_module(duplicate, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected = error.code() == "dudu.sema.error" &&
                   std::string(error.what()).find("duplicate function overload: select") !=
                       std::string::npos;
    }
    assert(rejected);
}

void test_inherited_method_identity_resolves_type_aliases() {
    bool rejected = false;
    try {
        const dudu::ModuleAst module =
            dudu::parse_source("class Node:\n"
                               "    id: i32\n"
                               "\n"
                               "class Payload:\n"
                               "    value: i32\n"
                               "\n"
                               "type PayloadAlias = Payload\n"
                               "\n"
                               "class Named:\n"
                               "    @abstract\n"
                               "    def name(self) -> str:\n"
                               "\n"
                               "    def label(self, payload: Payload) -> str:\n"
                               "        return self.name()\n"
                               "\n"
                               "class Titled:\n"
                               "    @abstract\n"
                               "    def title(self) -> str:\n"
                               "\n"
                               "    def label(self, payload: PayloadAlias) -> str:\n"
                               "        return self.title()\n"
                               "\n"
                               "class Sprite(Node, Named, Titled):\n"
                               "    @override\n"
                               "    def name(self) -> str:\n"
                               "        return \"sprite\"\n"
                               "\n"
                               "    @override\n"
                               "    def title(self) -> str:\n"
                               "        return \"sprite\"\n",
                               "ambiguous_inherited_alias_signature.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("ambiguous inherited concrete method: label(Payload)") !=
            std::string::npos;
    }
    assert(rejected);
}

void test_bare_void_return() {
    const dudu::ModuleAst module = dudu::parse_source("def done():\n"
                                                      "    return\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    done()\n"
                                                      "    return 0\n",
                                                      "bare_void_return.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_typed_for_emission() {
    const dudu::ModuleAst module = dudu::parse_source("class Item:\n"
                                                      "    value: i32\n"
                                                      "\n"
                                                      "type ItemAlias = Item\n"
                                                      "\n"
                                                      "def sum_items(items: list[Item]) -> i32:\n"
                                                      "    total: i32 = 0\n"
                                                      "    for index in range(3):\n"
                                                      "        total += index\n"
                                                      "    for item: &Item in items:\n"
                                                      "        total += item.value\n"
                                                      "    for alias_item: ItemAlias in items:\n"
                                                      "        total += alias_item.value\n"
                                                      "    for copy in items:\n"
                                                      "        total += copy.value\n"
                                                      "    return total\n",
                                                      "typed_for.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("for (int32_t index = 0; index < 3; index += 1)") != std::string::npos);
    assert(cpp.find("for (Item& item : items)") != std::string::npos);
    assert(cpp.find("for (ItemAlias alias_item : items)") != std::string::npos);
    assert(cpp.find("for (auto&& copy : items)") != std::string::npos);
}

void test_read_only_dict_iteration_emits_key_binding() {
    const dudu::ModuleAst module = dudu::parse_source(
        "def keys(values: &const[dict[str, i32]]) -> list[str]:\n"
        "    result: list[str] = []\n"
        "    for key in values:\n"
        "        result.append(key)\n"
        "    return result\n",
        "dict_iteration_codegen.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto&& [key, dudu_internal_discard_") != std::string::npos);
    assert(cpp.find("for (auto&& key : values)") == std::string::npos);
}

void test_named_aggregate_emission_orders_fields_and_lowers_fixed_arrays() {
    const dudu::ModuleAst module = dudu::parse_source(
        "class MatrixOwner:\n"
        "    first: i32\n"
        "    matrix: array[i32][2, 2]\n"
        "    last: i32\n"
        "\n"
        "def make_owner() -> MatrixOwner:\n"
        "    return MatrixOwner(last=9, matrix=[[1, 2], [3, 4]], first=7)\n",
        "aggregate_fixed_array_codegen.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    const size_t first = cpp.find(".first = 7");
    const size_t matrix = cpp.find(
        ".matrix = std::array<std::array<int32_t, 2>, 2>{"
        "std::array<int32_t, 2>{1, 2}, std::array<int32_t, 2>{3, 4}}");
    const size_t last = cpp.find(".last = 9");
    assert(first != std::string::npos);
    assert(matrix != std::string::npos);
    assert(last != std::string::npos);
    assert(first < matrix && matrix < last);
}

void test_class_field_defaults_and_static_fields() {
    const dudu::ModuleAst module = dudu::parse_source("class Counter:\n"
                                                      "    value: i32 = 7\n"
                                                      "    count: static[i32] = 0\n"
                                                      "\n"
                                                      "    def bump() -> i32:\n"
                                                      "        Counter.count += 1\n"
                                                      "        return Counter.count\n",
                                                      "class_defaults.dd");
    assert(module.classes.size() == 1);
    const dudu::ClassDecl& counter = module.classes.front();
    assert(counter.fields.size() == 1);
    assert(counter.fields[0].name == "value");
    assert(counter.fields[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(counter.fields[0].value_expr.value == "7");
    assert(counter.static_fields.size() == 1);
    assert(counter.static_fields[0].name == "count");
    assert(dudu::type_ref_text(counter.static_fields[0].type_ref) == "i32");
    assert(counter.methods.size() == 1);

    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("int32_t value = 7;") != std::string::npos);
    assert(cpp.find("inline static int32_t count = 0;") != std::string::npos);
    assert(cpp.find("static int32_t bump()") != std::string::npos);
}

} // namespace

int main() {
    try {
        test_list_iterator_methods();
        test_reference_list_indexing();
        test_std_map_index_assignment();
        test_negative_numeric_literals_contextualize_as_f32_args();
        test_receiver_reference_semantics();
        test_pointer_dereference_uses_type_ast();
        test_extern_c_signature_uses_type_ast();
        test_pointer_arithmetic_uses_type_ast();
        test_base_pointer_assignment_uses_type_ast();
        test_duplicate_base_check_resolves_type_aliases();
        test_free_function_overload_signatures_are_unique();
        test_inherited_method_identity_resolves_type_aliases();
        test_bare_void_return();
        test_typed_for_emission();
        test_read_only_dict_iteration_emits_key_binding();
        test_named_aggregate_emission_orders_fields_and_lowers_fixed_arrays();
        test_class_field_defaults_and_static_fields();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
