#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/core/source.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void test_inferred_auto_assignment_is_not_redeclared() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    value = cpp(\"MAKE_VALUE()\")\n"
                                                      "    value = cpp(\"MAKE_VALUE()\")\n"
                                                      "    return i32(0)\n",
                                                      "inferred_auto_assignment.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto value = MAKE_VALUE();") != std::string::npos);
    assert(cpp.find("value = MAKE_VALUE();") != std::string::npos);
    assert(cpp.find("auto value = MAKE_VALUE();\n    auto value = MAKE_VALUE();") ==
           std::string::npos);
}

void test_expected_generic_method_emission_uses_type_ast_receiver() {
    const dudu::ModuleAst module = dudu::parse_source("class Box[T]:\n"
                                                      "    def make[U](self) -> U:\n"
                                                      "        return U()\n"
                                                      "\n"
                                                      "class Wrapper(Box[f32]):\n"
                                                      "    tag: i32\n"
                                                      "\n"
                                                      "\n"
                                                      "def use(wrapper: Wrapper) -> str:\n"
                                                      "    text: str = wrapper.make()\n"
                                                      "    return text\n",
                                                      "expected_generic_method_type_ast_emit.dd");
    dudu::analyze_module(module, {.check_bodies = false});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::string text = wrapper.make<std::string>();") != std::string::npos);
}

