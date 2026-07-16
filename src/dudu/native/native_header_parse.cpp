#include "dudu/native/native_header_parse.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_header_ast_parse_internal.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"

#include <cctype>
#include <optional>
#include <regex>
#include <sstream>

namespace dudu {
namespace native_ast_parse {

bool dudu_primitive_name(std::string_view name) {
    for (std::string_view primitive : {"bool", "char", "f32", "f64", "i8", "i16", "i32", "i64",
                                       "isize", "u8", "u16", "u32", "u64", "usize", "void"}) {
        if (name == primitive) {
            return true;
        }
    }
    return false;
}

NativeSymbolId scanned_identity(const NativeCursorIdentityIndex& identities, NativeCursorKind kind,
                                std::string_view spelling, const SourceLocation& location,
                                std::string canonical_path, const std::string& current_file) {
    NativeSymbolId identity = native_identity(canonical_path, current_file);
    std::optional<std::string> usr = identities.find(kind, spelling, location);
    if (!usr && (kind == NativeCursorKind::Type || kind == NativeCursorKind::Class ||
                 kind == NativeCursorKind::Namespace)) {
        usr = identities.find_semantic(kind, canonical_path);
    }
    if (usr) {
        identity.usr = *usr;
    }
    return identity;
}

std::optional<TypeLayout> scanned_layout(const NativeCursorIdentityIndex& identities,
                                         NativeCursorKind kind, std::string_view spelling,
                                         const SourceLocation& location) {
    return identities.find_layout(kind, spelling, location);
}

TypeRef normalize_native_type_ref(TypeRef type);

bool relevant_ast_line(std::string_view line) {
    return line.find("Decl") != std::string_view::npos ||
           line.find("TemplateArgument") != std::string_view::npos ||
           line.find("IntegerLiteral") != std::string_view::npos ||
           line.find("CXXBoolLiteralExpr") != std::string_view::npos ||
           line.find("TextComment") != std::string_view::npos ||
           line.find("public '") != std::string_view::npos ||
           line.find("protected '") != std::string_view::npos ||
           line.find("private '") != std::string_view::npos;
}

std::string native_template_value(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

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

void replace_native_type_placeholder(TypeRef& type, std::string_view placeholder,
                                     std::string_view replacement) {
    if ((type.kind == TypeKind::Named || type.kind == TypeKind::Qualified) &&
        type.name == placeholder) {
        type.name = std::string(replacement);
    }
    for (TypeRef& child : type.children) {
        replace_native_type_placeholder(child, placeholder, replacement);
    }
}

std::string native_type_placeholder(std::string_view index) {
    return "__dudu_native_type_parameter_" + std::string(index);
}

std::string preserve_native_type_placeholders(std::string text) {
    static const std::regex placeholder(R"(type-parameter-[0-9]+-([0-9]+))");
    std::string out;
    std::smatch match;
    while (std::regex_search(text, match, placeholder)) {
        out += match.prefix().str();
        out += native_type_placeholder(match[1].str());
        text = match.suffix().str();
    }
    out += text;
    return out;
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
    static const std::regex named_line_spelling(R"(>\s+line:([0-9]+):([0-9]+)\s+)");
    static const std::regex named_column_spelling(R"(>\s+col:([0-9]+)\s+)");
    std::smatch named_line_match;
    std::smatch named_column_match;
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
    const bool relative_location = file == "line" || file == "col";
    if (relative_location && current_file.empty()) {
        return context_location;
    }
    SourceLocation out;
    if (relative_location) {
        out.file = SourceFileName(current_file);
    } else {
        out.file = SourceFileName(file);
    }
    out.line = std::stoi(match[2].str());
    out.column = std::stoi(match[3].str());
    if (std::regex_search(line, named_line_match, named_line_spelling)) {
        if (relative_location && !current_file.empty()) {
            out.file = SourceFileName(current_file);
        }
        out.line = std::stoi(named_line_match[1].str());
        out.column = std::stoi(named_line_match[2].str());
    } else if (std::regex_search(line, named_column_match, named_column_spelling)) {
        out.column = std::stoi(named_column_match[1].str());
    }
    return out;
}

void add_base_class(ClassDecl& klass, std::string base, const SourceLocation& location) {
    BaseClassDecl decl;
    decl.type_ref = parse_native_type_text(std::move(base), location);
    decl.location = location;
    klass.base_class_refs.push_back(std::move(decl));
}

bool native_param_name_token(std::string_view token) {
    if (token.empty() ||
        (!std::isalpha(static_cast<unsigned char>(token.front())) && token.front() != '_')) {
        return false;
    }
    for (const char ch : token) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
            return false;
        }
    }
    return token != "used" && token != "referenced" && token != "invalid" && token != "cinit" &&
           token != "implicit";
}

std::optional<std::string> parm_var_decl_name(const std::string& line) {
    const size_t type_quote = line.find(" '");
    if (type_quote == std::string::npos) {
        return std::nullopt;
    }
    std::istringstream words(trim_copy(line.substr(0, type_quote)));
    std::vector<std::string> tokens;
    std::string token;
    while (words >> token) {
        tokens.push_back(std::move(token));
    }
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (native_param_name_token(*it)) {
            return *it;
        }
        if (it->starts_with("col:") || it->starts_with("line:")) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void apply_param_name(NativeHeaderScan& scan, ParamTarget& target, std::string name) {
    if (name.empty()) {
        ++target.next_param;
        return;
    }
    if (target.kind == ParamTargetKind::Function) {
        if (target.primary < scan.functions.size()) {
            NativeFunctionDecl& fn = scan.functions[target.primary];
            if (fn.param_names.size() < fn.param_native_spellings.size()) {
                fn.param_names.resize(fn.param_native_spellings.size());
            }
            if (target.next_param < fn.param_names.size()) {
                fn.param_names[target.next_param] = std::move(name);
            }
        }
    } else if (target.primary < scan.classes.size() &&
               target.secondary < scan.classes[target.primary].methods.size()) {
        FunctionDecl& fn = scan.classes[target.primary].methods[target.secondary];
        if (target.next_param < fn.params.size()) {
            fn.params[target.next_param].name = std::move(name);
        }
    }
    ++target.next_param;
}

void append_doc_text(std::string& doc, std::string text) {
    text = trim_copy(std::move(text));
    if (text.empty()) {
        return;
    }
    if (!doc.empty()) {
        doc += "\n";
    }
    doc += text;
}

void append_doc_text(NativeHeaderScan& scan, const CommentTarget& target, const std::string& text) {
    switch (target.kind) {
    case CommentTargetKind::Type:
        if (target.primary < scan.types.size()) {
            append_doc_text(scan.types[target.primary].doc_comment, text);
        }
        break;
    case CommentTargetKind::Value:
        if (target.primary < scan.values.size()) {
            append_doc_text(scan.values[target.primary].doc_comment, text);
        }
        break;
    case CommentTargetKind::Function:
        if (target.primary < scan.functions.size()) {
            append_doc_text(scan.functions[target.primary].doc_comment, text);
        }
        break;
    case CommentTargetKind::Class:
        if (target.primary < scan.classes.size()) {
            append_doc_text(scan.classes[target.primary].doc_comment, text);
        }
        break;
    case CommentTargetKind::Field:
        if (target.primary < scan.classes.size() &&
            target.secondary < scan.classes[target.primary].fields.size()) {
            append_doc_text(scan.classes[target.primary].fields[target.secondary].doc_comment,
                            text);
        }
        break;
    case CommentTargetKind::StaticField:
        if (target.primary < scan.classes.size() &&
            target.secondary < scan.classes[target.primary].static_fields.size()) {
            append_doc_text(
                scan.classes[target.primary].static_fields[target.secondary].doc_comment, text);
        }
        break;
    case CommentTargetKind::Method:
        if (target.primary < scan.classes.size() &&
            target.secondary < scan.classes[target.primary].methods.size()) {
            append_doc_text(scan.classes[target.primary].methods[target.secondary].doc_comment,
                            text);
        }
        break;
    case CommentTargetKind::Namespace:
        if (target.primary < scan.namespaces.size()) {
            append_doc_text(scan.namespaces[target.primary].doc_comment, text);
        }
        break;
    }
}

size_t append_native_function(NativeHeaderScan& scan,
                              const std::vector<std::pair<int, std::string>>& namespaces,
                              const NativeCursorIdentityIndex& identities, std::string raw_name,
                              const std::string& signature, const SourceLocation& location,
                              const std::string& current_file,
                              const std::vector<std::string>& template_params,
                              const std::vector<bool>& template_param_is_value, bool deleted) {
    NativeFunctionDecl fn;
    fn.name = join_scope(namespaces, raw_name);
    fn.identity = scanned_identity(identities, NativeCursorKind::Function, raw_name, location,
                                   fn.name, current_file);
    fn.template_params = template_params;
    fn.template_param_is_value = template_param_is_value;
    fn.param_native_spellings = qualify_scoped_types(scan, namespaces, signature_params(signature));
    fn.param_names.resize(fn.param_native_spellings.size());
    fn.return_native_spelling =
        qualify_scoped_type(scan, namespaces, signature_return_type(signature));
    fn.param_type_refs.reserve(fn.param_native_spellings.size());
    for (const std::string& param : fn.param_native_spellings) {
        fn.param_type_refs.push_back(parse_native_type_text(param, location));
    }
    fn.return_type_ref = parse_native_type_text(fn.return_native_spelling, location);
    fn.min_params = static_cast<int>(fn.param_native_spellings.size());
    fn.variadic = signature.find("...") != std::string::npos;
    fn.deleted = deleted;
    fn.location = location;
    scan.functions.push_back(std::move(fn));
    return scan.functions.size() - 1;
}

void parse_ast_line(AstParseState& state, const std::string& line) {
    NativeHeaderScan& scan = state.scan;
    const SourceLocation& location = state.root_location;
    auto& namespaces = state.namespaces;
    auto& classes = state.classes;
    auto& enums = state.enums;
    auto& templates = state.templates;
    auto& functions = state.functions;
    auto& param_targets = state.param_targets;
    auto& comment_targets = state.comment_targets;
    std::string& current_file = state.current_file;
    static const std::regex template_type_param(
        R"(TemplateTypeParmDecl.*\bindex [0-9]+ (\.\.\. )?([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex template_value_param(
        R"(NonTypeTemplateParmDecl.*\bindex [0-9]+ (\.\.\. )?([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex template_type_default(R"(TemplateArgument type '([^']+)')");
    static const std::regex template_value_default(R"(TemplateArgument integral (.+)$)");
    static const std::regex template_param_index(R"(\bindex ([0-9]+)(?: |$))");
    static const std::regex template_integer_default(R"(IntegerLiteral.* ([+-]?[0-9]+)$)");
    static const std::regex template_bool_default(R"(CXXBoolLiteralExpr.* (true|false)$)");
    static const std::regex text_comment(R"re(Text="(.*)")re");
    const int depth = ast_depth(line);
    std::smatch match;
    while (!comment_targets.empty() && comment_targets.back().depth >= depth) {
        comment_targets.pop_back();
    }
    if (!comment_targets.empty() && line.find("TextComment") != std::string::npos &&
        std::regex_search(line, match, text_comment)) {
        append_doc_text(scan, comment_targets.back(), match[1].str());
        return;
    }
    while (!namespaces.empty() && namespaces.back().first >= depth) {
        namespaces.pop_back();
    }
    while (!classes.empty() && classes.back().first >= depth) {
        classes.pop_back();
    }
    while (!enums.empty() && enums.back().depth >= depth) {
        enums.pop_back();
    }
    while (!templates.empty() && templates.back().depth >= depth) {
        templates.pop_back();
    }
    while (!functions.empty() && functions.back().first >= depth) {
        functions.pop_back();
    }
    while (!param_targets.empty() && param_targets.back().depth >= depth) {
        param_targets.pop_back();
    }
    if (!relevant_ast_line(line)) {
        return;
    }
    SourceLocation decl_location = ast_source_location(line, location, current_file);
    if (const std::string concrete_file = ast_concrete_source_file(line); !concrete_file.empty()) {
        current_file = concrete_file;
    }
    if (line.find("TypeAliasTemplateDecl") != std::string::npos) {
        templates.push_back({.depth = depth,
                             .kind = TemplateContext::Kind::Alias,
                             .params = {},
                             .param_is_value = {},
                             .param_has_default = {},
                             .param_defaults = {},
                             .last_param_depth = -1,
                             .location = decl_location});
    } else if (line.find("ClassTemplateDecl") != std::string::npos) {
        templates.push_back({.depth = depth,
                             .kind = TemplateContext::Kind::Class,
                             .params = {},
                             .param_is_value = {},
                             .param_has_default = {},
                             .param_defaults = {},
                             .last_param_depth = -1,
                             .location = decl_location});
    } else if (line.find("FunctionTemplateDecl") != std::string::npos) {
        templates.push_back({.depth = depth,
                             .kind = TemplateContext::Kind::Function,
                             .params = {},
                             .param_is_value = {},
                             .param_has_default = {},
                             .param_defaults = {},
                             .last_param_depth = -1,
                             .location = decl_location});
    }
    if (!templates.empty() && decl_location.file == location.file &&
        templates.back().location.file != location.file) {
        decl_location = templates.back().location;
    }
    if (!templates.empty() && line.find("TemplateTypeParmDecl") != std::string::npos) {
        std::string name;
        if (std::regex_search(line, match, template_type_param)) {
            name = match[2].str() + (match[1].matched ? "..." : "");
        } else if (std::regex_search(line, match, template_param_index)) {
            name = native_type_placeholder(match[1].str());
        }
        if (!name.empty()) {
            templates.back().params.push_back(std::move(name));
            templates.back().param_is_value.push_back(false);
            templates.back().param_has_default.push_back(false);
            templates.back().param_defaults.emplace_back();
            templates.back().last_param_depth = depth;
        }
    }
    if (!templates.empty() && line.find("NonTypeTemplateParmDecl") != std::string::npos) {
        std::string name;
        if (std::regex_search(line, match, template_value_param)) {
            name = match[2].str() + (match[1].matched ? "..." : "");
        } else if (std::regex_search(line, match, template_param_index)) {
            name = native_type_placeholder(match[1].str());
        }
        if (!name.empty()) {
            templates.back().params.push_back(std::move(name));
            templates.back().param_is_value.push_back(true);
            templates.back().param_has_default.push_back(false);
            templates.back().param_defaults.emplace_back();
            templates.back().last_param_depth = depth;
        }
    }
    if (!classes.empty() && depth == classes.back().first + 1 &&
        line.find("TemplateArgument") != std::string::npos) {
        ClassDecl& klass = scan.classes[classes.back().second];
        std::string argument;
        if (std::regex_search(line, match, template_type_default)) {
            argument = dudu_type(preserve_native_type_placeholders(match[1].str()));
        } else if (std::regex_search(line, match, template_value_default)) {
            argument = native_template_value(match[1].str());
        }
        if (!argument.empty()) {
            argument = qualify_scoped_type(scan, namespaces, classes, std::move(argument));
            klass.native_specialization_args.push_back(
                parse_native_type_text(argument, decl_location));
        }
    }
    if (!classes.empty() && depth == classes.back().first + 1 &&
        (line.find("TemplateTypeParmDecl") != std::string::npos ||
         line.find("NonTypeTemplateParmDecl") != std::string::npos)) {
        ClassDecl& klass = scan.classes[classes.back().second];
        const std::regex& parameter = line.find("TemplateTypeParmDecl") != std::string::npos
                                          ? template_type_param
                                          : template_value_param;
        std::smatch parameter_match;
        std::smatch index_match;
        if (std::regex_search(line, parameter_match, parameter) &&
            std::regex_search(line, index_match, template_param_index)) {
            const std::string name =
                parameter_match[2].str() + (parameter_match[1].matched ? "..." : "");
            klass.generic_params.push_back(name);
            const std::string placeholder = native_type_placeholder(index_match[1].str());
            const std::string base_name =
                name.ends_with("...") ? name.substr(0, name.size() - 3) : name;
            for (TypeRef& argument : klass.native_specialization_args) {
                replace_native_type_placeholder(argument, placeholder, base_name);
            }
        }
    }
    if (!templates.empty() && !templates.back().param_has_default.empty() &&
        line.find("TemplateArgument") != std::string::npos &&
        depth == templates.back().last_param_depth + 1) {
        templates.back().param_has_default.back() = true;
        std::string default_text;
        if (std::regex_search(line, match, template_type_default)) {
            default_text = dudu_type(match[1].str());
        } else if (std::regex_search(line, match, template_value_default)) {
            default_text = native_template_value(match[1].str());
        }
        if (!default_text.empty()) {
            default_text = qualify_scoped_type(scan, namespaces, classes, default_text);
            templates.back().param_defaults.back() =
                parse_native_type_text(default_text, decl_location);
        }
    }
    if (!templates.empty() && !templates.back().param_has_default.empty() &&
        templates.back().param_has_default.back() &&
        !has_type_ref(templates.back().param_defaults.back()) &&
        depth > templates.back().last_param_depth + 1 &&
        (std::regex_search(line, match, template_integer_default) ||
         std::regex_search(line, match, template_bool_default))) {
        templates.back().param_defaults.back() =
            parse_native_type_text(match[1].str(), decl_location);
    }
    if (!functions.empty() && line.find("ParmVarDecl") != std::string::npos &&
        line.find(" cinit") != std::string::npos) {
        NativeFunctionDecl& fn = scan.functions[functions.back().second];
        if (fn.min_params > 0) {
            --fn.min_params;
        }
    }
    if (!param_targets.empty() && line.find("ParmVarDecl") != std::string::npos) {
        apply_param_name(scan, param_targets.back(), parm_var_decl_name(line).value_or(""));
    }
    parse_ast_declaration(state, line, depth, decl_location);
}

} // namespace native_ast_parse

void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump,
                    const SourceLocation& location) {
    parse_ast_dump(scan, dump, location, {});
}

void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump, const SourceLocation& location,
                    const NativeCursorIdentityIndex& identities) {
    native_ast_parse::AstParseState state(scan, identities, location);
    std::istringstream in(dump);
    std::string line;
    while (std::getline(in, line)) {
        native_ast_parse::parse_ast_line(state, line);
    }
}

} // namespace dudu
