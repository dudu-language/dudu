#include "dudu/cpp_emit.hpp"

#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

std::string replace_dots(std::string text) {
    size_t pos = 0;
    while ((pos = text.find('.', pos)) != std::string::npos) {
        text.replace(pos, 1, "::");
        pos += 2;
    }
    return text;
}

std::string trim_copy(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string lower_type(const std::string& type);
std::vector<std::string> split_top_level_args(const std::string& args);

std::string lower_template_type(std::string_view name, const std::string& args) {
    if (name == "list") {
        return "std::vector<" + lower_type(args) + ">";
    }
    if (name == "dict") {
        return "std::unordered_map<" + replace_dots(args) + ">";
    }
    if (name == "set") {
        return "std::unordered_set<" + lower_type(args) + ">";
    }
    if (name == "Option") {
        return "std::optional<" + lower_type(args) + ">";
    }
    if (name == "Result") {
        return "dudu::Result<" + replace_dots(args) + ">";
    }
    if (name == "tuple") {
        return "dudu::Tuple<" + replace_dots(args) + ">";
    }
    if (name == "const") {
        return "const " + lower_type(args);
    }
    if (name == "atomic") {
        return "std::atomic<" + lower_type(args) + ">";
    }
    if (name == "volatile") {
        return "volatile " + lower_type(args);
    }
    return replace_dots(std::string(name)) + "<" + replace_dots(args) + ">";
}

std::string lower_function_type(const std::string& type) {
    const size_t open = type.find('(');
    const size_t close = type.find(')', open);
    if (open == std::string::npos || close == std::string::npos) {
        return replace_dots(type);
    }
    const std::string args = type.substr(open + 1, close - open - 1);
    std::string result = "void";
    const size_t arrow = type.find("->", close);
    if (arrow != std::string::npos) {
        result = lower_type(type.substr(arrow + 2));
    }

    std::vector<std::string> lowered_args;
    for (const std::string& arg : split_top_level_args(args)) {
        lowered_args.push_back(lower_type(arg));
    }
    std::ostringstream out;
    out << "std::function<" << result << '(';
    for (size_t i = 0; i < lowered_args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lowered_args[i];
    }
    out << ")>";
    return out.str();
}

std::vector<std::string> split_top_level_args(const std::string& args) {
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        const char c = args[i];
        if (c == '[' || c == '(') {
            ++depth;
        } else if (c == ']' || c == ')') {
            --depth;
        } else if (c == ',' && depth == 0) {
            out.push_back(trim_copy(args.substr(start, i - start)));
            start = i + 1;
        }
    }
    const std::string last = trim_copy(args.substr(start));
    if (!last.empty()) {
        out.push_back(last);
    }
    return out;
}

std::string lower_type(const std::string& raw_type) {
    std::string type = trim_copy(raw_type);
    static const std::map<std::string, std::string> builtins = {
        {"bool", "bool"},       {"i8", "int8_t"},    {"i16", "int16_t"},
        {"i32", "int32_t"},     {"i64", "int64_t"},  {"u8", "uint8_t"},
        {"u16", "uint16_t"},    {"u32", "uint32_t"}, {"u64", "uint64_t"},
        {"isize", "intptr_t"},  {"usize", "size_t"}, {"f32", "float"},
        {"f64", "double"},      {"void", "void"},    {"str", "std::string"},
        {"cstr", "const char*"}};

    if (type.empty()) {
        return "void";
    }
    if (starts_with(type, "fn(")) {
        return lower_function_type(type);
    }
    if (const auto found = builtins.find(type); found != builtins.end()) {
        return found->second;
    }
    if (starts_with(type, "*const[") && ends_with(type, "]")) {
        return lower_type(type.substr(7, type.size() - 8)) + " const*";
    }
    if (starts_with(type, "&const[") && ends_with(type, "]")) {
        return lower_type(type.substr(7, type.size() - 8)) + " const&";
    }
    if (starts_with(type, "*")) {
        return lower_type(type.substr(1)) + "*";
    }
    if (starts_with(type, "&")) {
        return lower_type(type.substr(1)) + "&";
    }

    const size_t open = type.find('[');
    if (open != std::string::npos && ends_with(type, "]")) {
        const std::string name = type.substr(0, open);
        const std::string args = type.substr(open + 1, type.size() - open - 2);
        if (name != "list" && name != "dict" && name != "set" && name != "Option" &&
            name != "Result" && name != "tuple" && name != "const" && name != "atomic" &&
            name != "volatile" && args.find(',') == std::string::npos) {
            return lower_type(name) + "[" + args + "]";
        }
        return lower_template_type(name, args);
    }
    return replace_dots(type);
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

} // namespace

std::string emit_cpp_header(const ModuleAst& module) {
    std::ostringstream out;
    out << "#pragma once\n\n";
    out << "#include <atomic>\n";
    out << "#include <cstddef>\n";
    out << "#include <cstdint>\n";
    out << "#include <functional>\n";
    out << "#include <optional>\n";
    out << "#include <string>\n";
    out << "#include <unordered_map>\n";
    out << "#include <unordered_set>\n";
    out << "#include <vector>\n";

    for (const ImportDecl& import : module.imports) {
        if (import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp) {
            out << "#include " << include_path(import) << '\n';
        }
    }
    out << '\n';

    for (const TypeAliasDecl& alias : module.aliases) {
        out << "using " << alias.name << " = " << lower_type(alias.type) << ";\n";
    }
    if (!module.aliases.empty()) {
        out << '\n';
    }

    for (const EnumDecl& en : module.enums) {
        out << "enum class " << en.name;
        if (!en.underlying_type.empty()) {
            out << " : " << lower_type(en.underlying_type);
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

    for (const size_t index : class_emit_order(module.classes)) {
        const ClassDecl& klass = module.classes[index];
        out << "struct " << klass.name << " {\n";
        for (const FieldDecl& field : klass.fields) {
            out << "    " << lower_type(field.type) << ' ' << field.name << "{};\n";
        }
        out << "};\n\n";
    }

    for (const ConstDecl& constant : module.constants) {
        out << "extern const " << lower_type(constant.type) << ' ' << constant.name << ";\n";
    }
    if (!module.constants.empty()) {
        out << '\n';
    }

    for (const FunctionDecl& fn : module.functions) {
        out << lower_type(fn.return_type) << ' ' << fn.name << '(';
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_type(fn.params[i].type) << ' ' << fn.params[i].name;
        }
        out << ");\n";
    }
    return out.str();
}

} // namespace dudu
