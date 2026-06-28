#include "dudu/native_header_parse.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_header_identity.hpp"
#include "dudu/native_header_scope.hpp"
#include "dudu/native_header_types.hpp"

#include <optional>
#include <regex>
#include <sstream>

namespace dudu {
namespace {

struct TemplateContext {
    int depth = 0;
    std::vector<std::string> params;
};

TypeRef normalize_native_type_ref(TypeRef type);

TypeRef parse_native_type_text(std::string text, const SourceLocation& location) {
    text = trim_copy(std::move(text));
    if (text.ends_with("...")) {
        return pack_expansion_type_ref(normalize_native_type_ref(parse_type_text(
                                           trim_copy(text.substr(0, text.size() - 3)), location)),
                                       location);
    }
    return normalize_native_type_ref(parse_type_text(text, location));
}

bool childless_native_wrapper(const TypeRef& type) {
    if ((type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
         type.kind == TypeKind::Atomic || type.kind == TypeKind::Storage ||
         type.kind == TypeKind::Shared || type.kind == TypeKind::Device ||
         type.kind == TypeKind::Static || type.kind == TypeKind::Pointer ||
         type.kind == TypeKind::Reference || type.kind == TypeKind::PackExpansion) &&
        type.children.empty()) {
        return true;
    }
    return false;
}

bool native_dot_marker_type(const TypeRef& type) {
    return type.kind == TypeKind::Named && type.name == ".";
}

TypeRef normalize_native_type_ref(TypeRef type) {
    if (childless_native_wrapper(type)) {
        return TypeRef{};
    }
    for (TypeRef& child : type.children) {
        child = normalize_native_type_ref(std::move(child));
    }
    if (type.kind == TypeKind::Template && type.children.size() >= 2) {
        std::string pack_name = type_ref_head_name(type.children.front());
        while (!pack_name.empty() && pack_name.back() == '.') {
            pack_name.pop_back();
        }
        bool marker_tail = !pack_name.empty();
        for (size_t i = 1; i < type.children.size(); ++i) {
            marker_tail = marker_tail && native_dot_marker_type(type.children[i]);
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
    static const std::regex expansion(R"((?:<|, )([^<>:]+):([0-9]+):([0-9]+)(?=[,>]))");
    std::string out;
    for (std::sregex_iterator it(line.begin(), line.end(), expansion), end; it != end; ++it) {
        const std::string file = (*it)[1].str();
        if (file != "line" && file != "col") {
            out = file;
        }
    }
    return out;
}

SourceLocation ast_source_location(const std::string& line, const SourceLocation& context_location,
                                   const std::string& current_file) {
    static const std::regex expansion(R"((?:<|, )([^<>:]+):([0-9]+):([0-9]+)(?=[,>]))");
    static const std::regex spelling(R"( col:([0-9]+))");
    std::smatch spelling_match;
    std::optional<std::smatch> chosen;
    for (std::sregex_iterator it(line.begin(), line.end(), expansion), end; it != end; ++it) {
        const std::string file = (*it)[1].str();
        if (file != "line" && file != "col") {
            chosen = *it;
        } else if (!chosen) {
            chosen = *it;
        }
    }
    if (!chosen) {
        return context_location;
    }
    const std::smatch& match = *chosen;
    const std::string file = match[1].str();
    if ((file == "line" || file == "col") && current_file.empty()) {
        return context_location;
    }
    SourceLocation out;
    if (file == "line" || file == "col") {
        out.file = SourceFileName(current_file);
    } else {
        out.file = SourceFileName(file);
    }
    out.line = std::stoi(match[2].str());
    out.column = std::stoi(match[3].str());
    if (std::regex_search(line, spelling_match, spelling)) {
        out.column = std::stoi(spelling_match[1].str());
    }
    return out;
}

void add_base_class(ClassDecl& klass, std::string base, const SourceLocation& location) {
    BaseClassDecl decl;
    decl.type_ref = parse_native_type_text(std::move(base), location);
    decl.location = location;
    klass.base_class_refs.push_back(std::move(decl));
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
    static const std::regex ns_decl(R"(NamespaceDecl.*\b([A-Za-z_][A-Za-z0-9_]*)(?: inline)?$)");
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
        if (line.ends_with(" inline")) {
            return;
        }
        const std::string name = match[1].str();
        if (starts_with(name, "__")) {
            return;
        }
        namespaces.push_back({depth, name});
        scan.namespaces.push_back({.name = name,
                                   .identity = native_identity(name, current_file),
                                   .location = decl_location});
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
                                  .identity = native_identity(name, current_file),
                                  .location = decl_location});
        }
    } else if ((line.find("RecordDecl") != std::string::npos ||
                line.find("CXXRecordDecl") != std::string::npos) &&
               std::regex_search(line, match, record_decl)) {
        const std::string raw_name = match[3].str();
        if (raw_name == "definition") {
            return;
        }
        const std::string name = class_name(scan, namespaces, classes, raw_name);
        if (!starts_with(raw_name, "__")) {
            scan.types.push_back({.name = name,
                                  .native_spelling = "",
                                  .type_ref = {},
                                  .identity = native_identity(name, current_file),
                                  .location = decl_location});
            if (line.find(" definition") != std::string::npos) {
                ClassDecl klass;
                klass.name = name;
                klass.identity = native_identity(name, current_file);
                klass.location = decl_location;
                scan.classes.push_back(std::move(klass));
                classes.push_back({depth, scan.classes.size() - 1});
            }
        }
    } else if (line.find("EnumDecl") != std::string::npos &&
               std::regex_search(line, match, enum_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            scan.types.push_back({.name = name,
                                  .native_spelling = "",
                                  .type_ref = {},
                                  .identity = native_identity(name, current_file),
                                  .location = decl_location});
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
        fn.identity = native_identity(name, current_file);
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
        method.native_identity = native_identity(
            scan.classes[classes.back().second].identity.canonical_path + "." + method.name,
            current_file);
        if (!templates.empty()) {
            method.generic_params = templates.back().params;
        }
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
        ctor.native_identity = native_identity(
            scan.classes[classes.back().second].identity.canonical_path + ".init", current_file);
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
                                   .identity = native_identity(name, current_file),
                                   .location = decl_location});
        }
    } else if (line.find("VarDecl") != std::string::npos &&
               line.find("ParmVarDecl") == std::string::npos &&
               std::regex_search(line, match, var_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__") && name != "dudu_probe") {
            const std::string type = dudu_type(match[2].str());
            scan.values.push_back({.name = name,
                                   .native_spelling = type,
                                   .type_ref = parse_native_type_text(type, decl_location),
                                   .identity = native_identity(name, current_file),
                                   .location = decl_location});
        }
    }
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

} // namespace dudu
