#include "dudu/cpp_emit_modules.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_emit_internal.hpp"
#include "dudu/cpp_emit_prelude.hpp"
#include "dudu/file_io.hpp"

#include <fstream>
#include <optional>
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

bool public_abi_function(const FunctionDecl& fn, bool test_source) {
    return fn.visibility != Visibility::Private && (test_source || !cpp_emit_function_is_test(fn));
}

std::string module_target_kind(const ModuleAst& module) {
    const auto found = module.build_values.find("TARGET_KIND");
    if (found == module.build_values.end()) {
        return {};
    }
    std::string value = found->second;
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool preserve_public_abi_names(const ModuleAst& module) {
    const std::string kind = module_target_kind(module);
    return kind == "library" || kind == "shared_library";
}

void add_local_generated_names(CppEmitOptions& options, const ModuleAst& unit, bool public_abi,
                               bool test_source) {
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
            options.generated_value_names[fn.name] =
                cpp_emit_function_has_decorator(fn, "extern_c") ||
                        (public_abi && public_abi_function(fn, test_source))
                    ? fn.name
                    : fn.cpp_name;
        }
    }
}

void add_imported_generated_names(CppEmitOptions& options, const ModuleAst& dependency,
                                  const ImportDecl& import, bool public_abi, bool test_source) {
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
            options.generated_value_names[expose(fn.name)] =
                cpp_emit_function_has_decorator(fn, "extern_c") ||
                        (public_abi && public_abi_function(fn, test_source))
                    ? fn.name
                    : fn.cpp_name;
        }
    }
}

std::string resolved_module_path_for_import(const ModuleAst& unit, const ImportDecl& import) {
    for (const ModuleDependency& dependency : unit.dependencies) {
        if (dependency.kind == import.kind && dependency.import_module_path == import.module_path &&
            dependency.location.line == import.location.line &&
            dependency.location.column == import.location.column) {
            return dependency.resolved_module_path;
        }
    }
    return import.module_path;
}

