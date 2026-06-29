#pragma once

#include "dudu/core/ast.hpp"

#include <set>
#include <string>

namespace dudu {

class ProjectIndex;

struct NativeSemanticIndex {
    std::set<std::string> types;
    std::set<std::string> classes;
    std::set<std::string> class_aliases;
    std::set<std::string> values;
    std::set<std::string> functions;
    std::set<std::string> macros;
    std::set<std::string> namespaces;
    std::set<std::string> enum_members;
    std::set<std::string> methods;
};

struct DuduSemanticIndex {
    std::set<std::string> namespaces;
    std::set<std::string> types;
    std::set<std::string> classes;
    std::set<std::string> enums;
    std::set<std::string> enum_members;
    std::set<std::string> functions;
    std::set<std::string> values;
};

NativeSemanticIndex native_semantic_index(const ModuleAst& module);
DuduSemanticIndex dudu_semantic_index(const ModuleAst& module);
DuduSemanticIndex dudu_semantic_index(const ProjectIndex& index, const ModuleAst& current);

} // namespace dudu
