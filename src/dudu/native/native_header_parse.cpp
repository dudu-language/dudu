#include "dudu/native/native_header_parse.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_header_ast_parse_internal.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"

#include <optional>
#include <regex>
#include <sstream>

namespace dudu {
namespace native_ast_parse {
namespace {

std::string native_template_value(std::string text) {
    text = trim_copy(std::move(text));
    if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

} // namespace

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
    static const std::regex template_template_param(
        R"(TemplateTemplateParmDecl.*\bindex [0-9]+ ([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex template_type_default(R"(TemplateArgument type '([^']+)')");
    static const std::regex template_value_default(R"(TemplateArgument integral (.+)$)");
    static const std::regex template_param_index(R"(\bindex ([0-9]+)(?: |$))");
    static const std::regex template_integer_default(R"(IntegerLiteral.* ([+-]?[0-9]+)$)");
    static const std::regex template_bool_default(R"(CXXBoolLiteralExpr.* (true|false)$)");
    static const std::regex initializer_reference(
        R"(DeclRefExpr.*\b(?:NonTypeTemplateParm|Var) [^']* '([A-Za-z_][A-Za-z0-9_]*)')");
    static const std::regex initializer_integer(R"(IntegerLiteral.* ([+-]?[0-9]+)$)");
    static const std::regex initializer_bool(R"(CXXBoolLiteralExpr.* (true|false)$)");
    static const std::regex text_comment(R"re(Text="(.*)")re");
    const int depth = ast_depth(line);
    std::smatch match;
    if (state.static_initializer && state.static_initializer->depth >= depth) {
        state.static_initializer.reset();
    }
    if (state.static_initializer && depth > state.static_initializer->depth) {
        std::string value;
        if (std::regex_search(line, match, initializer_reference) ||
            std::regex_search(line, match, initializer_integer) ||
            std::regex_search(line, match, initializer_bool)) {
            value = match[1].str();
        }
        if (!value.empty()) {
            const StaticInitializerTarget& target = *state.static_initializer;
            if (target.class_index < scan.classes.size() &&
                target.field_index < scan.classes[target.class_index].static_fields.size()) {
                scan.classes[target.class_index].static_fields[target.field_index].value_expr =
                    parse_expr_text(value, scan.classes[target.class_index]
                                               .static_fields[target.field_index]
                                               .location);
            }
            state.static_initializer.reset();
        }
    }
    while (!comment_targets.empty() && comment_targets.back().depth >= depth) {
        comment_targets.pop_back();
    }
    if (!comment_targets.empty() && line.find("TextComment") != std::string::npos &&
        std::regex_search(line, match, text_comment)) {
        append_doc_text(scan, comment_targets.back(), match[1].str());
        return;
    }
    while (!classes.empty() && classes.back().depth >= depth) {
        finalize_class_scope(state, classes.back());
        classes.pop_back();
    }
    while (!namespaces.empty() && namespaces.back().first >= depth) {
        namespaces.pop_back();
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
    if (!templates.empty() && depth == templates.back().depth + 1 &&
        line.find("TemplateTemplateParmDecl") != std::string::npos &&
        std::regex_search(line, match, template_template_param)) {
        templates.back().params.push_back(match[1].str());
        templates.back().param_is_value.push_back(false);
        templates.back().param_has_default.push_back(false);
        templates.back().param_defaults.emplace_back();
        templates.back().last_param_depth = depth;
    }
    if (!templates.empty() && depth == templates.back().depth + 1 &&
        line.find("TemplateTypeParmDecl") != std::string::npos) {
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
    if (!templates.empty() && depth == templates.back().depth + 1 &&
        line.find("NonTypeTemplateParmDecl") != std::string::npos) {
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
    if (!classes.empty() && classes.back().declaration_index && depth == classes.back().depth + 1 &&
        line.find("TemplateArgument") != std::string::npos) {
        ClassDecl& klass = scan.classes[*classes.back().declaration_index];
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
    if (!classes.empty() && classes.back().declaration_index && depth == classes.back().depth + 1 &&
        line.find("TemplateTemplateParmDecl") != std::string::npos &&
        std::regex_search(line, match, template_template_param)) {
        scan.classes[*classes.back().declaration_index].generic_params.push_back(match[1].str());
    }
    if (!classes.empty() && classes.back().declaration_index && depth == classes.back().depth + 1 &&
        (line.find("TemplateTypeParmDecl") != std::string::npos ||
         line.find("NonTypeTemplateParmDecl") != std::string::npos)) {
        ClassDecl& klass = scan.classes[*classes.back().declaration_index];
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
    const std::optional<size_t> class_index =
        classes.empty() ? std::nullopt : classes.back().declaration_index;
    const size_t static_field_count =
        class_index ? scan.classes[*class_index].static_fields.size() : 0;
    parse_ast_declaration(state, line, depth, decl_location);
    if (class_index && line.find("VarDecl") != std::string::npos &&
        line.find(" cinit") != std::string::npos &&
        scan.classes[*class_index].static_fields.size() > static_field_count) {
        state.static_initializer = StaticInitializerTarget{
            .depth = depth,
            .class_index = *class_index,
            .field_index = scan.classes[*class_index].static_fields.size() - 1};
    }
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
    while (!state.classes.empty()) {
        native_ast_parse::finalize_class_scope(state, state.classes.back());
        state.classes.pop_back();
    }
    qualify_completed_native_scan(scan);
}

} // namespace dudu
