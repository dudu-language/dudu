#include "dudu/cpp_emit.hpp"

#include "dudu/cpp_emit_classes.hpp"
#include "dudu/cpp_emit_prelude.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_emit.hpp"

#include <map>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {
void emit_aliases(std::ostringstream& out, const ModuleAst& module) {
    for (const TypeAliasDecl& alias : module.aliases) {
        out << "using " << alias.name << " = " << lower_cpp_type(alias.type) << ";\n";
    }
    if (!module.aliases.empty()) {
        out << '\n';
    }
}
void emit_enums(std::ostringstream& out, const ModuleAst& module) {
    for (const EnumDecl& en : module.enums) {
        out << "enum class " << en.name;
        if (!en.underlying_type.empty()) {
            out << " : " << lower_cpp_type(en.underlying_type);
        }
        out << " {\n";
        for (const EnumValueDecl& value : en.values) {
            out << "    " << value.name;
            if (!value.value.empty()) {
                out << " = " << value.value;
            }
            out << ",\n";
        }
        out << "};\n\n";
    }
}

bool function_has_decorator(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (trim_copy(decorator.text) == name) {
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
    const std::string prefix = std::string(name) + "(";
    for (const Decorator& decorator : fn.decorators) {
        const std::string text = trim_copy(decorator.text);
        if (starts_with(text, prefix) && ends_with(text, ")")) {
            return trim_copy(text.substr(prefix.size(), text.size() - prefix.size() - 1));
        }
    }
    return {};
}

bool visible_in_header(Visibility visibility) {
    return visibility != Visibility::Private;
}

bool visible_function_in_header(const FunctionDecl& fn) {
    return visible_in_header(fn.visibility) && !function_has_decorator(fn, "test");
}

bool emit_before_constants(const FunctionDecl& fn) {
    return function_has_decorator(fn, "constexpr");
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
        const std::string lowered_type = lower_cpp_type(constant.type);
        const bool runtime_address = constant.type.find('*') != std::string::npos ||
                                     constant.type.find("volatile") != std::string::npos;
        out << "inline ";
        if (runtime_address && constant.type.find('*') != std::string::npos) {
            out << lowered_type << " const " << constant.name;
        } else {
            out << (runtime_address ? "const " : "constexpr ") << lowered_type << ' '
                << constant.name;
        }
        out << " = " << lower_cpp_expr(constant.value, aliases) << ";\n";
    }
    if (!module.constants.empty()) {
        out << '\n';
    }
}

void emit_static_asserts(std::ostringstream& out, const ModuleAst& module,
                         const std::vector<std::string>& aliases) {
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        out << "static_assert" << lower_cpp_expr(assertion.expression, aliases) << ";\n";
    }
    if (!module.static_asserts.empty()) {
        out << '\n';
    }
}

void emit_function_signature(std::ostringstream& out, const FunctionDecl& fn) {
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
    const std::string workgroup = function_decorator_arg(fn, "workgroup_size");
    if (!workgroup.empty()) {
        out << "DUDU_WORKGROUP_SIZE(" << workgroup << ") ";
    }
    if (function_has_decorator(fn, "inline")) {
        out << "inline ";
    }
    if (function_has_decorator(fn, "constexpr")) {
        out << "constexpr ";
    }
    out << lower_cpp_type(fn.return_type) << ' ' << fn.name << '(';
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(fn.params[i].type) << ' ' << fn.params[i].name;
    }
    out << ')';
}

void emit_function_body(std::ostringstream& out, const FunctionDecl& fn,
                        const std::vector<std::string>& aliases,
                        const std::map<std::string, std::string>& function_returns) {
    emit_function_signature(out, fn);
    out << " {\n";
    std::map<std::string, std::string> locals;
    for (const ParamDecl& param : fn.params) {
        locals[param.name] = param.type;
    }
    emit_raw_block(out, fn.body, 1, aliases, locals, fn.return_type, function_returns);
    out << "}\n\n";
}

