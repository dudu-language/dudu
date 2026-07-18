#include "dudu/codegen/cpp_emit_declaration_support.hpp"

#include "dudu/sema/sema_generics.hpp"

#include <sstream>

namespace dudu {

bool visible_in_cpp_header(Visibility visibility) {
    return visibility != Visibility::Private;
}

void emit_cpp_template_parameters(std::ostringstream& out, const std::vector<std::string>& params,
                                  const std::set<std::string>& value_params,
                                  std::string_view prefix) {
    if (params.empty()) {
        return;
    }
    out << prefix << "template <";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        const std::string name = generic_param_base_name(params[i]);
        if (generic_param_is_pack(params[i])) {
            out << (value_params.contains(name) ? "size_t... " : "typename... ") << name;
        } else {
            out << (value_params.contains(name) ? "size_t " : "typename ") << name;
        }
    }
    out << ">\n";
}

} // namespace dudu
