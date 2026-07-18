#include "dudu/codegen/cpp_emit_modules.hpp"

#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_emit_prelude.hpp"
#include "dudu/codegen/cpp_module_dependencies.hpp"
#include "dudu/codegen/cpp_module_emit_context.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/core/file_io.hpp"

#include <fstream>
#include <optional>
#include <set>
#include <span>
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

std::span<const ModuleAst> module_units(const ModuleAst& module) {
    if (module.module_units.empty()) {
        return {&module, 1};
    }
    return module.module_units;
}

std::vector<std::string> module_include_paths(const ModuleAst& unit, const CppModuleMap& modules,
                                              bool include_macro_host_modules,
                                              bool public_dependencies,
                                              const std::vector<bool>& public_imports) {
    std::set<std::string> paths;
    for (size_t i = 0; i < unit.imports.size(); ++i) {
        const ImportDecl& import = unit.imports[i];
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        if (import.module_path.empty() || import.module_path == unit.module_path) {
            continue;
        }
        if (public_imports[i] != public_dependencies) {
            continue;
        }
        const std::string resolved = resolved_module_path_for_import(unit, import);
        const auto dependency = modules.find(resolved);
        if (dependency != modules.end() && !include_macro_host_modules &&
            dependency->second->compilation_domain == CompilationDomain::MacroHost) {
            continue;
        }
        paths.insert(module_artifact_base_for_path(resolved).string() + ".hpp");
    }
    return {paths.begin(), paths.end()};
}

ModuleAst import_subset(const ModuleAst& unit, bool public_dependencies,
                        const std::vector<bool>& public_imports) {
    ModuleAst subset;
    for (size_t i = 0; i < unit.imports.size(); ++i) {
        if (public_imports[i] == public_dependencies) {
            subset.imports.push_back(unit.imports[i]);
        }
    }
    return subset;
}

void emit_dependency_includes(std::ostringstream& out, const ModuleAst& unit,
                              const CppModuleMap& modules, bool include_macro_host_modules,
                              bool public_dependencies) {
    const std::vector<bool> public_imports = cpp_public_import_mask(unit);
    emit_native_includes(out, import_subset(unit, public_dependencies, public_imports));
    const std::vector<std::string> includes = module_include_paths(
        unit, modules, include_macro_host_modules, public_dependencies, public_imports);
    for (const std::string& path : includes) {
        out << "#include \"" << path << "\"\n";
    }
    if (!includes.empty()) {
        out << '\n';
    }
}

void emit_module_header_includes(std::ostringstream& out, const ModuleAst& unit,
                                 const CppModuleMap& modules, bool include_macro_host_modules) {
    out << "#include \"dudu_runtime.hpp\"\n";
    emit_dependency_includes(out, unit, modules, include_macro_host_modules, true);
    out << '\n';
}

std::string runtime_header(const ModuleAst& module) {
    std::ostringstream out;
    emit_generated_banner(out);
    out << "#pragma once\n\n";
    emit_prelude(out, module, false);
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
        if (type_ref_is_void(function_return_type_ref(fn))) {
            out << "    " << call << ";\n"
                << "    return 0;\n";
        } else {
            out << "    return " << call << ";\n";
        }
        out << "}\n";
        return;
    }
}

std::string header_with_module_includes(const ModuleAst& module_tree, const ModuleAst& unit,
                                        const CppModuleMap& modules, bool test_source = false,
                                        bool public_abi = false,
                                        bool include_macro_host_modules = false) {
    std::ostringstream out;
    emit_module_header_includes(out, unit, modules, include_macro_host_modules);
    CppEmitOptions options = make_cpp_module_emit_options(unit, modules, test_source, public_abi,
                                                          include_macro_host_modules);
    options.module_tree = &module_tree;
    out << emit_cpp_header(unit, options);
    return out.str();
}

std::string source_with_boundary_comment(const ModuleAst& module_tree, const ModuleAst& unit,
                                         const CppModuleMap& modules, bool test_source = false,
                                         bool public_abi = false,
                                         bool include_macro_host_modules = false) {
    std::ostringstream out;
    CppEmitOptions options = make_cpp_module_emit_options(unit, modules, test_source, public_abi,
                                                          include_macro_host_modules);
    options.module_tree = &module_tree;
    emit_generated_banner(out);
    out << "// dudu module: " << (unit.module_path.empty() ? "main" : unit.module_path) << "\n"
        << "#include \"" << module_artifact_base(unit).string() << ".hpp\"\n\n";
    emit_dependency_includes(out, unit, modules, include_macro_host_modules, false);
    out << emit_cpp_module_implementation(unit, options);
    if (!test_source) {
        emit_entry_point(out, unit, options);
    }
    return out.str();
}

