#include "dudu/sema_methods.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/source.hpp"

#include <cctype>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

std::string unwrap_receiver_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
            type = trim(type.substr(1));
        }
        bool unwrapped = false;
        for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
            const std::string prefix = std::string(wrapper) + "[";
            if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
                type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
                unwrapped = true;
                break;
            }
        }
        if (!unwrapped) {
            return base_type(type);
        }
    }
}

std::string receiver_template_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
        type = trim(type.substr(1));
    }
    return trim(std::move(type));
}

std::string first_type_arg(const std::string& type) {
    const size_t open = type.find('[');
    if (open == std::string::npos || type.back() != ']') {
        return {};
    }
    int depth = 0;
    const size_t close = type.size() - 1;
    for (size_t i = open + 1; i < close; ++i) {
        if (type[i] == '[')
            ++depth;
        if (type[i] == ']')
            --depth;
        if (type[i] == ',' && depth == 0)
            return trim(type.substr(open + 1, i - open - 1));
    }
    return trim(type.substr(open + 1, close - open - 1));
}

bool builtin_cpp_method_signature(const Symbols& symbols, std::string receiver_type,
                                  const std::string& method_name, FunctionSignature& signature) {
    const std::string templated = receiver_template_type(symbols, std::move(receiver_type));
    if (starts_with(templated, "list[")) {
        const std::string item = first_type_arg(templated);
        if (method_name == "push_back" || method_name == "append") {
            signature.params = {item.empty() ? "auto" : item};
            signature.return_type = "void";
            return true;
        }
        if (method_name == "resize" || method_name == "reserve") {
            signature.params = {"auto"};
            signature.return_type = "void";
            return true;
        }
        if (method_name == "size" || method_name == "capacity") {
            signature.return_type = "usize";
            return true;
        }
        if (method_name == "empty") {
            signature.return_type = "bool";
            return true;
        }
    }
    return false;
}

bool is_indexed_local_segment(const std::string& text) {
    const size_t index = text.find('[');
    return index != std::string::npos && text.back() == ']' &&
           is_plain_identifier(trim(text.substr(0, index)));
}

bool method_is_static(const FunctionDecl& method) {
    return method.params.empty() || method.params.front().name != "self";
}

std::string template_method_name(const std::string& method_name) {
    const size_t open = method_name.find('[');
    return open == std::string::npos ? method_name : method_name.substr(0, open);
}

std::string template_method_arg(const std::string& method_name) {
    const size_t open = method_name.find('[');
    if (open == std::string::npos || method_name.back() != ']')
        return {};
    return trim(method_name.substr(open + 1, method_name.size() - open - 2));
}

std::string substitute_template_type(std::string type, const std::string& arg) {
    return !arg.empty() && trim(type) == "T" ? arg : type;
}

