#include "dudu/sema_constructors.hpp"

namespace dudu {

std::vector<ConstructorParam> constructor_params(const ClassDecl& klass) {
    for (const FunctionDecl& method : klass.methods) {
        if (method.name != "__init__") {
            continue;
        }
        std::vector<ConstructorParam> out;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            out.push_back({.name = method.params[i].name, .type = method.params[i].type});
        }
        return out;
    }

    std::vector<ConstructorParam> out;
    for (const FieldDecl& field : klass.fields) {
        out.push_back({.name = field.name, .type = field.type});
    }
    return out;
}

} // namespace dudu
