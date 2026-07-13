#pragma once

#include "dudu/core/ast.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dudu::macro {

enum class TargetKind {
    Any,
    Class,
    Enum,
    Function,
    Field,
    Constant,
};

struct Definition {
    std::string name;
    std::string identity;
    std::string module_path;
    TargetKind accepted_kind = TargetKind::Any;
    const FunctionDecl* function = nullptr;
    const ClassDecl* attribute_schema = nullptr;
    SourceLocation location;
};

struct HelperAttribute {
    const Definition* macro = nullptr;
    const Decorator* decorator = nullptr;
    std::string target_name;
    TargetKind target_kind = TargetKind::Any;
};

struct Invocation {
    const Definition* macro = nullptr;
    const Decorator* decorator = nullptr;
    std::string target_module;
    std::string target_name;
    TargetKind target_kind = TargetKind::Any;
    bool derive = false;
    std::vector<HelperAttribute> helper_attributes;
};

struct Plan {
    std::map<std::string, Definition> definitions;
    std::vector<Invocation> invocations;
};

Plan build_plan(const ModuleAst& module);
std::string_view target_kind_name(TargetKind kind);

} // namespace dudu::macro
