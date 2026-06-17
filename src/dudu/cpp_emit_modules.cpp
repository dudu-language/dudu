#include "dudu/cpp_emit_modules.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_emit_prelude.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

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

void add_local_generated_names(CppEmitOptions& options, const ModuleAst& unit) {
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
}

void add_imported_generated_names(CppEmitOptions& options, const ModuleAst& dependency,
                                  const ImportDecl& import) {
    const std::string prefix = import.alias.empty() ? import.module_path : import.alias;
    const bool selective = import.kind == ImportKind::From;
    const std::string selective_name = import.alias.empty() ? import.imported_name : import.alias;
    auto expose = [&](const std::string& name) {
        return selective ? selective_name : prefix + "." + name;
    };
    for (const TypeAliasDecl& alias : dependency.aliases) {
        if ((!selective || alias.name == import.imported_name) && !alias.cpp_name.empty()) {
            options.generated_type_names[expose(alias.name)] = alias.cpp_name;
        }
    }
    for (const EnumDecl& en : dependency.enums) {
        if ((!selective || en.name == import.imported_name) && !en.cpp_name.empty()) {
            options.generated_type_names[expose(en.name)] = en.cpp_name;
            options.generated_value_names[expose(en.name)] = en.cpp_name;
        }
    }
    for (const ClassDecl& klass : dependency.classes) {
        if ((!selective || klass.name == import.imported_name) && !klass.cpp_name.empty()) {
            options.generated_type_names[expose(klass.name)] = klass.cpp_name;
            options.generated_value_names[expose(klass.name)] = klass.cpp_name;
        }
    }
    for (const ConstDecl& constant : dependency.constants) {
        if ((!selective || constant.name == import.imported_name) && !constant.cpp_name.empty()) {
            options.generated_value_names[expose(constant.name)] = constant.cpp_name;
        }
    }
    for (const FunctionDecl& fn : dependency.functions) {
        if ((!selective || fn.name == import.imported_name) && !fn.cpp_name.empty()) {
            options.generated_value_names[expose(fn.name)] = fn.cpp_name;
        }
    }
}

CppEmitOptions module_emit_options(const ModuleAst& unit,
                                   const std::map<std::string, const ModuleAst*>& modules) {
    CppEmitOptions options;
    options.emit_prelude = false;
    options.use_generated_names = true;
    add_local_generated_names(options, unit);
    for (const ImportDecl& import : unit.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const auto dependency = modules.find(import.module_path);
        if (dependency != modules.end()) {
            add_imported_generated_names(options, *dependency->second, import);
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
    out << "#include \"dudu_runtime.hpp\"\n";
    const std::vector<std::string> includes = module_include_paths(unit);
    for (const std::string& path : includes) {
        out << "#include \"" << path << "\"\n";
    }
    out << '\n';
}

std::string runtime_header(const ModuleAst& module) {
    std::ostringstream out;
    out << "#pragma once\n\n";
    emit_includes(out, module);
    emit_result_prelude(out, module);
    return out.str();
}

void emit_entry_point(std::ostringstream& out, const ModuleAst& unit,
                      const CppEmitOptions& options) {
    for (const FunctionDecl& fn : unit.functions) {
        if (fn.name != "main") {
            continue;
        }
        out << "int main() {\n";
        const std::string call = emitted_name(fn, options) + "()";
        if (function_return_type_text(fn) == "void") {
            out << "    " << call << ";\n"
                << "    return 0;\n";
        } else {
            out << "    return " << call << ";\n";
        }
        out << "}\n";
        return;
    }
}

std::string header_with_module_includes(const ModuleAst& unit,
                                        const std::map<std::string, const ModuleAst*>& modules) {
    std::ostringstream out;
    emit_module_includes(out, unit);
    out << emit_cpp_header(unit, module_emit_options(unit, modules));
    return out.str();
}

std::string source_with_boundary_comment(const ModuleAst& unit,
                                         const std::map<std::string, const ModuleAst*>& modules) {
    std::ostringstream out;
    const CppEmitOptions options = module_emit_options(unit, modules);
    out << "// dudu module: " << (unit.module_path.empty() ? "main" : unit.module_path) << "\n";
    emit_module_includes(out, unit);
    out << emit_cpp_source(unit, options);
    emit_entry_point(out, unit, options);
    return out.str();
}

void append_artifacts(std::vector<CppModuleArtifact>& out, const ModuleAst& unit,
                      const std::map<std::string, const ModuleAst*>& modules) {
    const std::filesystem::path base = module_artifact_base(unit);
    out.push_back({.path = base.string() + ".hpp",
                   .module_path = unit.module_path,
                   .kind = CppModuleArtifactKind::Header,
                   .content = header_with_module_includes(unit, modules)});
    out.push_back({.path = base.string() + ".cpp",
                   .module_path = unit.module_path,
                   .kind = CppModuleArtifactKind::Source,
                   .content = source_with_boundary_comment(unit, modules)});
}

std::map<std::string, const ModuleAst*> module_map(const std::vector<ModuleAst>& units) {
    std::map<std::string, const ModuleAst*> out;
    for (const ModuleAst& unit : units) {
        out[unit.module_path] = &unit;
    }
    return out;
}

} // namespace

std::vector<CppModuleArtifact> emit_cpp_module_artifacts(const ModuleAst& module) {
    std::vector<CppModuleArtifact> out;
    out.push_back({.path = "dudu_runtime.hpp",
                   .module_path = {},
                   .kind = CppModuleArtifactKind::Header,
                   .content = runtime_header(module)});
    if (module.module_units.empty()) {
        append_artifacts(out, module, {{module.module_path, &module}});
        return out;
    }
    const std::map<std::string, const ModuleAst*> modules = module_map(module.module_units);
    for (const ModuleAst& unit : module.module_units) {
        append_artifacts(out, unit, modules);
    }
    return out;
}

void write_cpp_module_artifacts(const std::filesystem::path& dir, const ModuleAst& module) {
    if (dir.empty()) {
        throw std::runtime_error("emit-modules requires -o <directory>");
    }
    for (const CppModuleArtifact& artifact : emit_cpp_module_artifacts(module)) {
        const std::filesystem::path path = dir / artifact.path;
        std::filesystem::create_directories(path.parent_path().empty() ? dir : path.parent_path());
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("could not open output " + path.string());
        }
        out << artifact.content;
    }
}

} // namespace dudu
