#include "dudu/cpp_emit_prelude.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string include_path(const ImportDecl& import) {
    if (import.module_path.size() >= 2 && import.module_path.front() == '"' &&
        import.module_path.back() == '"') {
        return import.module_path;
    }
    return '"' + import.module_path + '"';
}

bool has_function(const ModuleAst& module, std::string_view name) {
    for (const FunctionDecl& fn : module.functions) {
        if (fn.name == name) {
            return true;
        }
    }
    return false;
}

std::string build_literal(const std::string& value) {
    if (value == "true" || value == "false") {
        return value;
    }
    if (!value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
        return value;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value;
    }
    return '"' + value + '"';
}

bool freestanding_like(const ModuleAst& module) {
    const auto found = module.build_values.find("TARGET_MODE");
    if (found == module.build_values.end()) {
        return false;
    }
    const std::string mode = build_literal(found->second);
    return mode == "\"freestanding\"" || mode == "\"embedded\"";
}

std::string build_type(const std::string& literal, bool freestanding) {
    if (literal == "true" || literal == "false") {
        return "bool";
    }
    if (!literal.empty() && literal.front() == '"') {
        return freestanding ? "const char*" : "std::string_view";
    }
    return "int";
}

void emit_build_namespace(std::ostringstream& out, const ModuleAst& module) {
    std::map<std::string, std::string> values = {{"DEBUG", "false"},
                                                 {"RENDER_BACKEND", "\"vulkan\""}};
    for (const auto& [name, value] : module.build_values) {
        values[name] = value;
    }

    out << "namespace build {\n";
    const bool freestanding = freestanding_like(module);
    for (const auto& [name, value] : values) {
        const std::string literal = build_literal(value);
        out << "inline constexpr " << build_type(literal, freestanding) << ' ' << name << " = "
            << literal << ";\n";
    }
    out << "} // namespace build\n\n";
}

} // namespace

std::vector<std::string> namespace_aliases(const ModuleAst& module) {
    std::vector<std::string> aliases;
    for (const ImportDecl& import : module.imports) {
        if (import.kind == ImportKind::ForeignCpp && !import.alias.empty()) {
            aliases.push_back(import.alias);
        } else if (import.kind == ImportKind::ForeignC && !import.alias.empty()) {
            aliases.push_back("!" + import.alias);
        }
    }
    return aliases;
}

void emit_includes(std::ostringstream& out, const ModuleAst& module) {
    if (freestanding_like(module)) {
        out << "#include <algorithm>\n"
               "#include <array>\n"
               "#include <atomic>\n"
               "#include <cstddef>\n"
               "#include <cstdint>\n"
               "#include <cstdlib>\n"
               "#include <type_traits>\n"
               "#include <utility>\n";
    } else {
        out << "#include <algorithm>\n"
               "#include <array>\n"
               "#include <atomic>\n"
               "#include <cstddef>\n"
               "#include <cstdint>\n"
               "#include <cstdlib>\n"
               "#include <functional>\n"
               "#include <iostream>\n"
               "#include <optional>\n"
               "#include <string>\n"
               "#include <string_view>\n"
               "#include <type_traits>\n"
               "#include <unordered_map>\n"
               "#include <unordered_set>\n"
               "#include <utility>\n"
               "#include <variant>\n"
               "#include <vector>\n";
    }

    for (const ImportDecl& import : module.imports) {
        if (import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp) {
            out << "#include " << include_path(import) << '\n';
        }
    }
    out << "#ifndef DUDU_CUDA_GLOBAL\n"
           "#define DUDU_CUDA_GLOBAL\n"
           "#endif\n"
           "#ifndef DUDU_CUDA_DEVICE\n"
           "#define DUDU_CUDA_DEVICE\n"
           "#endif\n"
           "#ifndef DUDU_CUDA_HOST\n"
           "#define DUDU_CUDA_HOST\n"
           "#endif\n"
           "#ifndef DUDU_SHADER_COMPUTE\n"
           "#define DUDU_SHADER_COMPUTE\n"
           "#endif\n"
           "#ifndef DUDU_WORKGROUP_SIZE\n"
           "#define DUDU_WORKGROUP_SIZE(x, y, z)\n"
           "#endif\n";
    out << '\n';
}

void emit_result_prelude(std::ostringstream& out, const ModuleAst& module) {
    const bool freestanding = freestanding_like(module);
    out << "namespace dudu {\n"
           "template <typename T> struct OkValue { T value; };\n"
           "template <typename E> struct ErrValue { E err; };\n"
           "template <typename T> OkValue<T> Ok(T value) { return {std::move(value)}; }\n"
           "template <typename E> ErrValue<E> Err(E err) { return {std::move(err)}; }\n";
    if (!freestanding) {
        out << "template <typename T> void print(const T& value) { std::cout << value << '\\n'; "
               "}\n";
    }
    if (!has_function(module, "align_up")) {
        out << "constexpr size_t align_up(size_t value, size_t alignment) {\n"
               "    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * "
               "alignment;\n"
               "}\n";
    }
    out << "template <typename T, typename E> struct Result {\n"
           "    bool ok{};\n"
           "    T value{};\n"
           "    E err{};\n"
           "    Result() = default;\n"
           "    Result(OkValue<T> ok_value) : ok(true), value(std::move(ok_value.value)), err{} "
           "{}\n"
           "    Result(ErrValue<E> err_value) : ok(false), value{}, err(std::move(err_value.err)) "
           "{}\n"
           "};\n"
           "template <typename T0> struct Tuple1 { T0 _0{}; };\n"
           "template <typename T0, typename T1> struct Tuple2 { T0 _0{}; T1 _1{}; };\n"
           "template <typename T0, typename T1, typename T2> struct Tuple3 { T0 _0{}; T1 _1{}; "
           "T2 _2{}; };\n"
           "template <typename T0, typename T1, typename T2, typename T3> struct Tuple4 { T0 _0{}; "
           "T1 _1{}; T2 _2{}; T3 _3{}; };\n"
           "template <typename T0, typename T1, typename T2, typename T3, typename T4> struct "
           "Tuple5 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; T4 _4{}; };\n"
           "template <typename T0, typename T1, typename T2, typename T3, typename T4, typename "
           "T5> struct Tuple6 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; T4 _4{}; T5 _5{}; };\n"
           "template <typename T0, typename T1, typename T2, typename T3, typename T4, typename "
           "T5, typename T6> struct Tuple7 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; T4 _4{}; T5 "
           "_5{}; T6 _6{}; };\n"
           "template <typename T0, typename T1, typename T2, typename T3, typename T4, typename "
           "T5, typename T6, typename T7> struct Tuple8 { T0 _0{}; T1 _1{}; T2 _2{}; T3 _3{}; "
           "T4 _4{}; T5 _5{}; T6 _6{}; T7 _7{}; };\n"
           "} // namespace dudu\n";
    if (!has_function(module, "align_up")) {
        out << "using dudu::align_up;\n";
    }
    if (!freestanding) {
        out << "using dudu::print;\n";
    }
    out << "using std::max;\n"
           "using std::min;\n"
           "namespace shader {\n"
           "struct GlobalId { int32_t x{}; int32_t y{}; int32_t z{}; };\n"
           "inline GlobalId global_id{};\n"
           "} // namespace shader\n";
    emit_build_namespace(out, module);
}

} // namespace dudu
