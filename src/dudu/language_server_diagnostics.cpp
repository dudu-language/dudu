#include "dudu/language_server_diagnostics.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_ast_lints.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string diagnostic_source(std::string_view message) {
    if (message.find("could not scan native header") != std::string_view::npos) {
        return "dudu/native-header";
    }
    if (message.find("return type mismatch") != std::string_view::npos ||
        message.find("cannot assign") != std::string_view::npos ||
        message.find("unknown identifier") != std::string_view::npos ||
        message.find("unknown type") != std::string_view::npos ||
        message.find("argument ") != std::string_view::npos) {
        return "dudu/sema";
    }
    if (message.find("unexpected") != std::string_view::npos ||
        message.find("expected newline") != std::string_view::npos ||
        message.find("expected indent") != std::string_view::npos ||
        message.find("expected identifier") != std::string_view::npos) {
        return "dudu/parser";
    }
    return "dudu/sema";
}

std::optional<std::string> missing_pkg_config_package(const ProjectConfig& config) {
    const char* pkg_config = std::getenv("PKG_CONFIG");
    const std::string executable = pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
    for (const std::string& package : config.pkg_config_packages) {
        const std::string command =
            shell_quote_arg(executable) + " --exists " + shell_quote_arg(package) + " 2>/dev/null";
        if (std::system(command.c_str()) != 0) {
            return package;
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<Diagnostic> diagnostics_for_document(const Document& doc) {
    try {
        ProjectConfig config;
        try {
            config = config_for_file(doc.path);
        } catch (const std::exception& error) {
            return {
                {{.file = doc.path, .line = 1, .column = 1}, error.what(), "dudu/build-config", 1}};
        }
        if (const std::optional<std::string> missing = missing_pkg_config_package(config)) {
            return {{{.file = doc.path, .line = 1, .column = 1},
                     "missing pkg-config package: " + *missing,
                     "dudu/build-config",
                     1}};
        }
        const bool project_tree =
            std::filesystem::exists(doc.path) && source_tree_files(doc.path).size() > 1;
        ModuleAst module =
            project_tree ? load_source_tree(doc.path) : parse_source(doc.text, doc.path);
        module.build_values = config.build_values;
        module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
        module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
        module.target_mode_explicit = config.target_mode_explicit;
        const NativeHeaderOptions native_options{.config = config,
                                                 .source_dir = doc.path.parent_path()};
        merge_native_header_types(module, native_options);
        for (ModuleAst& unit : module.module_units) {
            unit.build_values = module.build_values;
            unit.target_mode_explicit = module.target_mode_explicit;
            merge_native_header_types(unit, native_options);
        }
        if (project_tree || config.build_backend == "cmake") {
            analyze_module_tree(module, {.check_bodies = true});
        } else {
            analyze_module(module, {.check_bodies = true});
        }
        return ast_lint_diagnostics(module, doc);
    } catch (const CompileError& error) {
        return {{.location = error.location(),
                 .message = error.what(),
                 .source = diagnostic_source(error.what()),
                 .severity = 1}};
    } catch (const std::exception& error) {
        return {{{.file = doc.path, .line = 1, .column = 1}, error.what(), "dudu/lsp", 1}};
    }
}

std::string diagnostic_json(const Diagnostic& diagnostic) {
    const int line = std::max(0, diagnostic.location.line - 1);
    const int column = std::max(0, diagnostic.location.column - 1);
    std::ostringstream out;
    out << "{\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << column
        << "},\"end\":{\"line\":" << line << ",\"character\":" << (column + 1)
        << "}},\"severity\":" << diagnostic.severity << ",\"source\":\""
        << json_escape(diagnostic.source) << "\",\"message\":\"" << json_escape(diagnostic.message)
        << "\"}";
    return out.str();
}

} // namespace dudu
