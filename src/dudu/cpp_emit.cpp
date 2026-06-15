#include "dudu/cpp_emit.hpp"

#include "dudu/cpp_emit_classes.hpp"
#include "dudu/cpp_emit_enums.hpp"
#include "dudu/cpp_emit_prelude.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_emit.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_context.hpp"

#include <map>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {
void emit_aliases(std::ostringstream& out, const ModuleAst& module) {
    for (const TypeAliasDecl& alias : module.aliases) {
        out << "using " << alias.name << " = " << lower_cpp_type(alias.type_ref) << ";\n";
    }
    if (!module.aliases.empty()) {
        out << '\n';
    }
}

bool function_has_decorator(const FunctionDecl& fn, std::string_view name) {
    return has_decorator(fn.decorators, name);
}

bool function_is_test(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (decorator_matches(decorator, "test") || decorator_matches(decorator, "test.ignore") ||
            decorator_matches(decorator, "test.should_panic") ||
            decorator_call_matches(decorator, "test.should_panic")) {
            return true;
        }
    }
    return false;
}

std::string cpp_string_literal(std::string text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string function_decorator_arg(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const std::optional<std::string> arg = decorator_first_arg_text(decorator, name)) {
            return *arg;
        }
    }
    return {};
}

std::string function_decorator_args(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const std::optional<std::string> args = decorator_arg_list_text(decorator, name)) {
            return *args;
        }
    }
    return {};
}

bool visible_in_header(Visibility visibility) {
    return visibility != Visibility::Private;
}

bool visible_function_in_header(const FunctionDecl& fn) {
    return visible_in_header(fn.visibility) && !function_is_test(fn);
}

bool emit_before_constants(const FunctionDecl& fn) {
    return function_has_decorator(fn, "constexpr");
}

void emit_template_params(std::ostringstream& out, const std::vector<std::string>& params) {
    if (params.empty()) {
        return;
    }
    out << "template <";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "typename " << params[i];
    }
    out << ">\n";
}

std::map<std::string, std::string> function_return_types(const ModuleAst& module) {
    std::map<std::string, std::string> out;
    for (const FunctionDecl& fn : module.functions) {
        out[fn.name] = fn.return_type.empty() ? "void" : fn.return_type;
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            out[klass.name + "." + method.name] =
                method.return_type.empty() ? "void" : method.return_type;
        }
    }
    return out;
}

void emit_constants(std::ostringstream& out, const ModuleAst& module,
                    const std::vector<std::string>& aliases) {
    for (const ConstDecl& constant : module.constants) {
        const std::string lowered_type = lower_cpp_type(constant.type_ref, aliases);
        const bool runtime_address = constant.type.find('*') != std::string::npos ||
                                     constant.type.find("volatile") != std::string::npos;
        out << "inline ";
        if (runtime_address && constant.type.find('*') != std::string::npos) {
            out << lowered_type << " const " << constant.name;
        } else {
            out << (runtime_address ? "const " : "constexpr ") << lowered_type << ' '
                << constant.name;
        }
        out << " = " << lower_cpp_expr_ast(constant.value_expr, aliases) << ";\n";
    }
    if (!module.constants.empty()) {
        out << '\n';
    }
}

void emit_static_asserts(std::ostringstream& out, const ModuleAst& module,
                         const std::vector<std::string>& aliases) {
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        out << "static_assert" << lower_cpp_expr_ast(assertion.expression_expr, aliases) << ";\n";
    }
    if (!module.static_asserts.empty()) {
        out << '\n';
    }
}

void emit_function_signature(std::ostringstream& out, const FunctionDecl& fn,
                             const std::vector<std::string>& aliases) {
    if (function_has_decorator(fn, "extern_c")) {
        out << "extern \"C\" ";
    }
    if (function_has_decorator(fn, "cuda.global")) {
        out << "DUDU_CUDA_GLOBAL ";
    }
    if (function_has_decorator(fn, "cuda.device")) {
        out << "DUDU_CUDA_DEVICE ";
    }
    if (function_has_decorator(fn, "cuda.host")) {
        out << "DUDU_CUDA_HOST ";
    }
    if (function_has_decorator(fn, "shader.compute")) {
        out << "DUDU_SHADER_COMPUTE ";
    }
    const std::string section = function_decorator_arg(fn, "section");
    if (!section.empty()) {
        out << "__attribute__((section(" << section << "))) ";
    }
    const std::string workgroup = function_decorator_args(fn, "workgroup_size");
    if (!workgroup.empty()) {
        out << "DUDU_WORKGROUP_SIZE(" << workgroup << ") ";
    }
    if (function_has_decorator(fn, "inline")) {
        out << "inline ";
    }
    if (function_has_decorator(fn, "constexpr")) {
        out << "constexpr ";
    }
    out << lower_cpp_type(fn.return_type_ref, aliases) << ' ' << fn.name << '(';
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(fn.params[i].type_ref, aliases) << ' ' << fn.params[i].name;
    }
    out << ')';
}

