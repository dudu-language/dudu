#include "dudu/sema_method_templates.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <cctype>

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

} // namespace

std::vector<std::string> template_args_from_type(const std::string& type) {
    const size_t open = type.find('[');
    if (open == std::string::npos || type.empty() || type.back() != ']') {
        return {};
    }
    return split_top_level_args(type.substr(open + 1, type.size() - open - 2));
}

std::string substitute_method_template_type(std::string type,
                                            const std::vector<std::string>& generic_params,
                                            const std::vector<std::string>& args) {
    for (size_t i = 0; i < generic_params.size() && i < args.size(); ++i) {
        type = replace_type_identifier(std::move(type), generic_params[i], args[i]);
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
         {"T", "_T", "_Tp", "_Tp1", "_Ty", "_Ty1", "value_type", "element_type", "key_type"}) {
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

} // namespace dudu
