#include "dudu/sema_method_templates.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <cctype>
#include <map>

namespace dudu {
namespace {

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

std::map<std::string, std::string>
receiver_template_substitutions(const std::vector<std::string>& receiver_args) {
    std::map<std::string, std::string> substitutions;
    if (receiver_args.empty()) {
        return substitutions;
    }
    const std::string first = trim_copy(receiver_args.front());
    for (const char* name :
         {"T", "_T", "_Tp", "_Tp1", "_Ty", "_Ty1", "value_type", "element_type", "key_type"}) {
        substitutions.emplace(name, first);
    }
    if (receiver_args.size() >= 2) {
        const std::string first_arg = trim_copy(receiver_args[0]);
        const std::string second_arg = trim_copy(receiver_args[1]);
        substitutions.insert_or_assign("_Key", first_arg);
        substitutions.insert_or_assign("_Val", second_arg);
        substitutions.insert_or_assign("_T1", first_arg);
        substitutions.insert_or_assign("_T2", second_arg);
        substitutions.insert_or_assign("_Tp1", first_arg);
        substitutions.insert_or_assign("_Tp2", second_arg);
        substitutions.insert_or_assign("_Ty1", first_arg);
        substitutions.insert_or_assign("_Ty2", second_arg);
        substitutions.insert_or_assign("mapped_type", second_arg);
        substitutions.insert_or_assign("key_type", first_arg);
    }
    return substitutions;
}

std::string
substitute_native_type_identifiers(std::string type,
                                   const std::map<std::string, std::string>& substitutions) {
    for (const auto& [name, value] : substitutions) {
        type = replace_type_identifier(std::move(type), name, value);
    }
    return type;
}

} // namespace

std::vector<std::string> template_args_from_type(const std::string& type) {
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::Template) {
        return {};
    }
    std::vector<std::string> out;
    out.reserve(parsed.children.size());
    for (const TypeRef& child : parsed.children) {
        out.push_back(substitute_type_ref_text(child, {}));
    }
    return out;
}

std::string substitute_receiver_template_type(std::string type,
                                              const std::vector<std::string>& receiver_args) {
    const std::map<std::string, std::string> substitutions =
        receiver_template_substitutions(receiver_args);
    if (substitutions.empty()) {
        return type;
    }
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::Unknown) {
        return substitute_type_ref_text(parsed, substitutions);
    }
    return substitute_native_type_identifiers(std::move(type), substitutions);
}

} // namespace dudu
