#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/core/decorators.hpp"

#include <sstream>

namespace dudu {

void emit_test_harness(std::ostringstream& out, const ModuleAst& module, const std::string& filter,
                       bool capture_output, const CppEmitOptions& options) {
    out << "namespace dudu_test {\n"
           "struct Capture {\n"
           "    bool enabled = false;\n"
           "    std::ostringstream stream;\n"
           "    std::streambuf* previous = nullptr;\n"
           "    explicit Capture(bool active) : enabled(active) {\n"
           "        if (enabled) { previous = std::cout.rdbuf(stream.rdbuf()); }\n"
           "    }\n"
           "    ~Capture() { restore(); }\n"
           "    void restore() {\n"
           "        if (previous != nullptr) { std::cout.rdbuf(previous); previous = nullptr; }\n"
           "    }\n"
           "    std::string text() const { return stream.str(); }\n"
           "};\n"
           "void print_captured(const std::string& text) {\n"
           "    if (!text.empty()) { std::cout << text; }\n"
           "}\n"
           "template <typename F> bool run_one(const char* name, F fn) {\n"
           "    Capture capture("
        << (capture_output ? "true" : "false")
        << ");\n"
           "    try {\n"
           "        bool ok = true;\n"
           "        using R = decltype(fn());\n"
           "        if constexpr (std::is_same_v<R, void>) {\n"
           "            fn();\n"
           "        } else if constexpr (std::is_same_v<R, bool>) {\n"
           "            ok = fn();\n"
           "        } else {\n"
           "            ok = fn() == 0;\n"
           "        }\n"
           "        capture.restore();\n"
           "        if (!ok) {\n"
           "            print_captured(capture.text());\n"
           "            std::cout << \"FAILED \" << name << \"\\n\";\n"
           "            return false;\n"
           "        }\n"
           "        std::cout << \"ok \" << name << \"\\n\";\n"
           "        return true;\n"
           "    } catch (const std::exception& error) {\n"
           "        capture.restore();\n"
           "        print_captured(capture.text());\n"
           "        std::cout << \"FAILED \" << name << \": \" << error.what() << \"\\n\";\n"
           "        return false;\n"
           "    }\n"
           "}\n"
           "template <typename F> bool run_should_panic(const char* name, F fn, "
           "std::string_view expected) {\n"
           "    Capture capture("
        << (capture_output ? "true" : "false")
        << ");\n"
           "    try {\n"
           "        fn();\n"
           "    } catch (const std::exception& error) {\n"
           "        capture.restore();\n"
           "        const std::string_view message = error.what();\n"
           "        if (!expected.empty() && message.find(expected) == std::string_view::npos) {\n"
           "            print_captured(capture.text());\n"
           "            std::cout << \"FAILED \" << name << \": expected panic containing \"\n"
           "                      << expected << \", got \" << message << \"\\n\";\n"
           "            return false;\n"
           "        }\n"
           "        std::cout << \"ok \" << name << \"\\n\";\n"
           "        return true;\n"
           "    }\n"
           "    capture.restore();\n"
           "    print_captured(capture.text());\n"
           "    std::cout << \"FAILED \" << name << \": expected panic\\n\";\n"
           "    return false;\n"
           "}\n"
           "} // namespace dudu_test\n\n"
           "int main() {\n"
           "    int total = 0;\n"
           "    int passed = 0;\n"
           "    int ignored = 0;\n";
    for (const FunctionDecl& fn : module.functions) {
        if (!is_test_function(fn)) {
            continue;
        }
        if (!filter.empty() && fn.name.find(filter) == std::string::npos) {
            continue;
        }
        if (has_decorator(fn, "test.ignore")) {
            out << "    ++ignored;\n"
                << "    std::cout << \"ignored " << fn.name << "\\n\";\n";
        } else if (has_decorator(fn, "test.should_panic") ||
                   !cpp_emit_function_decorator_arg(fn, "test.should_panic").empty()) {
            const std::string expected = cpp_emit_function_decorator_arg(fn, "test.should_panic");
            out << "    ++total;\n"
                << "    if (dudu_test::run_should_panic(" << cpp_emit_string_literal(fn.name)
                << ", " << emitted_name(fn, options) << ", " << cpp_emit_string_literal(expected)
                << ")) { ++passed; }\n";
        } else {
            out << "    ++total;\n"
                << "    if (dudu_test::run_one(" << cpp_emit_string_literal(fn.name) << ", "
                << emitted_name(fn, options) << ")) { ++passed; }\n";
        }
    }
    out << "    if (total == 0 && ignored == 0) {\n"
           "        std::cout << \"running 0 tests\\n\"\n"
           "                     \"test result: ok. 0 passed; 0 failed; 0 filtered out\\n\";\n"
           "        return 0;\n"
           "    }\n"
           "    std::cout << passed << \"/\" << total << \" tests passed\";\n"
           "    if (ignored > 0) { std::cout << \"; \" << ignored << \" ignored\"; }\n"
           "    std::cout << \"\\n\";\n"
           "    return passed == total ? 0 : 1;\n"
           "}\n";
}

} // namespace dudu
