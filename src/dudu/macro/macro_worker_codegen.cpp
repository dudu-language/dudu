#include "dudu/macro/macro_worker_codegen.hpp"

#include "dudu/core/source.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace dudu::macro {
namespace {

std::string cpp_string_literal(std::string_view value) {
    std::string out = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string module_header(std::string module_path) {
    std::replace(module_path.begin(), module_path.end(), '.', '/');
    return module_path + ".hpp";
}

std::string protocol_kind(TargetKind kind) {
    switch (kind) {
    case TargetKind::Any:
        return "DeclarationKind::Unknown";
    case TargetKind::Class:
        return "DeclarationKind::Class";
    case TargetKind::Enum:
        return "DeclarationKind::Enum";
    case TargetKind::Function:
        return "DeclarationKind::Function";
    case TargetKind::Field:
        return "DeclarationKind::Field";
    case TargetKind::Constant:
        return "DeclarationKind::Constant";
    }
    throw std::logic_error("unknown macro target kind");
}

std::string declaration_field(TargetKind kind) {
    switch (kind) {
    case TargetKind::Class:
        return "class_decl";
    case TargetKind::Enum:
        return "enum_decl";
    case TargetKind::Function:
        return "function_decl";
    case TargetKind::Field:
        return "field_decl";
    case TargetKind::Constant:
        return "constant_decl";
    case TargetKind::Any:
        return {};
    }
    throw std::logic_error("unknown macro target kind");
}

std::string capability_kind(std::string_view name) {
    if (name == "fs.read")
        return "CapabilityKind::FsRead";
    if (name == "fs.write")
        return "CapabilityKind::FsWrite";
    if (name == "env.read")
        return "CapabilityKind::EnvRead";
    if (name == "process")
        return "CapabilityKind::Process";
    if (name == "network")
        return "CapabilityKind::Network";
    if (name == "clock")
        return "CapabilityKind::Clock";
    if (name == "random")
        return "CapabilityKind::Random";
    throw std::invalid_argument("unknown macro capability: " + std::string(name));
}

void emit_capabilities(std::ostringstream& out, const std::vector<std::string>& values) {
    std::map<std::string, std::vector<std::string>> grouped;
    for (const std::string& value : values) {
        const std::size_t separator = value.find('=');
        const std::string name = value.substr(0, separator);
        std::vector<std::string>& scopes = grouped[name];
        if (separator != std::string::npos)
            scopes.push_back(value.substr(separator + 1));
    }
    for (const auto& [name, scopes] : grouped) {
        out << "    catalog.capabilities.push_back({.kind = " << capability_kind(name)
            << ", .values = {";
        for (std::size_t index = 0; index < scopes.size(); ++index) {
            if (index != 0)
                out << ", ";
            out << cpp_string_literal(scopes[index]);
        }
        out << "}});\n";
    }
}

void emit_catalog_entry(std::ostringstream& out, const Definition& definition) {
    out << "    catalog.macros.push_back({.name = " << cpp_string_literal(definition.name)
        << ", .entry_point = " << cpp_string_literal(definition.identity)
        << ", .accepted_kind = DeclarationKind::";
    const std::string kind = protocol_kind(definition.accepted_kind);
    out << kind.substr(kind.find("::") + 2) << "});\n";
}

void emit_dispatch_entry(std::ostringstream& out, const Definition& definition, bool cacheable) {
    if (definition.function == nullptr || definition.function->cpp_name.empty()) {
        throw CompileError(definition.location,
                           "macro definition is missing its generated worker entry point");
    }
    out << "    if (request.macro_name == " << cpp_string_literal(definition.identity) << ") {\n";
    if (definition.accepted_kind == TargetKind::Any) {
        out << "        auto input = sdk_bridge::from_protocol(request.declaration);\n";
    } else {
        const std::string field = declaration_field(definition.accepted_kind);
        out << "        if (!request.declaration." << field << ".has_value()) {\n"
            << "            throw std::runtime_error("
            << cpp_string_literal("macro " + definition.identity +
                                  " received the wrong declaration kind")
            << ");\n"
            << "        }\n"
            << "        auto input = sdk_bridge::from_protocol(*request.declaration." << field
            << ");\n";
    }
    out << "        auto expansion = " << definition.function->cpp_name << "(std::move(input));\n"
        << "        return ExpansionResponse{.expansion = sdk_bridge::to_protocol(expansion), "
           ".cacheable = "
        << (cacheable ? "true" : "false") << ", .external_input_hashes = {}};\n"
        << "    }\n";
}

} // namespace

std::string generate_worker_source(const Plan& plan, const WorkerSourceOptions& options) {
    if (plan.definitions.empty()) {
        throw std::invalid_argument("cannot generate a worker without macro definitions");
    }
    std::set<std::string> headers;
    for (const auto& [_, definition] : plan.definitions) {
        headers.insert(module_header(definition.module_path));
    }

    std::ostringstream out;
    out << "// Generated Dudu macro worker.\n"
        << "#include \"dudu/macro/macro_sdk_bridge_generated.hpp\"\n"
        << "#include \"dudu/macro/macro_worker_runtime.hpp\"\n";
    for (const std::string& header : headers) {
        out << "#include " << cpp_string_literal(header) << "\n";
    }
    out << "\n#include <stdexcept>\n#include <utility>\n\n"
        << "using namespace dudu::macro;\n"
        << "using namespace dudu::macro::protocol;\n\n"
        << "int main() {\n"
        << "    MacroCatalog catalog{.package = " << cpp_string_literal(options.package)
        << ", .binary_identity = " << cpp_string_literal(options.binary_identity) << "};\n";
    for (const auto& [_, definition] : plan.definitions) {
        emit_catalog_entry(out, definition);
    }
    emit_capabilities(out, options.capabilities);
    out << "    return serve_worker(catalog, " << cpp_string_literal(options.project_root)
        << ", [](const ExpansionRequest& request) "
           "-> ExpansionResponse {\n";
    for (const auto& [_, definition] : plan.definitions) {
        emit_dispatch_entry(out, definition,
                            !options.non_cacheable_macros.contains(definition.identity));
    }
    out << "        throw std::runtime_error(\"unknown macro entry point: \" + "
           "request.macro_name);\n"
        << "    });\n"
        << "}\n";
    return out.str();
}

} // namespace dudu::macro