std::vector<std::string> template_args_from_type(const std::string& type) {
    const size_t open = type.find('[');
    if (open == std::string::npos || type.empty() || type.back() != ']') {
        return {};
    }
    return split_top_level_args(type.substr(open + 1, type.size() - open - 2));
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string replace_type_identifier(std::string type, const std::string& from,
                                    const std::string& to) {
    if (from.empty() || to.empty()) {
        return type;
    }
    size_t pos = type.find(from);
    while (pos != std::string::npos) {
        const bool left_ok = pos == 0 || !is_identifier_char(type[pos - 1]);
        const size_t end = pos + from.size();
        const bool right_ok = end == type.size() || !is_identifier_char(type[end]);
        if (left_ok && right_ok) {
            type.replace(pos, from.size(), to);
            pos = type.find(from, pos + to.size());
        } else {
            pos = type.find(from, end);
        }
    }
    return type;
}

std::string substitute_receiver_template_type(std::string type,
                                              const std::vector<std::string>& receiver_args) {
    if (receiver_args.empty()) {
        return type;
    }
    const std::string& first = receiver_args.front();
    for (const char* name :
         {"T", "_T", "_Tp", "_Tp1", "_Ty", "_Ty1", "value_type", "element_type"}) {
        type = replace_type_identifier(std::move(type), name, first);
    }
    if (receiver_args.size() >= 2) {
        type = replace_type_identifier(std::move(type), "_Key", receiver_args[0]);
        type = replace_type_identifier(std::move(type), "_Val", receiver_args[1]);
        type = replace_type_identifier(std::move(type), "_T1", receiver_args[0]);
        type = replace_type_identifier(std::move(type), "_T2", receiver_args[1]);
        type = replace_type_identifier(std::move(type), "_Tp1", receiver_args[0]);
        type = replace_type_identifier(std::move(type), "_Tp2", receiver_args[1]);
        type = replace_type_identifier(std::move(type), "_Ty1", receiver_args[0]);
        type = replace_type_identifier(std::move(type), "_Ty2", receiver_args[1]);
        type = replace_type_identifier(std::move(type), "mapped_type", receiver_args[1]);
        type = replace_type_identifier(std::move(type), "key_type", receiver_args[0]);
    }
    return type;
}

std::string substitute_class_template_type(std::string type,
                                           const std::vector<std::string>& generic_params,
                                           const std::vector<std::string>& receiver_args) {
    for (size_t i = 0; i < generic_params.size() && i < receiver_args.size(); ++i) {
        type = replace_type_identifier(std::move(type), generic_params[i], receiver_args[i]);
    }
    return type;
}

std::string first_path_type(const Symbols& symbols,
                            const std::map<std::string, std::string>& locals,
                            const SourceLocation* location, const std::string& first,
                            const std::string& unknown_local_prefix) {
    if (is_indexed_local_segment(first)) {
        const size_t index = first.find('[');
        const std::string name = trim(first.substr(0, index));
        const std::string index_expr = first.substr(index + 1, first.size() - index - 2);
        if (location == nullptr && !locals.contains(name)) {
            return {};
        }
        return indexed_value_type(
            symbols, locals, location == nullptr ? SourceLocation{} : *location, name, index_expr,
            unknown_local_prefix.empty() ? "indexed access to unknown local: "
                                         : unknown_local_prefix);
    }
    const auto local = locals.find(first);
    if (local == locals.end()) {
        if (location != nullptr && !unknown_local_prefix.empty()) {
            fail(*location, unknown_local_prefix + first);
        }
        return {};
    }
    return local->second;
}

} // namespace

std::string member_path_type(const Symbols& symbols,
                             const std::map<std::string, std::string>& locals,
                             const SourceLocation* location, const std::string& path,
                             std::string unknown_local_prefix) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        if (const auto local = locals.find(path); local != locals.end()) {
            return local->second;
        }
        return {};
    }
    std::string current = path.substr(0, dot);
    if (symbols.classes.contains(current)) {
        size_t start = dot + 1;
        const ClassDecl* klass = symbols.classes.at(current);
        while (start < path.size()) {
            const size_t next = path.find('.', start);
            const std::string member =
                path.substr(start, next == std::string::npos ? next : next - start);
            bool found = false;
            for (const ConstDecl& constant : klass->constants) {
                if (constant.name == member) {
                    if (next == std::string::npos) {
                        return constant.type;
                    }
                    const auto next_class = symbols.classes.find(base_type(constant.type));
                    if (next_class == symbols.classes.end()) {
                        return {};
                    }
                    klass = next_class->second;
                    found = true;
                    break;
                }
            }
            for (const ConstDecl& field : klass->static_fields) {
                if (field.name == member) {
                    if (next == std::string::npos) {
                        return field.type;
                    }
                    const auto next_class = symbols.classes.find(base_type(field.type));
                    if (next_class == symbols.classes.end()) {
                        return {};
                    }
                    klass = next_class->second;
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (location != nullptr) {
                    fail(*location, "unknown static member: " + path);
                }
                return {};
            }
            start = next + 1;
        }
        return {};
    }
    std::string type = first_path_type(symbols, locals, location, current, unknown_local_prefix);
    if (type.empty())
        return {};
    size_t start = dot + 1;
    while (start < path.size()) {
        const size_t next = path.find('.', start);
        const std::string field =
            path.substr(start, next == std::string::npos ? next : next - start);
        const auto klass = symbols.classes.find(unwrap_receiver_type(symbols, type));
        if (klass == symbols.classes.end()) {
            return {};
        }
        bool found = false;
        for (const FieldDecl& decl : klass->second->fields) {
            if (decl.name == field) {
                const std::vector<std::string> receiver_args = template_args_from_type(type);
                type = substitute_class_template_type(decl.type, klass->second->generic_params,
                                                      receiver_args);
                type = substitute_receiver_template_type(std::move(type), receiver_args);
                found = true;
                break;
            }
        }
        if (!found) {
            if (location != nullptr) {
                fail(*location, "unknown field: " + path);
            }
            return {};
        }
        if (next == std::string::npos) {
            return type;
        }
        start = next + 1;
    }
    return type;
}

