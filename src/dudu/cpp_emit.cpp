#include "dudu/cpp_emit.hpp"

#include "dudu/cpp_emit_prelude.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_emit.hpp"

#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {
bool contains_type_name(const std::string& type, const std::string& name) {
    size_t pos = type.find(name);
    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(type[pos - 1])) == 0 && type[pos - 1] != '_');
        const size_t end = pos + name.size();
        const bool right_ok =
            end == type.size() ||
            (std::isalnum(static_cast<unsigned char>(type[end])) == 0 && type[end] != '_');
        if (left_ok && right_ok) {
            return true;
        }
        pos = type.find(name, pos + 1);
    }
    return false;
}
void visit_class(const std::vector<ClassDecl>& classes, size_t index, std::set<size_t>& visiting,
                 std::set<size_t>& emitted, std::vector<size_t>& order) {
    if (emitted.contains(index)) {
        return;
    }
    if (visiting.contains(index)) {
        return;
    }
    visiting.insert(index);

    const ClassDecl& klass = classes[index];
    for (const FieldDecl& field : klass.fields) {
        for (size_t dep = 0; dep < classes.size(); ++dep) {
            if (dep != index && contains_type_name(field.type, classes[dep].name)) {
                visit_class(classes, dep, visiting, emitted, order);
            }
        }
    }

    visiting.erase(index);
    emitted.insert(index);
    order.push_back(index);
}
std::vector<size_t> class_emit_order(const std::vector<ClassDecl>& classes) {
    std::set<size_t> visiting;
    std::set<size_t> emitted;
    std::vector<size_t> order;
    for (size_t i = 0; i < classes.size(); ++i) {
        visit_class(classes, i, visiting, emitted, order);
    }
    return order;
}
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

std::string decorator_arg(const ClassDecl& klass, std::string_view name) {
    const std::string prefix = std::string(name) + "(";
    for (const Decorator& decorator : klass.decorators) {
        const std::string text = trim_copy(decorator.text);
        if (starts_with(text, prefix) && ends_with(text, ")")) {
            return trim_copy(text.substr(prefix.size(), text.size() - prefix.size() - 1));
        }
    }
    return {};
}

bool has_decorator(const ClassDecl& klass, std::string_view name) {
    for (const Decorator& decorator : klass.decorators) {
        if (trim_copy(decorator.text) == name) {
            return true;
        }
    }
    return false;
}

std::string class_opening(const ClassDecl& klass) {
    const bool packed = has_decorator(klass, "packed");
    const std::string alignment = decorator_arg(klass, "align");
    if (packed && !alignment.empty()) {
        return "struct __attribute__((packed, aligned(" + alignment + "))) " + klass.name;
    }
    if (packed) {
        return "struct __attribute__((packed)) " + klass.name;
    }
    if (!alignment.empty()) {
        return "struct alignas(" + alignment + ") " + klass.name;
    }
    return "struct " + klass.name;
}

bool function_has_decorator(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (trim_copy(decorator.text) == name) {
            return true;
        }
    }
    return false;
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

bool emit_before_constants(const FunctionDecl& fn) {
    return function_has_decorator(fn, "constexpr");
}

std::map<std::string, std::string> function_return_types(const ModuleAst& module) {
    std::map<std::string, std::string> out;
    for (const FunctionDecl& fn : module.functions) {
        out[fn.name] = fn.return_type.empty() ? "void" : fn.return_type;
    }
    return out;
}

void emit_method(std::ostringstream& out, const FunctionDecl& method,
                 const std::vector<std::string>& aliases,
                 const std::map<std::string, std::string>& function_returns) {
    out << "    " << lower_cpp_type(method.return_type) << ' ' << method.name << '(';
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    for (size_t i = first_param; i < method.params.size(); ++i) {
        if (i > first_param) {
            out << ", ";
        }
        out << lower_cpp_type(method.params[i].type) << ' ' << method.params[i].name;
    }
    out << ") {\n";
    std::map<std::string, std::string> locals;
    if (first_param == 1) {
        out << "        auto& self = *this;\n";
        locals[method.params.front().name] = method.params.front().type;
    }
    for (size_t i = first_param; i < method.params.size(); ++i) {
        locals[method.params[i].name] = method.params[i].type;
    }
    emit_raw_block(out, method.body, 2, aliases, locals, method.return_type, function_returns);
    out << "    }\n";
}

void emit_classes(std::ostringstream& out, const ModuleAst& module,
                  const std::vector<std::string>& aliases,
                  const std::map<std::string, std::string>& function_returns,
                  bool header_only = false) {
    for (const size_t index : class_emit_order(module.classes)) {
        const ClassDecl& klass = module.classes[index];
        if (header_only && !visible_in_header(klass.visibility)) {
            continue;
        }
        out << class_opening(klass) << " {\n";
        for (const FieldDecl& field : klass.fields) {
            out << "    " << lower_cpp_type(field.type) << ' ' << field.name << "{};\n";
        }
        for (const FunctionDecl& method : klass.methods) {
            emit_method(out, method, aliases, function_returns);
        }
        out << "};\n\n";
    }
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

void emit_early_functions(std::ostringstream& out, const ModuleAst& module,
                          const std::vector<std::string>& aliases,
                          const std::map<std::string, std::string>& function_returns,
                          bool header_only) {
    for (const FunctionDecl& fn : module.functions) {
        if (!emit_before_constants(fn)) {
            continue;
        }
        if (header_only && !visible_in_header(fn.visibility)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns);
    }
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
        if (!visible_in_header(fn.visibility)) {
            continue;
        }
        emit_function_signature(out, fn);
        out << ";\n";
    }
    emit_static_asserts(out, module, aliases);
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

} // namespace dudu