CppEmitOptions module_emit_options(const ModuleAst& unit,
                                   const std::map<std::string, const ModuleAst*>& modules,
                                   bool test_source = false, bool public_abi = false) {
    CppEmitOptions options;
    options.emit_prelude = false;
    options.use_generated_names = true;
    options.test_source = test_source;
    options.expose_test_functions = test_source;
    add_local_generated_names(options, unit, public_abi, test_source);
    for (const ImportDecl& import : unit.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const auto dependency = modules.find(resolved_module_path_for_import(unit, import));
        if (dependency != modules.end()) {
            add_imported_generated_names(options, *dependency->second, import, public_abi,
                                         test_source);
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
        paths.insert(
            module_artifact_base_for_path(resolved_module_path_for_import(unit, import)).string() +
            ".hpp");
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

std::string header_with_module_includes(const ModuleAst& unit,
                                        const std::map<std::string, const ModuleAst*>& modules,
                                        bool test_source = false, bool public_abi = false) {
    std::ostringstream out;
    emit_module_includes(out, unit);
    out << emit_cpp_header(unit, module_emit_options(unit, modules, test_source, public_abi));
    return out.str();
}

std::string source_with_boundary_comment(const ModuleAst& unit,
                                         const std::map<std::string, const ModuleAst*>& modules,
                                         bool test_source = false, bool public_abi = false) {
    std::ostringstream out;
    const CppEmitOptions options = module_emit_options(unit, modules, test_source, public_abi);
    out << "// dudu module: " << (unit.module_path.empty() ? "main" : unit.module_path) << "\n";
    emit_module_includes(out, unit);
    out << emit_cpp_source(unit, options);
    if (!test_source) {
        emit_entry_point(out, unit, options);
    }
    return out.str();
}

void append_artifacts(std::vector<CppModuleArtifact>& out, const ModuleAst& unit,
                      const std::map<std::string, const ModuleAst*>& modules,
                      bool test_source = false, bool public_abi = false) {
    const std::filesystem::path base = module_artifact_base(unit);
    out.push_back({.path = base.string() + ".hpp",
                   .module_path = unit.module_path,
                   .kind = CppModuleArtifactKind::Header,
                   .content = header_with_module_includes(unit, modules, test_source, public_abi)});
    out.push_back(
        {.path = base.string() + ".cpp",
         .module_path = unit.module_path,
         .kind = CppModuleArtifactKind::Source,
         .content = source_with_boundary_comment(unit, modules, test_source, public_abi)});
}

std::map<std::string, const ModuleAst*> module_map(const std::vector<ModuleAst>& units) {
    std::map<std::string, const ModuleAst*> out;
    for (const ModuleAst& unit : units) {
        out[unit.module_path] = &unit;
    }
    return out;
}

std::set<std::string> module_filter(const std::vector<std::string>& module_paths) {
    return {module_paths.begin(), module_paths.end()};
}

bool should_emit_module(const std::set<std::string>& module_paths, const ModuleAst& unit) {
    return module_paths.empty() || module_paths.contains(unit.module_path);
}

ModuleAst test_harness_module(const ModuleAst& module) {
    ModuleAst out;
    const std::vector<ModuleAst>& units =
        module.module_units.empty() ? std::vector<ModuleAst>{module} : module.module_units;
    for (const ModuleAst& unit : units) {
        for (const FunctionDecl& fn : unit.functions) {
            if (cpp_emit_function_is_test(fn)) {
                out.functions.push_back(fn);
            }
        }
    }
    return out;
}

std::string test_harness_source(const ModuleAst& module, const std::string& filter,
                                bool capture_output) {
    std::ostringstream out;
    out << "#include \"dudu_runtime.hpp\"\n";
    const std::vector<ModuleAst>& units =
        module.module_units.empty() ? std::vector<ModuleAst>{module} : module.module_units;
    for (const ModuleAst& unit : units) {
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
    const std::vector<ModuleAst>& units =
        module.module_units.empty() ? std::vector<ModuleAst>{module} : module.module_units;
    std::vector<std::string> module_paths;
    module_paths.reserve(units.size());
    for (const ModuleAst& unit : units) {
        module_paths.push_back(unit.module_path);
    }
    return emit_cpp_module_artifacts(module, module_paths);
}

std::vector<CppModuleArtifact>
emit_cpp_module_artifacts(const ModuleAst& module, const std::vector<std::string>& module_paths) {
    std::vector<CppModuleArtifact> out;
    if (module_paths.empty()) {
        return out;
    }
    const std::set<std::string> filter = module_filter(module_paths);
    out.push_back({.path = "dudu_runtime.hpp",
                   .module_path = {},
                   .kind = CppModuleArtifactKind::Header,
                   .content = runtime_header(module)});
    if (module.module_units.empty()) {
        if (should_emit_module(filter, module)) {
            append_artifacts(out, module, {{module.module_path, &module}}, false,
                             preserve_public_abi_names(module));
        }
        return out;
    }
    const std::map<std::string, const ModuleAst*> modules = module_map(module.module_units);
    const bool public_abi = preserve_public_abi_names(module);
    for (const ModuleAst& unit : module.module_units) {
        if (should_emit_module(filter, unit)) {
            append_artifacts(out, unit, modules, false, public_abi);
        }
    }
    return out;
}

std::vector<CppModuleArtifact> emit_cpp_test_module_artifacts(const ModuleAst& module,
                                                              const std::string& filter,
                                                              bool capture_output) {
    const std::vector<ModuleAst>& units =
        module.module_units.empty() ? std::vector<ModuleAst>{module} : module.module_units;
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
    if (module.module_units.empty()) {
        if (should_emit_module(module_filter_set, module)) {
            append_artifacts(out, module, {{module.module_path, &module}}, true);
        }
    } else {
        const std::map<std::string, const ModuleAst*> modules = module_map(module.module_units);
        for (const ModuleAst& unit : module.module_units) {
            if (should_emit_module(module_filter_set, unit)) {
                append_artifacts(out, unit, modules, true);
            }
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
    const std::vector<ModuleAst>& units =
        module.module_units.empty() ? std::vector<ModuleAst>{module} : module.module_units;
    out.reserve(units.size());
    for (const ModuleAst& unit : units) {
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
    const std::vector<ModuleAst>& units =
        module.module_units.empty() ? std::vector<ModuleAst>{module} : module.module_units;
    out.reserve(1 + units.size() * 2);
    for (const ModuleAst& unit : units) {
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

void write_cpp_module_artifacts(const std::filesystem::path& dir, const ModuleAst& module) {
    write_cpp_artifacts(dir, emit_cpp_module_artifacts(module));
}

} // namespace dudu
