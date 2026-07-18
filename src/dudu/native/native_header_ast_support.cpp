#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/native/native_header_ast_parse_internal.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace dudu::native_ast_parse {
namespace {

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

void append_doc_text(std::string& doc, std::string text) {
    text = trim_string(std::move(text));
    if (text.empty()) {
        return;
    }
    if (!doc.empty()) {
        doc += "\n";
    }
    doc += text;
}

} // namespace

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

void finalize_class_scope(AstParseState& state, const NativeClassScope& scope) {
    if (!scope.declaration_index || scope.specialization_source_args.empty()) {
        return;
    }
    ClassDecl& klass = state.scan.classes[*scope.declaration_index];
    std::vector<TypeRef> written_args;
    written_args.reserve(scope.specialization_source_args.size());
    for (const std::string& argument : scope.specialization_source_args) {
        std::string source_arg = dudu_type(preserve_native_type_placeholders(argument));
        source_arg = qualify_scoped_type(state.scan, state.namespaces, std::move(source_arg));
        written_args.push_back(
            parse_native_type_text(std::move(source_arg), klass.location, klass.generic_params));
    }
    if (written_args.size() != klass.native_specialization_args.size()) {
        klass.native_specialization_args = std::move(written_args);
        return;
    }
    for (size_t i = 0; i < scope.specialization_source_args.size(); ++i) {
        TypeRef written = std::move(written_args[i]);
        // Clang may collapse a dependent source argument such as void_t<T::value_type> to
        // void in the AST. The written argument preserves the SFINAE condition needed when
        // Dudu later matches a concrete specialization.
        klass.native_specialization_args[i] = std::move(written);
    }
}

int ast_depth(const std::string& line) {
    const size_t branch = line.find("|-");
    const size_t last = line.find("`-");
    if (branch == std::string::npos) {
        return last == std::string::npos ? 0 : static_cast<int>(last / 2);
    }
    if (last == std::string::npos) {
        return static_cast<int>(branch / 2);
    }
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
    out.file = SourceFileName(relative_location ? current_file : file);
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

NativeDeclarationMetadata scanned_metadata(const NativeCursorIdentityIndex& identities,
                                           NativeCursorKind kind, std::string_view spelling,
                                           const SourceLocation& location) {
    return identities.find_metadata(kind, spelling, location).value_or(NativeDeclarationMetadata{});
}

std::optional<std::string> parm_var_decl_name(const std::string& line) {
    const size_t type_quote = line.find(" '");
    if (type_quote == std::string::npos) {
        return std::nullopt;
    }
    std::istringstream words(trim_string(line.substr(0, type_quote)));
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
                              const std::vector<bool>& template_param_is_value,
                              const std::vector<TypeRef>& template_default_args, bool deleted) {
    NativeFunctionDecl fn;
    fn.name = join_scope(namespaces, raw_name);
    fn.identity = scanned_identity(identities, NativeCursorKind::Function, raw_name, location,
                                   fn.name, current_file);
    fn.template_params = template_params;
    fn.template_param_is_value = template_param_is_value;
    fn.template_default_args = template_default_args;
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
    fn.native_metadata =
        scanned_metadata(identities, NativeCursorKind::Function, raw_name, location);
    for (size_t index = 0;
         index < fn.param_names.size() && index < fn.native_metadata.parameters.size(); ++index) {
        if (!fn.native_metadata.parameters[index].name.empty()) {
            fn.param_names[index] = fn.native_metadata.parameters[index].name;
        }
        if (!fn.native_metadata.parameters[index].default_value.empty()) {
            fn.min_params = std::min(fn.min_params, static_cast<int>(index));
        }
    }
    scan.functions.push_back(std::move(fn));
    return scan.functions.size() - 1;
}

} // namespace dudu::native_ast_parse