void emit_function_body(std::ostringstream& out, const FunctionDecl& fn,
                        const std::vector<std::string>& aliases,
                        const std::map<std::string, std::string>& function_returns,
                        const Symbols& symbols) {
    emit_template_params(out, fn.generic_params);
    emit_function_signature(out, fn, aliases);
    out << " {\n";
    std::map<std::string, std::string> locals;
    for (const ParamDecl& param : fn.params) {
        locals[param.name] = param.type;
    }
    emit_block(out, fn.statements, 1, aliases, locals, fn.return_type, function_returns, &symbols);
    out << "}\n\n";
}

bool should_emit_function(const FunctionDecl& fn, bool test_source) {
    return !test_source || fn.name != "main";
}

void emit_early_functions(std::ostringstream& out, const ModuleAst& module,
                          const std::vector<std::string>& aliases,
                          const std::map<std::string, std::string>& function_returns,
                          const Symbols& symbols, bool header_only, bool test_source = false) {
    for (const FunctionDecl& fn : module.functions) {
        if (!emit_before_constants(fn) || !should_emit_function(fn, test_source)) {
            continue;
        }
        if (header_only && !visible_function_in_header(fn)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns, symbols);
    }
}

void emit_test_harness(std::ostringstream& out, const ModuleAst& module, const std::string& filter,
                       bool capture_output) {
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
        if (!function_is_test(fn)) {
            continue;
        }
        if (!filter.empty() && fn.name.find(filter) == std::string::npos) {
            continue;
        }
        if (function_has_decorator(fn, "test.ignore")) {
            out << "    ++ignored;\n"
                << "    std::cout << \"ignored " << fn.name << "\\n\";\n";
        } else if (function_has_decorator(fn, "test.should_panic") ||
                   !function_decorator_arg(fn, "test.should_panic").empty()) {
            const std::string expected = function_decorator_arg(fn, "test.should_panic");
            out << "    ++total;\n"
                << "    if (dudu_test::run_should_panic(" << cpp_string_literal(fn.name) << ", "
                << fn.name << ", " << (expected.empty() ? "\"\"" : expected)
                << ")) { ++passed; }\n";
        } else {
            out << "    ++total;\n"
                << "    if (dudu_test::run_one(" << cpp_string_literal(fn.name) << ", " << fn.name
                << ")) { ++passed; }\n";
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

} // namespace

std::string emit_cpp_header(const ModuleAst& module) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, std::string> function_returns = function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    out << "#pragma once\n\n";
    emit_includes(out, module);
    emit_result_prelude(out, module);

    emit_aliases(out, module);
    emit_enum_forward_declarations(out, module);
    emit_classes(out, module, aliases, function_returns, symbols, true);
    emit_enums(out, module, aliases);
    emit_early_functions(out, module, aliases, function_returns, symbols, true);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn)) {
            continue;
        }
        if (!visible_function_in_header(fn)) {
            continue;
        }
        emit_template_params(out, fn.generic_params);
        emit_function_signature(out, fn, aliases);
        out << ";\n";
    }
    emit_static_asserts(out, module, aliases);
    return out.str();
}

std::string emit_c_header(const ModuleAst& module) {
    std::ostringstream out;
    out << "#pragma once\n\n"
        << "#include <stdbool.h>\n"
        << "#include <stddef.h>\n"
        << "#include <stdint.h>\n\n"
        << "#ifdef __cplusplus\n"
        << "extern \"C\" {\n"
        << "#endif\n\n";
    for (const FunctionDecl& fn : module.functions) {
        if (!function_has_decorator(fn, "extern_c") || !visible_function_in_header(fn)) {
            continue;
        }
        out << lower_cpp_type(fn.return_type_ref) << ' ' << fn.name << '(';
        if (fn.params.empty()) {
            out << "void";
        }
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_cpp_type(fn.params[i].type_ref) << ' ' << fn.params[i].name;
        }
        out << ");\n";
    }
    out << "\n#ifdef __cplusplus\n"
        << "}\n"
        << "#endif\n";
    return out.str();
}

std::string emit_cpp_source(const ModuleAst& module) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, std::string> function_returns = function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    emit_includes(out, module);
    emit_result_prelude(out, module);

    emit_aliases(out, module);
    emit_enum_forward_declarations(out, module);
    emit_classes(out, module, aliases, function_returns, symbols);
    emit_enums(out, module, aliases);
    emit_early_functions(out, module, aliases, function_returns, symbols, false);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns, symbols);
    }
    emit_static_asserts(out, module, aliases);
    return out.str();
}

std::string emit_cpp_test_source(const ModuleAst& module, const std::string& filter,
                                 bool capture_output) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, std::string> function_returns = function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    emit_includes(out, module);
    emit_result_prelude(out, module);

    emit_aliases(out, module);
    emit_enum_forward_declarations(out, module);
    emit_classes(out, module, aliases, function_returns, symbols);
    emit_enums(out, module, aliases);
    emit_early_functions(out, module, aliases, function_returns, symbols, false, true);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn) || !should_emit_function(fn, true)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns, symbols);
    }
    emit_static_asserts(out, module, aliases);
    emit_test_harness(out, module, filter, capture_output);
    return out.str();
}

} // namespace dudu