void test_dependent_self_generic_method_uses_template_disambiguator() {
    const dudu::ModuleAst module =
        dudu::parse_source("class Box[T]:\n"
                           "    def write[Writer](self, output: &Writer) -> bool:\n"
                           "        return True\n"
                           "\n"
                           "    def encode[Writer](self, output: &Writer) -> bool:\n"
                           "        ready: bool = self.write[Writer](output)\n"
                           "        return ready\n",
                           "dependent_self_generic_method.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find(".template write<Writer>(output)") != std::string::npos);
}

void test_receiver_reference_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("class Vec3:\n"
                           "    x: f32\n"
                           "\n"
                           "    def length(self: &const[Self]) -> f32:\n"
                           "        return self.x\n"
                           "\n"
                           "    def copy(self) -> Self:\n"
                           "        return self\n"
                           "\n"
                           "    def const_copy(self: &const[Self]) -> Self:\n"
                           "        return self\n"
                           "\n"
                           "    def const_ref(self: &const[Self]) -> &const[Self]:\n"
                           "        return self\n"
                           "\n"
                           "    def normalize(self) -> &Self:\n"
                           "        return self\n"
                           "\n"
                           "    @operator(\"+=\")\n"
                           "    def add_assign(self, other: &const[Self]) -> &Self:\n"
                           "        self.x += other.x\n"
                           "        return self\n",
                           "receiver_reference_emit.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("float length() const") != std::string::npos);
    assert(cpp.find("Vec3 copy()") != std::string::npos);
    assert(cpp.find("Vec3 const_copy() const") != std::string::npos);
    assert(cpp.find("const Vec3& const_ref() const") != std::string::npos);
    assert(cpp.find("Vec3& normalize()") != std::string::npos);
    assert(cpp.find("Vec3& operator+=") != std::string::npos);
    assert(cpp.find("auto& self = *this;") != std::string::npos);
}

void test_class_construction_distinguishes_aggregates_and_constructors() {
    const dudu::ModuleAst module =
        dudu::parse_source("class Enemy:\n"
                           "    hp: i32\n"
                           "    x: f32\n"
                           "\n"
                           "class Tracker:\n"
                           "    value: i32\n"
                           "\n"
                           "    def init(self, value: i32):\n"
                           "        self.value = value\n"
                           "\n"
                           "class Box[T]:\n"
                           "    value: T\n"
                           "\n"
                           "def allocate():\n"
                           "    value = Enemy(100, 2.0)\n"
                           "    generic = Box[i32](7)\n"
                           "    tracker_value = Tracker(42)\n"
                           "    enemy: *Enemy = new[Enemy](100, 2.0)\n"
                           "    tracker: *Tracker = new[Tracker](42)\n"
                           "    values: *list[i32] = new[list[i32]](8)\n",
                           "heap_allocation_emission.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("Enemy{100, 2.0}") != std::string::npos);
    assert(cpp.find("Box<int32_t>{7}") != std::string::npos);
    assert(cpp.find("Tracker(42)") != std::string::npos);
    assert(cpp.find("new Enemy{100, 2.0}") != std::string::npos);
    assert(cpp.find("new Tracker(42)") != std::string::npos);
    assert(cpp.find("new std::vector<int32_t>(8)") != std::string::npos);
}

void test_class_emit_order_uses_type_ast_fields() {
    const dudu::ModuleAst module = dudu::parse_source("class Holder:\n"
                                                      "    items: list[Node]\n"
                                                      "    late: *Late\n"
                                                      "\n"
                                                      "class Late:\n"
                                                      "    value: i32\n"
                                                      "\n"
                                                      "class Node:\n"
                                                      "    value: i32\n"
                                                      "\n"
                                                      "class Derived(Base):\n"
                                                      "    value: i32\n"
                                                      "\n"
                                                      "class Base:\n"
                                                      "    value: i32\n",
                                                      "class_emit_order_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("\nstruct Node {") != std::string::npos);
    assert(cpp.find("\nstruct Holder {") != std::string::npos);
    assert(cpp.find("\nstruct Node {") < cpp.find("\nstruct Holder {"));
    assert(cpp.find("\nstruct Holder {") < cpp.find("\nstruct Late {"));
    assert(cpp.find("\nstruct Base {") < cpp.find("\nstruct Derived : public Base {"));
}

void test_class_defaults_use_complete_enums_and_optional_values() {
    const dudu::ModuleAst module = dudu::parse_source("enum State:\n"
                                                      "    Ready\n"
                                                      "    Done\n"
                                                      "\n"
                                                      "class Item:\n"
                                                      "    state: State = State.Ready\n"
                                                      "    value: Option[i32] = None\n",
                                                      "class_enum_optional_defaults.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("enum class State {") < cpp.find("struct Item {"));
    assert(cpp.find("State state = State::Ready;") != std::string::npos);
    assert(cpp.find("std::optional<int32_t> value = std::nullopt;") != std::string::npos);
}

void test_class_and_payload_enum_emit_order_follows_by_value_fields() {
    const dudu::ModuleAst enum_first = dudu::parse_source("enum Event:\n"
                                                          "    Move:\n"
                                                          "        x: i32\n"
                                                          "\n"
                                                          "class Envelope:\n"
                                                          "    event: Event\n",
                                                          "payload_enum_before_class.dd");
    dudu::analyze_module(enum_first, {.check_bodies = true});
    const std::string enum_first_cpp = dudu::emit_cpp_source(enum_first);
    assert(enum_first_cpp.find("struct Event {") < enum_first_cpp.find("struct Envelope {"));

    const dudu::ModuleAst class_first = dudu::parse_source("enum Shape:\n"
                                                           "    Dot:\n"
                                                           "        point: Point\n"
                                                           "\n"
                                                           "class Point:\n"
                                                           "    x: i32\n",
                                                           "class_before_payload_enum.dd");
    dudu::analyze_module(class_first, {.check_bodies = true});
    const std::string class_first_cpp = dudu::emit_cpp_source(class_first);
    assert(class_first_cpp.find("struct Point {") < class_first_cpp.find("struct Shape {"));
}

void test_operator_continuation_is_part_of_return_expression() {
    const dudu::ModuleAst module = dudu::parse_source("def value(x: i32) -> i32:\n"
                                                      "    return x\n"
                                                      "        + 1\n"
                                                      "        + 2\n",
                                                      "operator_continuation_return.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("return ((x + 1) + 2);") != std::string::npos);
}

} // namespace

int main() {
    try {
        test_inferred_auto_assignment_is_not_redeclared();
        test_expected_generic_method_emission_uses_type_ast_receiver();
        test_dependent_self_generic_method_uses_template_disambiguator();
        test_receiver_reference_emission();
        test_class_construction_distinguishes_aggregates_and_constructors();
        test_class_emit_order_uses_type_ast_fields();
        test_class_defaults_use_complete_enums_and_optional_values();
        test_class_and_payload_enum_emit_order_follows_by_value_fields();
        test_operator_continuation_is_part_of_return_expression();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
