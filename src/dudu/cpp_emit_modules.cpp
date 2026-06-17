#include "dudu/cpp_emit_modules.hpp"

#include "dudu/cpp_emit.hpp"

#include <sstream>

namespace dudu {
namespace {

std::filesystem::path module_artifact_base(const ModuleAst& module) {
    std::filesystem::path out;
    const std::string path = module.module_path.empty() ? "main" : module.module_path;
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

std::string source_with_boundary_comment(const ModuleAst& unit) {
    std::ostringstream out;
    out << "// dudu module: " << (unit.module_path.empty() ? "main" : unit.module_path) << "\n";
    out << emit_cpp_source(unit);
    return out.str();
}

void append_artifacts(std::vector<CppModuleArtifact>& out, const ModuleAst& unit) {
    const std::filesystem::path base = module_artifact_base(unit);
    out.push_back({.path = base.string() + ".hpp",
                   .module_path = unit.module_path,
                   .kind = CppModuleArtifactKind::Header,
                   .content = emit_cpp_header(unit)});
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