bool is_member_path(const std::string& path) {
    if (path.find('.') == std::string::npos) {
        return false;
    }
    for (const std::string& part : split_top_level(path)) {
        if (part != path) {
            return false;
        }
    }
    size_t start = 0;
    while (start < path.size()) {
        const size_t dot = path.find('.', start);
        const std::string part = path.substr(start, dot == std::string::npos ? dot : dot - start);
        const std::string trimmed = trim(part);
        if (!is_plain_identifier(trimmed) && !is_indexed_local_segment(trimmed)) {
            return false;
        }
        if (dot == std::string::npos) {
            return true;
        }
        start = dot + 1;
    }
    return false;
}

bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, signature)) {
        return true;
    }
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, std::move(receiver_type));
    const std::string lookup_name = template_method_name(method_name);
    const std::string template_arg = template_method_arg(method_name);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            std::string param_type = substitute_template_type(method.params[i].type, template_arg);
            param_type = substitute_class_template_type(std::move(param_type),
                                                        klass->second->generic_params,
                                                        receiver_args);
            signature.params.push_back(
                substitute_receiver_template_type(std::move(param_type), receiver_args));
        }
        signature.return_type = method.return_type.empty()
                                    ? "void"
                                    : substitute_template_type(method.return_type, template_arg);
        signature.return_type = substitute_class_template_type(std::move(signature.return_type),
                                                               klass->second->generic_params,
                                                               receiver_args);
        signature.return_type =
            substitute_receiver_template_type(std::move(signature.return_type), receiver_args);
        return true;
    }
    for (const std::string& base : klass->second->base_classes) {
        if (method_signature_for_type(symbols, base, method_name, signature, nullptr)) {
            return true;
        }
    }
    if (location != nullptr) {
        fail(*location, "unknown method: " + type + "." + method_name);
    }
    return false;
}

std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          std::string receiver_type,
                                                          const std::string& method_name) {
    FunctionSignature builtin;
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, builtin)) {
        return {builtin};
    }
    std::vector<FunctionSignature> out;
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, std::move(receiver_type));
    const std::string lookup_name = template_method_name(method_name);
    const std::string template_arg = template_method_arg(method_name);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return out;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        FunctionSignature signature;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            std::string param_type = substitute_template_type(method.params[i].type, template_arg);
            param_type = substitute_class_template_type(std::move(param_type),
                                                        klass->second->generic_params,
                                                        receiver_args);
            signature.params.push_back(
                substitute_receiver_template_type(std::move(param_type), receiver_args));
        }
        signature.return_type = method.return_type.empty()
                                    ? "void"
                                    : substitute_template_type(method.return_type, template_arg);
        signature.return_type = substitute_class_template_type(std::move(signature.return_type),
                                                               klass->second->generic_params,
                                                               receiver_args);
        signature.return_type =
            substitute_receiver_template_type(std::move(signature.return_type), receiver_args);
        out.push_back(std::move(signature));
    }
    for (const std::string& base : klass->second->base_classes) {
        std::vector<FunctionSignature> base_signatures =
            method_signatures_for_type(symbols, base, method_name);
        out.insert(out.end(), base_signatures.begin(), base_signatures.end());
    }
    return out;
}

bool static_method_signature_for_type(const Symbols& symbols, const std::string& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location) {
    const auto klass = symbols.classes.find(type_name);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || !method_is_static(method)) {
            continue;
        }
        for (const ParamDecl& param : method.params) {
            signature.params.push_back(param.type);
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        return true;
    }
    if (location != nullptr) {
        fail(*location, "unknown static method: " + type_name + "." + method_name);
    }
    return false;
}

} // namespace dudu
