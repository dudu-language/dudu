#include "dudu/cpp_emit_classes.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_emit.hpp"

#include <cctype>
#include <set>
#include <sstream>
#include <string_view>

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
    if (emitted.contains(index) || visiting.contains(index)) {
        return;
    }
    visiting.insert(index);

    for (const FieldDecl& field : classes[index].fields) {
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

bool class_has_decorator(const ClassDecl& klass, std::string_view name) {
    for (const Decorator& decorator : klass.decorators) {
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

std::string unquoted(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

bool is_constructor_method(const FunctionDecl& method) {
    return method.name == "init";
}

bool is_destructor_method(const FunctionDecl& method) {
    return method.name == "drop";
}

std::string operator_name(const FunctionDecl& method) {
    const std::string op = unquoted(function_decorator_arg(method, "operator"));
    if (!op.empty()) {
        return op == "bool" ? "operator bool" : "operator" + op;
    }
    return method.name;
}

std::string class_opening(const ClassDecl& klass) {
    const bool packed = class_has_decorator(klass, "packed");
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

bool visible_in_header(Visibility visibility) {
    return visibility != Visibility::Private;
}

std::string_view class_section_for_method(Visibility visibility) {
    return visibility == Visibility::Private ? "private" : "public";
}

void emit_method(std::ostringstream& out, const std::string& class_name, const FunctionDecl& method,
                 const std::vector<std::string>& aliases,
                 const std::map<std::string, std::string>& function_returns) {
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    if (is_constructor_method(method)) {
        out << "    " << class_name << '(';
    } else if (is_destructor_method(method)) {
        out << "    ~" << class_name << '(';
    } else {
        out << "    ";
        if (first_param == 0) {
            out << "static ";
        }
        const std::string lowered_name = operator_name(method);
        if (lowered_name == "operator bool") {
            out << "explicit " << lowered_name << '(';
        } else {
            out << lower_cpp_type(method.return_type_ref, aliases) << ' ' << lowered_name << '(';
        }
    }
    for (size_t i = first_param; i < method.params.size(); ++i) {
        if (i > first_param) {
            out << ", ";
        }
        out << lower_cpp_type(method.params[i].type_ref, aliases) << ' ' << method.params[i].name;
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
    emit_block(out, method.statements, 2, aliases, locals, method.return_type, function_returns);
    out << "    }\n";
}

void emit_class_constant_decl(std::ostringstream& out, const ConstDecl& constant,
                              const std::vector<std::string>& aliases) {
    const std::string lowered_type = lower_cpp_type(constant.type_ref, aliases);
    const bool pointer = constant.type.find('*') != std::string::npos;
    out << "    static ";
    out << (pointer ? lowered_type + " const " : "const " + lowered_type + " ");
    out << constant.name << ";\n";
}

void emit_class_constant_definition(std::ostringstream& out, const std::string& class_name,
                                    const ConstDecl& constant,
                                    const std::vector<std::string>& aliases) {
    const std::string lowered_type = lower_cpp_type(constant.type_ref, aliases);
    const bool runtime_address = constant.type.find('*') != std::string::npos ||
                                 constant.type.find("volatile") != std::string::npos;
    out << "inline ";
    if (runtime_address && constant.type.find('*') != std::string::npos) {
        out << lowered_type << " const " << class_name << "::" << constant.name;
    } else {
        out << (runtime_address ? "const " : "constexpr ") << lowered_type << ' ' << class_name
            << "::" << constant.name;
    }
    out << " = " << lower_cpp_expr(constant.value, aliases) << ";\n";
}

} // namespace

void emit_classes(std::ostringstream& out, const ModuleAst& module,
                  const std::vector<std::string>& aliases,
                  const std::map<std::string, std::string>& function_returns, bool header_only) {
    for (const size_t index : class_emit_order(module.classes)) {
        const ClassDecl& klass = module.classes[index];
        if (header_only && !visible_in_header(klass.visibility)) {
            continue;
        }
        out << class_opening(klass) << " {\n";
        for (const FieldDecl& field : klass.fields) {
            out << "    " << lower_cpp_type(field.type_ref, aliases) << ' ' << field.name;
            if (field.value.empty()) {
                out << "{}";
            } else {
                out << " = " << lower_cpp_expr(field.value, aliases);
            }
            out << ";\n";
        }
        for (const ConstDecl& field : klass.static_fields) {
            out << "    inline static " << lower_cpp_type(field.type_ref, aliases) << ' '
                << field.name << " = " << lower_cpp_expr(field.value, aliases) << ";\n";
        }
        for (const ConstDecl& constant : klass.constants) {
            emit_class_constant_decl(out, constant, aliases);
        }
        std::string_view current_section = "public";
        for (const FunctionDecl& method : klass.methods) {
            const std::string_view method_section = class_section_for_method(method.visibility);
            if (method_section != current_section) {
                out << method_section << ":\n";
                current_section = method_section;
            }
            emit_method(out, klass.name, method, aliases, function_returns);
        }
        out << "};\n\n";
        for (const ConstDecl& constant : klass.constants) {
            emit_class_constant_definition(out, klass.name, constant, aliases);
        }
        if (!klass.constants.empty()) {
            out << '\n';
        }
    }
}

} // namespace dudu
