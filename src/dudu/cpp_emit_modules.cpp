#include "dudu/cpp_emit_modules.hpp"

#include "dudu/cpp_emit.hpp"

#include <set>
#include <sstream>

namespace dudu {
namespace {

std::filesystem::path module_artifact_base_for_path(const std::string& module_path) {
    std::filesystem::path out;
    const std::string path = module_path.empty() ? "main" : module_path;
    size_t start = 0;
    while (start < path.size()) {
        const size_t dot = path.find('.', start);
        const size_t end = dot == std::string::npos ? path.size() : dot;
        out /= path.substr(start, end - start);
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return out;
}

std::filesystem::path module_artifact_base(const ModuleAst& module) {
    return module_artifact_base_for_path(module.module_path);
}

CppEmitOptions module_emit_options(const ModuleAst& unit) {
    CppEmitOptions options;
    options.use_generated_names = true;
    for (const TypeAliasDecl& alias : unit.aliases) {
        if (!alias.cpp_name.empty()) {
            options.generated_type_names[alias.name] = alias.cpp_name;
        }
    }
    for (const EnumDecl& en : unit.enums) {
        if (!en.cpp_name.empty()) {
            options.generated_type_names[en.name] = en.cpp_name;
            options.generated_value_names[en.name] = en.cpp_name;
        }
    }
    for (const ClassDecl& klass : unit.classes) {
        if (!klass.cpp_name.empty()) {
            options.generated_type_names[klass.name] = klass.cpp_name;
            options.generated_value_names[klass.name] = klass.cpp_name;
        }
        for (const ConstDecl& constant : klass.constants) {
            if (!constant.cpp_name.empty()) {
                options.generated_value_names[klass.name + "." + constant.name] = constant.cpp_name;
            }
        }
    }
    for (const ConstDecl& constant : unit.constants) {
        if (!constant.cpp_name.empty()) {
            options.generated_value_names[constant.name] = constant.cpp_name;
        }
    }
    for (const FunctionDecl& fn : unit.functions) {
        if (!fn.cpp_name.empty()) {
            options.generated_value_names[fn.name] = fn.cpp_name;
        }
    }
    return options;
}

std::vector<std::string> module_include_paths(const ModuleAst& unit) {
    std::set<std::string> paths;
    for (const ImportDecl& import : unit.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        if (import.module_path.empty() || import.module_path == unit.module_path) {
            continue;
        }
        paths.insert(module_artifact_base_for_path(import.module_path).string() + ".hpp");
    }
    return {paths.begin(), paths.end()};
}

void emit_module_includes(std::ostringstream& out, const ModuleAst& unit) {
    const std::vector<std::string> includes = module_include_paths(unit);
    for (const std::string& path : includes) {
        out << "#include \"" << path << "\"\n";
    }
    if (!includes.empty()) {
        out << '\n';
    }
}

std::string header_with_module_includes(const ModuleAst& unit) {
    std::ostringstream out;
    emit_module_includes(out, unit);
    out << emit_cpp_header(unit, module_emit_options(unit));
    return out.str();
}

std::string source_with_boundary_comment(const ModuleAst& unit) {
    std::ostringstream out;
    const CppEmitOptions options = module_emit_options(unit);
    out << "// dudu module: " << (unit.module_path.empty() ? "main" : unit.module_path) << "\n";
    emit_module_includes(out, unit);
    out << emit_cpp_source(unit, options);
    return out.str();
}

void append_artifacts(std::vector<CppModuleArtifact>& out, const ModuleAst& unit) {
    const std::filesystem::path base = module_artifact_base(unit);
    out.push_back({.path = base.string() + ".hpp",
                   .module_path = unit.module_path,
                   .kind = CppModuleArtifactKind::Header,
                   .content = header_with_module_includes(unit)});
    out.push_back({.path = base.string() + ".cpp",
                   .module_path = unit.module_path,
                   .kind = CppModuleArtifactKind::Source,
                   .content = source_with_boundary_comment(unit)});
}

} // namespace

std::vector<CppModuleArtifact> emit_cpp_module_artifacts(const ModuleAst& module) {
    std::vector<CppModuleArtifact> out;
    if (module.module_units.empty()) {
        append_artifacts(out, module);
        return out;
    }
    for (const ModuleAst& unit : module.module_units) {
        append_artifacts(out, unit);
    }
    return out;
}

} // namespace dudu
