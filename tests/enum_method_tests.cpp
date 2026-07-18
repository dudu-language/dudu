#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"

#include <cassert>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view enum_source =
    "enum State:\n"
    "    Ready\n"
    "    Done\n"
    "\n"
    "    def is_done(self) -> bool:\n"
    "        return self == State.Done\n"
    "\n"
    "    def make(value: i32) -> State:\n"
    "        if value > 0:\n"
    "            return State.Done\n"
    "        return State.Ready\n"
    "\n"
    "    def keep[T](self, value: T) -> T:\n"
    "        return value\n"
    "\n"
    "    def score(self, value: i32) -> i32:\n"
    "        return value\n"
    "\n"
    "    def score(self, value: bool) -> i32:\n"
    "        if value:\n"
    "            return 1\n"
    "        return 0\n"
    "\n"
    "def generic_is_done[T](state: &const[T]) -> bool:\n"
    "    return state.is_done()\n"
    "\n"
    "def generic_make[T](value: i32) -> T:\n"
    "    return T.make(value)\n"
    "\n"
    "def main() -> i32:\n"
    "    state = generic_make[State](1)\n"
    "    if generic_is_done(state):\n"
    "        return state.keep[i32](state.score(42))\n"
    "    return state.score(False)\n";

void test_enum_methods_parse_analyze_and_emit() {
    const dudu::ModuleAst module = dudu::parse_source(enum_source, "enum_methods.dd");
    assert(module.enums.size() == 1);
    const dudu::EnumDecl& state = module.enums.front();
    assert(state.methods.size() == 5);
    assert(state.methods.front().name == "is_done");
    assert(state.methods.front().params.front().name == "self");
    assert(state.methods[1].name == "make");
    assert(state.methods[1].params.front().name == "value");
    assert(state.methods[2].generic_params.size() == 1);

    dudu::analyze_module(module, {.check_bodies = true});
    const std::string header = dudu::emit_cpp_header(module);
    const std::string source = dudu::emit_cpp_source(module);
    assert(header.find("State__is_done") != std::string::npos);
    assert(header.find("State__keep") != std::string::npos);
    assert(header.find("dudu_dispatch_instance_is_done") != std::string::npos);
    assert(header.find("dudu_dispatch_static_make") != std::string::npos);
    assert(header.find("requires { dudu_dispatch_instance_is_done") != std::string::npos);
    assert(header.find("requires { dudu_dispatch_static_make") != std::string::npos);
    assert(source.find("generic_make<") != std::string::npos);
    assert(source.find("State__keep<int32_t>(state, State__score(state, 42))") !=
           std::string::npos);
}

void test_enum_method_protocol_roundtrip() {
    const dudu::ModuleAst module = dudu::parse_source(enum_source, "enum_protocol.dd");
    const dudu::EnumDecl& source = module.enums.front();
    const dudu::macro::protocol::EnumDecl public_value =
        dudu::macro::to_protocol(source, "fixture");
    assert(public_value.methods.size() == source.methods.size());
    assert(public_value.methods[2].generic_parameters.size() == 1);
    assert(public_value.methods[2].generic_parameters.front().name == "T");
    assert(public_value.methods.front().body.front().value.has_value());

    const dudu::EnumDecl decoded =
        dudu::macro::from_protocol(public_value, "generated", source.location);
    assert(decoded.methods.size() == source.methods.size());
    assert(decoded.methods.front().statements.size() == 1);
    assert(decoded.methods.front().statements.front().kind == dudu::StmtKind::Return);
    assert(decoded.methods[2].generic_params == std::vector<std::string>{"T"});
}

void expect_rejected(std::string_view source, std::string_view message) {
    bool rejected = false;
    std::string actual;
    try {
        const dudu::ModuleAst module = dudu::parse_source(source, "bad_enum_method.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        actual = error.what();
        rejected = actual.find(message) != std::string::npos;
    }
    if (!rejected) {
        throw std::runtime_error("expected diagnostic '" + std::string(message) + "', got '" +
                                 actual + "'");
    }
}

void test_enum_method_failures_are_explicit() {
    expect_rejected("enum Bad:\n"
                    "    Value\n"
                    "\n"
                    "    def score(self, value: i32) -> i32:\n"
                    "        return value\n"
                    "\n"
                    "    def score(self, value: i32) -> i32:\n"
                    "        return value\n",
                    "duplicate method overload");
    expect_rejected("enum Bad:\n"
                    "    Value\n"
                    "\n"
                    "    @operator(\"+\")\n"
                    "    def add(self, other: Bad) -> Bad:\n"
                    "        return self\n",
                    "enum methods cannot overload operators");
    expect_rejected("enum Bad:\n"
                    "    Value\n"
                    "\n"
                    "    def inspect(self: Bad) -> i32:\n"
                    "        return 1\n",
                    "self must be a receiver reference");
}

} // namespace

int main() {
    test_enum_methods_parse_analyze_and_emit();
    test_enum_method_protocol_roundtrip();
    test_enum_method_failures_are_explicit();
    return 0;
}