void append_artifacts(std::vector<CppModuleArtifact>& out, const ModuleAst& module_tree,
                      const ModuleAst& unit, const CppModuleMap& modules, bool test_source = false,
                      bool public_abi = false, bool include_macro_host_modules = false) {
    const std::filesystem::path base = module_artifact_base(unit);
    out.push_back({.path = base.string() + ".hpp",
                   .module_path = unit.module_path,
                   .kind = CppModuleArtifactKind::Header,
                   .content = header_with_module_includes(module_tree, unit, modules, test_source,
                                                          public_abi, include_macro_host_modules)});
    out.push_back(
        {.path = base.string() + ".cpp",
         .module_path = unit.module_path,
         .kind = CppModuleArtifactKind::Source,
         .content = source_with_boundary_comment(module_tree, unit, modules, test_source,
                                                 public_abi, include_macro_host_modules)});
}

CppModuleMap module_map(std::span<const ModuleAst> units) {
    CppModuleMap out;
    for (const ModuleAst& unit : units) {
        out[unit.module_path] = &unit;
    }
    return out;
}

std::set<std::string> module_filter(const std::vector<std::string>& module_paths) {
    return {module_paths.begin(), module_paths.end()};
}

bool should_emit_module(const std::set<std::string>& module_paths, const ModuleAst& unit,
                        bool include_macro_host_modules = false) {
    return (include_macro_host_modules ||
            unit.compilation_domain != CompilationDomain::MacroHost) &&
           (module_paths.empty() || module_paths.contains(unit.module_path));
}

ModuleAst test_harness_module(const ModuleAst& module) {
    ModuleAst out;
    for (const ModuleAst& unit : module_units(module)) {
        for (const FunctionDecl& fn : unit.functions) {
            if (is_test_function(fn)) {
                out.functions.push_back(fn);
            }
        }
    }
    return out;
}

std::string test_harness_source(const ModuleAst& module, const std::string& filter,
                                bool capture_output) {
    std::ostringstream out;
    out << "#include <exception>\n"
           "#include <iostream>\n"
           "#include <sstream>\n"
           "#include <string>\n"
           "#include <string_view>\n"
           "#include <type_traits>\n"
           "#include \"dudu_runtime.hpp\"\n";
    for (const ModuleAst& unit : module_units(module)) {
        if (unit.compilation_domain == CompilationDomain::MacroHost) {
            continue;
        }
        out << "#include \"" << module_artifact_base(unit).string() << ".hpp\"\n";
    }
    out << '\n';
    CppEmitOptions options;
    options.use_generated_names = true;
    emit_test_harness(out, test_harness_module(module), filter, capture_output, options);
    return out.str();
}

} // namespace

std::vector<CppModuleArtifact> emit_cpp_module_artifacts(const ModuleAst& module) {
    const std::span<const ModuleAst> units = module_units(module);
    std::vector<std::string> module_paths;
    module_paths.reserve(units.size());
    for (const ModuleAst& unit : units) {
        module_paths.push_back(unit.module_path);
    }
    return emit_cpp_module_artifacts(module, module_paths);
}

std::vector<CppModuleArtifact>
emit_cpp_module_artifacts(const ModuleAst& module, const std::vector<std::string>& module_paths) {
    return emit_cpp_module_artifacts(module, module_paths, {});
}

std::vector<CppModuleArtifact>
emit_cpp_module_artifacts(const ModuleAst& module, const std::vector<std::string>& module_paths,
                          CppModuleEmitOptions emit_options) {
    std::vector<CppModuleArtifact> out;
    if (module_paths.empty()) {
        return out;
    }
    const std::set<std::string> filter = module_filter(module_paths);
    out.push_back({.path = "dudu_runtime.hpp",
                   .module_path = {},
                   .kind = CppModuleArtifactKind::Header,
                   .content = runtime_header(module)});
    const std::span<const ModuleAst> units = module_units(module);
    const CppModuleMap modules = module_map(units);
    const bool public_abi = preserve_public_abi_names(module);
    for (const ModuleAst& unit : units) {
        if (should_emit_module(filter, unit, emit_options.include_macro_host_modules)) {
            append_artifacts(out, module, unit, modules, false, public_abi,
                             emit_options.include_macro_host_modules);
        }
    }
    return out;
}

