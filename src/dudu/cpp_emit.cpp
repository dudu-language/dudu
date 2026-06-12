#include "dudu/cpp_emit.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_emit.hpp"

#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {
std::vector<std::string> namespace_aliases(const ModuleAst& module) {
    std::vector<std::string> aliases;
    for (const ImportDecl& import : module.imports) {
        if ((import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp) &&
            !import.alias.empty()) {
            aliases.push_back(import.alias);
        }
    }
    return aliases;
}

std::string include_path(const ImportDecl& import) {
    if (import.module_path.size() >= 2 && import.module_path.front() == '"' &&
        import.module_path.back() == '"') {
        return import.module_path;
    }
    return '"' + import.module_path + '"';
}
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
void emit_includes(std::ostringstream& out, const ModuleAst& module) {
    out << "#include <array>\n"
           "#include <atomic>\n"
           "#include <cstddef>\n"
           "#include <cstdint>\n"
           "#include <cstdlib>\n"
           "#include <functional>\n"
           "#include <optional>\n"
           "#include <string>\n"
           "#include <tuple>\n"
           "#include <unordered_map>\n"
           "#include <unordered_set>\n"
           "#include <utility>\n"
           "#include <variant>\n"
           "#include <vector>\n";

    for (const ImportDecl& import : module.imports) {
        if (import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp) {
            out << "#include " << include_path(import) << '\n';
        }
    }
    out << '\n';
}

void emit_result_prelude(std::ostringstream& out) {
    out << "namespace dudu {\n"
           "template <typename T> struct OkValue { T value; };\n"
           "template <typename E> struct ErrValue { E err; };\n"
           "template <typename T> OkValue<T> Ok(T value) { return {std::move(value)}; }\n"
           "template <typename E> ErrValue<E> Err(E err) { return {std::move(err)}; }\n"
           "template <typename T, typename E> struct Result {\n"
           "    bool ok{};\n"
           "    T value{};\n"
           "    E err{};\n"
           "    Result() = default;\n"
           "    Result(OkValue<T> ok_value) : ok(true), value(std::move(ok_value.value)), err{} "
           "{}\n"
           "    Result(ErrValue<E> err_value) : ok(false), value{}, err(std::move(err_value.err)) "
           "{}\n"
           "};\n"
           "} // namespace dudu\n\n";
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

bool visible_in_header(Visibility visibility) {
    return visibility != Visibility::Private;
}

void emit_classes(std::ostringstream& out, const ModuleAst& module, bool header_only = false) {
    for (const size_t index : class_emit_order(module.classes)) {
        const ClassDecl& klass = module.classes[index];
        if (header_only && !visible_in_header(klass.visibility)) {
            continue;
        }
        out << class_opening(klass) << " {\n";
        for (const FieldDecl& field : klass.fields) {
            out << "    " << lower_cpp_type(field.type) << ' ' << field.name << "{};\n";
        }
        out << "};\n\n";
    }
}

void emit_constants(std::ostringstream& out, const ModuleAst& module,
                    const std::vector<std::string>& aliases) {
    for (const ConstDecl& constant : module.constants) {
        out << "inline constexpr " << lower_cpp_type(constant.type) << ' ' << constant.name << " = "
            << lower_cpp_expr(constant.value, aliases) << ";\n";
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

} // namespace

std::string emit_cpp_header(const ModuleAst& module) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    out << "#pragma once\n\n";
    emit_includes(out, module);
    emit_result_prelude(out);

    emit_aliases(out, module);
    emit_enums(out, module);
    emit_classes(out, module, true);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
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
    emit_includes(out, module);
    emit_result_prelude(out);

    emit_aliases(out, module);
    emit_enums(out, module);
    emit_classes(out, module);
    emit_constants(out, module, aliases);

    for (const FunctionDecl& fn : module.functions) {
        emit_function_signature(out, fn);
        out << " {\n";
        emit_raw_block(out, fn.body, 1, aliases);
        out << "}\n\n";
    }
    emit_static_asserts(out, module, aliases);
    return out.str();
}

} // namespace dudu
