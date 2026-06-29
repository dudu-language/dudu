#include "dudu/native/native_header_parse.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/native/native_header_identity.hpp"

#include <regex>
#include <sstream>

namespace dudu {
namespace {

struct MacroParams {
    int arity = 0;
    bool variadic = false;
};

MacroParams macro_params(std::string args) {
    args = trim_copy(std::move(args));
    if (args.empty()) {
        return {};
    }
    MacroParams out;
    for (std::string part : split_top_level_args(args)) {
        part = trim_copy(std::move(part));
        if (part == "..." || part.find("...") != std::string::npos || part == "__VA_ARGS__") {
            out.variadic = true;
        } else {
            ++out.arity;
        }
    }
    return out;
}

bool public_function_macro_name(const std::string& name) {
    return !starts_with(name, "__");
}

bool public_object_macro_name(const std::string& name) {
    return !starts_with(name, "_");
}

} // namespace

void parse_macro_dump(NativeHeaderScan& scan, const std::string& dump,
                      const SourceLocation& location) {
    static const std::regex function_macro(R"(^#define ([A-Za-z_][A-Za-z0-9_]*)\(([^)]*)\))");
    static const std::regex object_macro(R"(^#define ([A-Z_][A-Z0-9_]*)(\s|$))");
    std::istringstream in(dump);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch match;
        if (std::regex_search(line, match, function_macro)) {
            const std::string name = match[1].str();
            if (!public_function_macro_name(name)) {
                continue;
            }
            const MacroParams params = macro_params(match[2].str());
            scan.macros.push_back({.name = name,
                                   .arity = params.arity,
                                   .function_like = true,
                                   .identity = native_identity(name),
                                   .location = location});
            scan.functions.push_back(
                {.name = name,
                 .template_params = {},
                 .param_native_spellings =
                     std::vector<std::string>(static_cast<size_t>(params.arity), "auto"),
                 .param_type_refs = std::vector<TypeRef>(static_cast<size_t>(params.arity),
                                                         named_type_ref("auto", location)),
                 .return_native_spelling = "auto",
                 .return_type_ref = named_type_ref("auto", location),
                 .min_params = params.arity,
                 .variadic = params.variadic,
                 .identity = native_identity(name),
                 .location = location});
        } else if (std::regex_search(line, match, object_macro)) {
            const std::string name = match[1].str();
            if (!public_object_macro_name(name)) {
                continue;
            }
            scan.macros.push_back({.name = name,
                                   .function_like = false,
                                   .identity = native_identity(name),
                                   .location = location});
            scan.values.push_back({.name = name,
                                   .native_spelling = "auto",
                                   .type_ref = named_type_ref("auto", location),
                                   .identity = native_identity(name),
                                   .location = location});
        }
    }
}

} // namespace dudu