std::vector<CppModuleArtifact> emit_cpp_test_module_artifacts(const ModuleAst& module,
                                                              const std::string& filter,
                                                              bool capture_output) {
    const std::span<const ModuleAst> units = module_units(module);
    std::vector<std::string> module_paths;
    module_paths.reserve(units.size());
    for (const ModuleAst& unit : units) {
        module_paths.push_back(unit.module_path);
    }
    return emit_cpp_test_module_artifacts(module, module_paths, filter, capture_output);
}

std::vector<CppModuleArtifact>
emit_cpp_test_module_artifacts(const ModuleAst& module,
                               const std::vector<std::string>& module_paths,
                               const std::string& filter, bool capture_output) {
    std::vector<CppModuleArtifact> out;
    if (module_paths.empty()) {
        return out;
    }
    const std::set<std::string> module_filter_set = module_filter(module_paths);
    out.push_back({.path = "dudu_runtime.hpp",
                   .module_path = {},
                   .kind = CppModuleArtifactKind::Header,
                   .content = runtime_header(module)});
    const std::span<const ModuleAst> units = module_units(module);
    const CppModuleMap modules = module_map(units);
    for (const ModuleAst& unit : units) {
        if (should_emit_module(module_filter_set, unit)) {
            append_artifacts(out, module, unit, modules, true);
        }
    }
    out.push_back({.path = "test_harness.cpp",
                   .module_path = {},
                   .kind = CppModuleArtifactKind::Source,
                   .content = test_harness_source(module, filter, capture_output)});
    return out;
}

std::vector<std::filesystem::path> cpp_module_source_paths(const ModuleAst& module) {
    std::vector<std::filesystem::path> out;
    const std::span<const ModuleAst> units = module_units(module);
    out.reserve(units.size());
    for (const ModuleAst& unit : units) {
        if (unit.compilation_domain == CompilationDomain::MacroHost) {
            continue;
        }
        out.push_back(module_artifact_base(unit).string() + ".cpp");
    }
    return out;
}

std::vector<std::filesystem::path> cpp_test_module_source_paths(const ModuleAst& module) {
    std::vector<std::filesystem::path> out = cpp_module_source_paths(module);
    out.push_back("test_harness.cpp");
    return out;
}

std::vector<std::filesystem::path> cpp_module_artifact_paths(const ModuleAst& module) {
    std::vector<std::filesystem::path> out;
    out.push_back("dudu_runtime.hpp");
    const std::span<const ModuleAst> units = module_units(module);
    out.reserve(1 + units.size() * 2);
    for (const ModuleAst& unit : units) {
        if (unit.compilation_domain == CompilationDomain::MacroHost) {
            continue;
        }
        const std::filesystem::path base = module_artifact_base(unit);
        out.push_back(base.string() + ".hpp");
        out.push_back(base.string() + ".cpp");
    }
    return out;
}

std::vector<std::filesystem::path> cpp_test_module_artifact_paths(const ModuleAst& module) {
    std::vector<std::filesystem::path> out = cpp_module_artifact_paths(module);
    out.push_back("test_harness.cpp");
    return out;
}

bool file_content_matches(const std::filesystem::path& path, const std::string& content) {
    std::optional<std::string> existing = try_read_text_file(path);
    if (!existing) {
        return false;
    }
    return *existing == content;
}

void write_cpp_artifacts(const std::filesystem::path& dir,
                         const std::vector<CppModuleArtifact>& artifacts) {
    if (dir.empty()) {
        throw std::runtime_error("emit-modules requires -o <directory>");
    }
    for (const CppModuleArtifact& artifact : artifacts) {
        const std::filesystem::path path = dir / artifact.path;
        std::filesystem::create_directories(path.parent_path().empty() ? dir : path.parent_path());
        if (file_content_matches(path, artifact.content)) {
            continue;
        }
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("could not open output " + path.string());
        }
        out << artifact.content;
    }
}

} // namespace dudu