bool should_emit_function(const FunctionDecl& fn, bool test_source) {
    return !test_source || fn.name != "main";
}

void emit_early_functions(std::ostringstream& out, const ModuleAst& module,
                          const std::vector<std::string>& aliases,
                          const std::map<std::string, std::string>& function_returns,
                          bool header_only, bool test_source = false) {
    for (const FunctionDecl& fn : module.functions) {
        if (!emit_before_constants(fn) || !should_emit_function(fn, test_source)) {
            continue;
        }
        if (header_only && !visible_function_in_header(fn)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns);
    }
}

void emit_test_harness(std::ostringstream& out, const ModuleAst& module,
                       const std::string& filter) {
    out << "namespace dudu_test {\n"
           "template <typename F> bool run_one(const char* name, F fn) {\n"
           "    try {\n"
           "        using R = decltype(fn());\n"
           "        if constexpr (std::is_same_v<R, void>) {\n"
           "            fn();\n"
           "        } else if constexpr (std::is_same_v<R, bool>) {\n"
           "            if (!fn()) { std::cout << \"FAILED \" << name << \"\\n\"; return false; }\n"
           "        } else {\n"
           "            if (fn() != 0) { std::cout << \"FAILED \" << name << \"\\n\"; return false; }\n"
           "        }\n"
           "        std::cout << \"ok \" << name << \"\\n\";\n"
           "        return true;\n"
           "    } catch (const std::exception& error) {\n"
           "        std::cout << \"FAILED \" << name << \": \" << error.what() << \"\\n\";\n"
           "        return false;\n"
           "    }\n"
           "}\n"
           "} // namespace dudu_test\n\n"
           "int main() {\n"
           "    int total = 0;\n"
           "    int passed = 0;\n";
    for (const FunctionDecl& fn : module.functions) {
        if (!function_has_decorator(fn, "test")) {
            continue;
        }
        if (!filter.empty() && fn.name.find(filter) == std::string::npos) {
            continue;
        }
        out << "    ++total;\n"
            << "    if (dudu_test::run_one(" << cpp_string_literal(fn.name) << ", " << fn.name
            << ")) { ++passed; }\n";
    }
    out << "    std::cout << passed << \"/\" << total << \" tests passed\\n\";\n"
           "    return passed == total ? 0 : 1;\n"
           "}\n";
}

} // namespace

std::string emit_cpp_header(const ModuleAst& module) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, std::string> function_returns = function_return_types(module);
    out << "#pragma once\n\n";
    emit_includes(out, module);
    emit_result_prelude(out, module);

    emit_aliases(out, module);
    emit_enums(out, module);
    emit_classes(out, module, aliases, function_returns, true);
    emit_early_functions(out, module, aliases, function_returns, true);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn)) {
            continue;
        }
        if (!visible_function_in_header(fn)) {
            continue;
        }
        emit_function_signature(out, fn);
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
        out << lower_cpp_type(fn.return_type) << ' ' << fn.name << '(';
        if (fn.params.empty()) {
            out << "void";
        }
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_cpp_type(fn.params[i].type) << ' ' << fn.params[i].name;
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
    emit_includes(out, module);
    emit_result_prelude(out, module);

    emit_aliases(out, module);
    emit_enums(out, module);
    emit_classes(out, module, aliases, function_returns);
    emit_early_functions(out, module, aliases, function_returns, false);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns);
    }
    emit_static_asserts(out, module, aliases);
    return out.str();
}

std::string emit_cpp_test_source(const ModuleAst& module, const std::string& filter) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, std::string> function_returns = function_return_types(module);
    emit_includes(out, module);
    emit_result_prelude(out, module);

    emit_aliases(out, module);
    emit_enums(out, module);
    emit_classes(out, module, aliases, function_returns);
    emit_early_functions(out, module, aliases, function_returns, false, true);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn) || !should_emit_function(fn, true)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns);
    }
    emit_static_asserts(out, module, aliases);
    emit_test_harness(out, module, filter);
    return out.str();
}

} // namespace dudu
