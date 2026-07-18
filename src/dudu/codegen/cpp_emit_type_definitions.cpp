#include "dudu/codegen/cpp_emit_type_definitions.hpp"

#include "dudu/codegen/cpp_emit_classes.hpp"
#include "dudu/codegen/cpp_emit_declaration_support.hpp"
#include "dudu/codegen/cpp_emit_enums.hpp"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace dudu {
namespace {

using Definition = std::variant<const ClassDecl*, const EnumDecl*>;
using DefinitionIndex = std::unordered_map<std::string_view, size_t>;

enum class VisitState : unsigned char { Unvisited, Visiting, Emitted };

std::string_view definition_name(const Definition& definition) {
    return std::visit([](const auto* declaration) -> std::string_view { return declaration->name; },
                      definition);
}

std::string_view named_type_head(const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
    case TypeKind::Associated:
    case TypeKind::AssociatedTemplate:
        return type.name.str();
    default:
        return {};
    }
}

void collect_dependencies(const TypeRef& type, const DefinitionIndex& definition_index,
                          std::vector<size_t>& dependencies) {
    if (type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference) {
        return;
    }
    if (const auto found = definition_index.find(named_type_head(type));
        found != definition_index.end()) {
        dependencies.push_back(found->second);
    }
    for (const TypeRef& child : type.children) {
        collect_dependencies(child, definition_index, dependencies);
    }
}

std::vector<Definition> definitions_for(const ModuleAst& module, bool header_only) {
    std::vector<Definition> definitions;
    definitions.reserve(module.classes.size() + module.enums.size());
    for (const ClassDecl& klass : module.classes) {
        if (!header_only || visible_in_cpp_header(klass.visibility)) {
            definitions.emplace_back(&klass);
        }
    }
    for (const EnumDecl& en : module.enums) {
        if (enum_has_payload_fields(en)) {
            definitions.emplace_back(&en);
        }
    }
    return definitions;
}

std::vector<std::vector<size_t>> dependency_graph(const std::vector<Definition>& definitions) {
    DefinitionIndex definition_index;
    definition_index.reserve(definitions.size());
    for (size_t index = 0; index < definitions.size(); ++index) {
        definition_index.emplace(definition_name(definitions[index]), index);
    }

    std::vector<std::vector<size_t>> dependencies(definitions.size());
    for (size_t index = 0; index < definitions.size(); ++index) {
        std::vector<size_t>& current = dependencies[index];
        std::visit(
            [&](const auto* declaration) {
                using Decl = std::remove_cv_t<std::remove_pointer_t<decltype(declaration)>>;
                if constexpr (std::is_same_v<Decl, ClassDecl>) {
                    for (const BaseClassDecl& base : declaration->base_class_refs) {
                        collect_dependencies(base.type_ref, definition_index, current);
                    }
                    for (const FieldDecl& field : declaration->fields) {
                        collect_dependencies(field.type_ref, definition_index, current);
                    }
                } else {
                    for (const EnumValueDecl& value : declaration->values) {
                        for (const EnumPayloadField& field : value.payload_fields) {
                            collect_dependencies(field.type_ref, definition_index, current);
                        }
                    }
                }
            },
            definitions[index]);
        std::ranges::sort(current);
        current.erase(std::unique(current.begin(), current.end()), current.end());
    }
    return dependencies;
}

void visit_definition(const std::vector<std::vector<size_t>>& dependencies, size_t index,
                      std::vector<VisitState>& states, std::vector<size_t>& order) {
    if (states[index] == VisitState::Emitted) {
        return;
    }
    if (states[index] == VisitState::Visiting) {
        return;
    }
    states[index] = VisitState::Visiting;
    for (const size_t dependency : dependencies[index]) {
        if (dependency != index) {
            visit_definition(dependencies, dependency, states, order);
        }
    }
    states[index] = VisitState::Emitted;
    order.push_back(index);
}

std::vector<size_t> definition_order(const std::vector<Definition>& definitions) {
    const std::vector<std::vector<size_t>> dependencies = dependency_graph(definitions);
    std::vector<VisitState> states(definitions.size(), VisitState::Unvisited);
    std::vector<size_t> order;
    order.reserve(definitions.size());
    for (size_t index = 0; index < definitions.size(); ++index) {
        visit_definition(dependencies, index, states, order);
    }
    return order;
}

} // namespace

void emit_class_and_payload_enum_definitions(std::ostringstream& out, const ModuleAst& module,
                                             const std::vector<std::string>& aliases,
                                             const std::map<std::string, TypeRef>& function_returns,
                                             const Symbols& symbols, bool header_only,
                                             const CppEmitOptions& options) {
    const std::vector<Definition> definitions = definitions_for(module, header_only);
    const std::vector<size_t> order = definition_order(definitions);
    for (const size_t index : order) {
        std::visit(
            [&](const auto* declaration) {
                using Decl = std::remove_cv_t<std::remove_pointer_t<decltype(declaration)>>;
                if constexpr (std::is_same_v<Decl, ClassDecl>) {
                    emit_class_definition(out, *declaration, aliases, function_returns, symbols,
                                          options);
                } else {
                    emit_payload_enum_definition(out, *declaration, aliases, options);
                }
            },
            definitions[index]);
    }
    if (!header_only) {
        for (const size_t index : order) {
            if (const auto* klass = std::get_if<const ClassDecl*>(&definitions[index])) {
                emit_class_out_of_line_definitions(out, **klass, aliases, function_returns, symbols,
                                                   options);
            }
        }
    }
}

} // namespace dudu
