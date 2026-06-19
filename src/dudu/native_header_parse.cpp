#include "dudu/native_header_parse.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_header_merge.hpp"
#include "dudu/native_header_scope.hpp"
#include "dudu/native_header_types.hpp"

#include <cctype>
#include <regex>
#include <set>
#include <sstream>

namespace dudu {
namespace {

struct TemplateContext {
    int depth = 0;
    std::vector<std::string> params;
};

TypeRef parse_native_type_text(std::string text, const SourceLocation& location) {
    text = trim_copy(std::move(text));
    if (text.ends_with("...")) {
        return pack_expansion_type_ref(
            parse_type_text(trim_copy(text.substr(0, text.size() - 3)), location), location);
    }
    TypeRef type = parse_type_text(text, location);
    if ((type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
         type.kind == TypeKind::Atomic || type.kind == TypeKind::Storage ||
         type.kind == TypeKind::Shared || type.kind == TypeKind::Device ||
         type.kind == TypeKind::Static || type.kind == TypeKind::Pointer ||
         type.kind == TypeKind::Reference || type.kind == TypeKind::PackExpansion) &&
        type.children.empty()) {
        return TypeRef{};
    }
    for (TypeRef& child : type.children) {
        child = parse_native_type_text(type_ref_text(child), child.location);
    }
    if (type.kind == TypeKind::Template && type.children.size() >= 2) {
        std::string pack_name = type_ref_head_name(type.children.front());
        while (!pack_name.empty() && pack_name.back() == '.') {
            pack_name.pop_back();
        }
        bool marker_tail = !pack_name.empty();
        for (size_t i = 1; i < type.children.size(); ++i) {
            marker_tail = marker_tail && type_ref_text(type.children[i]) == ".";
        }
        if (marker_tail) {
            TypeRef pack_child = named_type_ref(pack_name, type.children.front().location);
            type.children = {pack_expansion_type_ref(std::move(pack_child), type.location)};
        }
    }
    return type;
}

int ast_depth(const std::string& line) {
    const size_t branch = line.find("|-");
    const size_t last = line.find("`-");
    if (branch == std::string::npos)
        return last == std::string::npos ? 0 : static_cast<int>(last / 2);
    if (last == std::string::npos)
        return static_cast<int>(branch / 2);
    return static_cast<int>((branch < last ? branch : last) / 2);
}

std::string ast_concrete_source_file(const std::string& line) {
    static const std::regex expansion(R"( <([^<>:]+):([0-9]+):([0-9]+)[,>])");
    std::smatch match;
    if (!std::regex_search(line, match, expansion)) {
        return {};
    }
    const std::string file = match[1].str();
    return file == "line" || file == "col" ? std::string{} : file;
}

SourceLocation ast_source_location(const std::string& line, const SourceLocation& fallback,
                                   const std::string& current_file) {
    static const std::regex expansion(R"( <([^<>:]+):([0-9]+):([0-9]+)[,>])");
    static const std::regex spelling(R"( col:([0-9]+))");
    std::smatch match;
    if (!std::regex_search(line, match, expansion)) {
        return fallback;
    }
    const std::string file = match[1].str();
    if ((file == "line" || file == "col") && current_file.empty()) {
        return fallback;
    }
    SourceLocation out;
    out.file = file == "line" || file == "col" ? current_file : file;
    out.line = std::stoi(match[2].str());
    out.column = std::stoi(match[3].str());
    if (std::regex_search(line, match, spelling)) {
        out.column = std::stoi(match[1].str());
    }
    return out;
}

template <typename T> void add_unique(std::vector<T>& out, std::set<std::string>& seen, T value) {
    if (seen.insert(value.name).second) {
        out.push_back(std::move(value));
    }
}

void add_unique_function(std::vector<NativeFunctionDecl>& out, std::set<std::string>& seen,
                         NativeFunctionDecl value) {
    if (seen.insert(native_function_key(value)).second)
        out.push_back(std::move(value));
}

std::string method_key(const FunctionDecl& fn) {
    std::string key = fn.name + "(";
    for (const ParamDecl& param : fn.params)
        key += type_ref_text(param.type_ref) + ",";
    return key + ")->" + type_ref_text(function_return_type_ref(fn));
}

void add_base_class(ClassDecl& klass, std::string base, const SourceLocation& location) {
    BaseClassDecl decl;
    decl.type_ref = parse_native_type_text(std::move(base), location);
    decl.location = location;
    klass.base_class_refs.push_back(std::move(decl));
}

void merge_class(ClassDecl& target, const ClassDecl& source) {
    std::set<std::string> bases;
    for (const BaseClassDecl& base : target.base_class_refs) {
        bases.insert(type_ref_text(base.type_ref));
    }
    for (const BaseClassDecl& base : source.base_class_refs) {
        if (bases.insert(type_ref_text(base.type_ref)).second) {
            target.base_class_refs.push_back(base);
        }
    }
    std::set<std::string> fields;
    for (const FieldDecl& field : target.fields)
        fields.insert(field.name);
    for (const FieldDecl& field : source.fields)
        if (fields.insert(field.name).second)
            target.fields.push_back(field);
    std::set<std::string> methods;
    for (const FunctionDecl& method : target.methods)
        methods.insert(method_key(method));
    for (const FunctionDecl& method : source.methods)
        if (methods.insert(method_key(method)).second)
            target.methods.push_back(method);
}

void add_unique_class(std::vector<ClassDecl>& out, std::set<std::string>& seen, ClassDecl value) {
    if (seen.insert(value.name).second) {
        out.push_back(std::move(value));
        return;
    }
    for (ClassDecl& klass : out)
        if (klass.name == value.name)
            merge_class(klass, value);
}

void parse_ast_line(NativeHeaderScan& scan, const std::string& line,
                    std::vector<std::pair<int, std::string>>& namespaces,
                    std::vector<std::pair<int, size_t>>& classes,
                    std::vector<std::pair<int, std::string>>& enums,
                    std::vector<TemplateContext>& templates,
                    std::vector<std::pair<int, size_t>>& functions, const SourceLocation& location,
                    std::string& current_file) {
    const SourceLocation decl_location = ast_source_location(line, location, current_file);
    if (const std::string concrete_file = ast_concrete_source_file(line); !concrete_file.empty()) {
        current_file = concrete_file;
    }
    static const std::regex typedef_decl(
        R"((TypedefDecl|TypeAliasDecl).*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex record_decl(
        R"((RecordDecl|CXXRecordDecl).*\b(struct|class|union) ([A-Za-z_][A-Za-z0-9_]*)\b)");
    static const std::regex enum_decl(R"(EnumDecl.*\b([A-Za-z_][A-Za-z0-9_]*)\b)");
    static const std::regex template_type_param(
        R"(TemplateTypeParmDecl.*\bindex [0-9]+ (?:\.\.\. )?([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex ns_decl(R"(NamespaceDecl.*\b([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex fn_decl(
        R"(FunctionDecl.*\b((?:operator[^\s']+)|[A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex method_decl(R"(CXXMethodDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex ctor_decl(
        R"(CXXConstructorDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex field_decl(R"(FieldDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex base_decl(R"(\b(public|protected|private) '([^']+)')");
    static const std::regex enum_value_decl(
        R"(EnumConstantDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex var_decl(R"(VarDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    const int depth = ast_depth(line);
    while (!namespaces.empty() && namespaces.back().first >= depth) {
        namespaces.pop_back();
    }
    while (!classes.empty() && classes.back().first >= depth) {
        classes.pop_back();
    }
    while (!enums.empty() && enums.back().first >= depth) {
        enums.pop_back();
    }
    while (!templates.empty() && templates.back().depth >= depth) {
        templates.pop_back();
    }
    while (!functions.empty() && functions.back().first >= depth) {
        functions.pop_back();
    }
    std::smatch match;
    if (line.find("FunctionTemplateDecl") != std::string::npos) {
        templates.push_back({.depth = depth, .params = {}});
    }
    if (!templates.empty() && line.find("TemplateTypeParmDecl") != std::string::npos &&
        std::regex_search(line, match, template_type_param)) {
        templates.back().params.push_back(match[1].str());
    }
    if (!functions.empty() && line.find("ParmVarDecl") != std::string::npos &&
        line.find(" cinit") != std::string::npos) {
        NativeFunctionDecl& fn = scan.functions[functions.back().second];
        if (fn.min_params > 0) {
            --fn.min_params;
        }
    }
    if (line.find("NamespaceDecl") != std::string::npos &&
        std::regex_search(line, match, ns_decl)) {
        const std::string name = match[1].str();
        if (starts_with(name, "__")) {
            return;
        }
        namespaces.push_back({depth, name});
        scan.namespaces.push_back({.name = name, .location = decl_location});
    } else if ((line.find("TypedefDecl") != std::string::npos ||
                line.find("TypeAliasDecl") != std::string::npos) &&
               std::regex_search(line, match, typedef_decl)) {
        const std::string name = match[2].str();
        const std::string lowered_type = dudu_type(match[3].str());
        const bool useful_alias = lowered_type != name;
        if (!starts_with(name, "__") || useful_alias) {
            const TypeRef type_ref =
                useful_alias ? parse_native_type_text(lowered_type, decl_location) : TypeRef{};
            scan.types.push_back({.name = name,
                                  .native_spelling = useful_alias ? lowered_type : "",
                                  .type_ref = type_ref,
                                  .location = decl_location});
        }
    } else if ((line.find("RecordDecl") != std::string::npos ||
                line.find("CXXRecordDecl") != std::string::npos) &&
               std::regex_search(line, match, record_decl)) {
        const std::string raw_name = match[3].str();
        const std::string name = class_name(scan, namespaces, classes, raw_name);
        if (!starts_with(raw_name, "__")) {
            scan.types.push_back(
                {.name = name, .native_spelling = "", .type_ref = {}, .location = decl_location});
            if (line.find(" definition") != std::string::npos) {
                ClassDecl klass;
                klass.name = name;
                klass.location = decl_location;
                scan.classes.push_back(std::move(klass));
                classes.push_back({depth, scan.classes.size() - 1});
            }
        }
    } else if (line.find("EnumDecl") != std::string::npos &&
               std::regex_search(line, match, enum_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            scan.types.push_back(
                {.name = name, .native_spelling = "", .type_ref = {}, .location = decl_location});
            enums.push_back({depth, name});
        }
    } else if (line.find("FunctionDecl") != std::string::npos &&
               std::regex_search(line, match, fn_decl)) {
        const std::string name = join_scope(namespaces, match[1].str());
        if (starts_with(name, "__")) {
            return;
        }
        const std::string signature = match[2].str();
        NativeFunctionDecl fn;
        fn.name = name;
        if (!templates.empty()) {
            fn.template_params = templates.back().params;
        }
        fn.param_native_spellings =
            qualify_scoped_types(scan, namespaces, signature_params(signature));
        fn.return_native_spelling =
            qualify_scoped_type(scan, namespaces, signature_return_type(signature));
        fn.param_type_refs.reserve(fn.param_native_spellings.size());
        for (const std::string& param : fn.param_native_spellings) {
            fn.param_type_refs.push_back(parse_native_type_text(param, decl_location));
        }
        fn.return_type_ref = parse_native_type_text(fn.return_native_spelling, decl_location);
        fn.min_params = static_cast<int>(fn.param_native_spellings.size());
        fn.variadic = signature.find("...") != std::string::npos;
        fn.location = decl_location;
        scan.functions.push_back(std::move(fn));
        functions.push_back({depth, scan.functions.size() - 1});
    } else if (!classes.empty() && line.find("CXXMethodDecl") != std::string::npos &&
               std::regex_search(line, match, method_decl)) {
        FunctionDecl method;
        method.name = match[1].str();
        method.return_type_ref = parse_native_type_text(
            qualify_scoped_type(scan, namespaces, signature_return_type(match[2].str())),
            decl_location);
        for (const std::string& param : signature_params(match[2].str())) {
            ParamDecl decl;
            decl.name = "arg" + std::to_string(method.params.size());
            decl.type_ref =
                parse_native_type_text(qualify_scoped_type(scan, namespaces, param), decl_location);
            decl.location = decl_location;
            method.params.push_back(std::move(decl));
        }
        method.location = decl_location;
        scan.classes[classes.back().second].methods.push_back(std::move(method));
    } else if (!classes.empty() && line.find("CXXConstructorDecl") != std::string::npos &&
               std::regex_search(line, match, ctor_decl)) {
        const std::vector<std::string> params =
            qualify_scoped_types(scan, namespaces, signature_params(match[2].str()));
        FunctionDecl ctor;
        ctor.name = "init";
        for (const std::string& param : params) {
            ParamDecl decl;
            decl.name = "arg" + std::to_string(ctor.params.size());
            decl.type_ref = parse_native_type_text(param, decl_location);
            decl.location = decl_location;
            ctor.params.push_back(std::move(decl));
        }
        ctor.location = decl_location;
        scan.classes[classes.back().second].methods.push_back(std::move(ctor));
    } else if (!classes.empty() && line.find("FieldDecl") != std::string::npos &&
               std::regex_search(line, match, field_decl)) {
        const std::string type = qualify_scoped_type(scan, namespaces, dudu_type(match[2].str()));
        scan.classes[classes.back().second].fields.push_back(
            {.name = match[1].str(),
             .type_ref = parse_native_type_text(type, decl_location),
             .value_expr = {},
             .location = decl_location});
    } else if (!classes.empty() &&
               (line.find("public '") != std::string::npos ||
                line.find("protected '") != std::string::npos ||
                line.find("private '") != std::string::npos) &&
               std::regex_search(line, match, base_decl)) {
        add_base_class(scan.classes[classes.back().second],
                       qualify_scoped_type(scan, namespaces, dudu_type(match[2].str())),
                       decl_location);
    } else if (line.find("EnumConstantDecl") != std::string::npos &&
               std::regex_search(line, match, enum_value_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            const std::string type = match.size() > 2
                                         ? dudu_type(match[2].str())
                                         : (enums.empty() ? "i32" : enums.back().second);
            scan.values.push_back({.name = name,
                                   .native_spelling = type,
                                   .type_ref = parse_native_type_text(type, decl_location),
                                   .enum_constant = true,
                                   .location = decl_location});
        }
    } else if (line.find("VarDecl") != std::string::npos &&
               line.find("ParmVarDecl") == std::string::npos &&
               std::regex_search(line, match, var_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            const std::string type = dudu_type(match[2].str());
            scan.values.push_back({.name = name,
                                   .native_spelling = type,
                                   .type_ref = parse_native_type_text(type, decl_location),
                                   .location = decl_location});
        }
    }
}

struct MacroParams {
    int arity = 0;
    bool variadic = false;
};

MacroParams macro_params(std::string args) {
    args = trim_copy(std::move(args));
    if (args.empty())
        return {};
    MacroParams out;
    for (std::string part : split_top_level_args(args)) {
        part = trim_copy(std::move(part));
        if (part == "..." || part.find("...") != std::string::npos || part == "__VA_ARGS__")
            out.variadic = true;
        else
            ++out.arity;
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

void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump,
                    const SourceLocation& location) {
    std::vector<std::pair<int, std::string>> namespaces;
    std::vector<std::pair<int, size_t>> classes;
    std::vector<std::pair<int, std::string>> enums;
    std::vector<TemplateContext> templates;
    std::vector<std::pair<int, size_t>> functions;
    std::string current_file;
    std::istringstream in(dump);
    std::string line;
    while (std::getline(in, line)) {
        parse_ast_line(scan, line, namespaces, classes, enums, templates, functions, location,
                       current_file);
    }
}

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
            scan.macros.push_back(
                {.name = name, .arity = params.arity, .function_like = true, .location = location});
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
                 .location = location});
        } else if (std::regex_search(line, match, object_macro)) {
            const std::string name = match[1].str();
            if (!public_object_macro_name(name)) {
                continue;
            }
            scan.macros.push_back({.name = name, .function_like = false, .location = location});
            scan.values.push_back({.name = name,
                                   .native_spelling = "auto",
                                   .type_ref = named_type_ref("auto", location),
                                   .location = location});
        }
    }
}

NativeHeaderScan dedupe_scan(NativeHeaderScan scan) {
    NativeHeaderScan out;
    std::set<std::string> types;
    std::set<std::string> values;
    std::set<std::string> functions;
    std::set<std::string> macros;
    std::set<std::string> namespaces;
    std::set<std::string> classes;
    for (auto item : scan.types)
        add_unique(out.types, types, std::move(item));
    for (auto item : scan.values)
        add_unique(out.values, values, std::move(item));
    for (auto item : scan.functions)
        add_unique_function(out.functions, functions, std::move(item));
    for (auto item : scan.macros)
        add_unique(out.macros, macros, std::move(item));
    for (auto item : scan.namespaces)
        add_unique(out.namespaces, namespaces, std::move(item));
    for (auto item : scan.classes)
        add_unique_class(out.classes, classes, std::move(item));
    return out;
}

} // namespace dudu
