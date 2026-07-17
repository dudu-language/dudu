#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/native/native_header_ast_parse_internal.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"

#include <algorithm>
#include <regex>

namespace dudu::native_ast_parse {
namespace {

int generic_min_args(const TemplateContext& context) {
    int minimum = 0;
    for (size_t i = 0; i < context.params.size(); ++i) {
        if (!context.param_has_default[i] && !context.params[i].ends_with("...")) {
            ++minimum;
        }
    }
    return minimum;
}

void apply_generic_metadata(NativeTypeDecl& type, const TemplateContext& context) {
    type.generic_params = context.params;
    type.generic_default_args = context.param_defaults;
    type.generic_min_args = generic_min_args(context);
}

void apply_generic_metadata(ClassDecl& klass, const TemplateContext& context) {
    klass.generic_params = context.params;
    klass.generic_default_args = context.param_defaults;
    klass.generic_min_args = generic_min_args(context);
}

std::optional<size_t> current_class_index(const std::vector<NativeClassScope>& classes) {
    return classes.empty() ? std::nullopt : classes.back().declaration_index;
}

std::vector<std::string> dependent_type_names(
    const NativeHeaderScan& scan, const std::vector<NativeClassScope>& classes,
    const std::vector<TemplateContext>& templates) {
    std::vector<std::string> out;
    for (const NativeClassScope& scope : classes) {
        if (!scope.declaration_index) {
            continue;
        }
        const ClassDecl& klass = scan.classes[*scope.declaration_index];
        out.insert(out.end(), klass.generic_params.begin(), klass.generic_params.end());
    }
    for (const TemplateContext& context : templates) {
        for (size_t i = 0; i < context.params.size(); ++i) {
            if (i >= context.param_is_value.size() || !context.param_is_value[i]) {
                out.push_back(context.params[i]);
            }
        }
    }
    return out;
}

} // namespace

void parse_ast_declaration(AstParseState& state, const std::string& line, int depth,
                           const SourceLocation& location) {
    static const std::regex typedef_decl(
        R"((TypedefDecl|TypeAliasDecl).*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)'(?:\s*:\s*'([^']*)')?)");
    static const std::regex record_decl(
        R"((RecordDecl|CXXRecordDecl).*\b(struct|class|union) ([A-Za-z_][A-Za-z0-9_]*)\b)");
    static const std::regex specialization_decl(
        R"((ClassTemplate(Partial)?SpecializationDecl).*\b(struct|class|union) ([A-Za-z_][A-Za-z0-9_]*)\b)");
    static const std::regex enum_decl(
        R"(EnumDecl.*(?:line:[0-9]+:[0-9]+|col:[0-9]+)(?: referenced)? (?:(class|struct) )?([A-Za-z_][A-Za-z0-9_]*)(?: '[^']*'(?:\s*:\s*'[^']*')?)?$)");
    static const std::regex namespace_decl(
        R"(NamespaceDecl.*\b([A-Za-z_][A-Za-z0-9_]*)(?: inline)?$)");
    static const std::regex function_decl(
        R"(FunctionDecl.*\b((?:operator[^\s']+)|[A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex using_shadow_function(
        R"(UsingShadowDecl.*\bFunction 0x[0-9A-Fa-f]+ '([A-Za-z_][A-Za-z0-9_]*)' '([^']*)')");
    static const std::regex method_decl(
        R"(CXXMethodDecl.*\b((?:operator(?:\(\)|\[\]|[^\s']+))|[A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex constructor_decl(
        R"(CXXConstructorDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex field_decl(R"(FieldDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex base_decl(R"(\b(public|protected|private) '([^']+)')");
    static const std::regex enum_value_decl(
        R"(EnumConstantDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex variable_decl(R"(VarDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");

    NativeHeaderScan& scan = state.scan;
    const NativeCursorIdentityIndex& identities = state.identities;
    auto& namespaces = state.namespaces;
    auto& classes = state.classes;
    auto& enums = state.enums;
    auto& templates = state.templates;
    auto& functions = state.functions;
    auto& param_targets = state.param_targets;
    auto& comment_targets = state.comment_targets;
    const std::string& current_file = state.current_file;
    std::smatch match;

    if (line.find("NamespaceDecl") != std::string::npos &&
        std::regex_search(line, match, namespace_decl)) {
        if (line.ends_with(" inline")) {
            return;
        }
        const std::string name = match[1].str();
        namespaces.push_back({depth, name});
        if (starts_with(name, "__")) {
            return;
        }
        scan.namespaces.push_back(
            {.name = name,
             .identity = scanned_identity(identities, NativeCursorKind::Namespace, name, location,
                                          name, current_file),
             .location = location});
        comment_targets.push_back({.depth = depth,
                                   .kind = CommentTargetKind::Namespace,
                                   .primary = scan.namespaces.size() - 1});
        return;
    }

    if ((line.find("TypedefDecl") != std::string::npos ||
         line.find("TypeAliasDecl") != std::string::npos) &&
        std::regex_search(line, match, typedef_decl)) {
        const std::string raw_name = match[2].str();
        const std::string name = join_scope(namespaces, raw_name);
        const std::string underlying_type = trim_string(match[3].str());
        const std::vector<std::string> dependent_names =
            dependent_type_names(scan, classes, templates);
        const std::string semantic_type =
            match[4].matched && dependent_names.empty() ? trim_string(match[4].str())
                                                        : underlying_type;
        const std::string lowered_type =
            qualify_scoped_type(scan, namespaces, classes, dudu_type(underlying_type));
        std::string semantic_lowered_type =
            qualify_scoped_type(scan, namespaces, classes, dudu_type(semantic_type));
        const bool useful_alias = (lowered_type != raw_name && lowered_type != name) ||
                                  (lowered_type == raw_name && dudu_primitive_name(raw_name) &&
                                   underlying_type != raw_name);
        if (starts_with(raw_name, "__") && !useful_alias) {
            return;
        }

        const std::string visible_name =
            classes.empty() ? name : classes.back().name + "." + raw_name;
        if (semantic_lowered_type == raw_name || semantic_lowered_type == name ||
            semantic_lowered_type == visible_name) {
            semantic_lowered_type = lowered_type;
        }
        const TypeRef type_ref =
            useful_alias ? parse_native_type_text(semantic_lowered_type, location, dependent_names)
                         : TypeRef{};
        NativeTypeDecl native_type{
            .name = visible_name,
            .native_spelling = useful_alias ? lowered_type : "",
            .type_ref = type_ref,
            .identity = scanned_identity(identities, NativeCursorKind::Type, raw_name, location,
                                         visible_name, current_file),
            .layout = scanned_layout(identities, NativeCursorKind::Type, raw_name, location),
            .location = location};
        if (!templates.empty() && templates.back().kind == TemplateContext::Kind::Alias) {
            apply_generic_metadata(native_type, templates.back());
        }
        scan.types.push_back(std::move(native_type));
        if (const auto class_index = current_class_index(classes);
            class_index && useful_alias) {
            TypeAliasDecl alias{.name = raw_name,
                                .cpp_name = visible_name,
                                .type_ref = type_ref,
                                .origin_module = current_file,
                                .location = location};
            if (!templates.empty() && templates.back().kind == TemplateContext::Kind::Alias) {
                alias.generic_params = templates.back().params;
                alias.generic_min_args = generic_min_args(templates.back());
                alias.generic_default_args = templates.back().param_defaults;
            }
            scan.classes[*class_index].type_aliases.push_back(std::move(alias));
        }
        comment_targets.push_back(
            {.depth = depth, .kind = CommentTargetKind::Type, .primary = scan.types.size() - 1});
        return;
    }

    if (line.find("ClassTemplateSpecializationDecl") != std::string::npos &&
        line.find("implicit_instantiation") != std::string::npos &&
        std::regex_search(line, match, specialization_decl)) {
        const std::string raw_name = match[4].str();
        classes.push_back({.depth = depth,
                           .name = class_name(scan, namespaces, classes, raw_name),
                           .declaration_index = std::nullopt,
                           .specialization_source_args = {}});
        return;
    }

    if ((line.find("ClassTemplatePartialSpecializationDecl") != std::string::npos ||
         (line.find("ClassTemplateSpecializationDecl") != std::string::npos &&
          line.find("implicit_instantiation") == std::string::npos)) &&
        std::regex_search(line, match, specialization_decl)) {
        const std::string raw_name = match[4].str();
        const std::string name = class_name(scan, namespaces, classes, raw_name);
        ClassDecl klass;
        klass.name = name;
        klass.identity = scanned_identity(identities, NativeCursorKind::Class, raw_name, location,
                                          name, current_file);
        klass.layout = scanned_layout(identities, NativeCursorKind::Class, raw_name, location);
        klass.native_declaration = true;
        klass.native_partial_specialization = match[2].matched;
        klass.location = location;
        scan.classes.push_back(std::move(klass));
        classes.push_back({
            .depth = depth,
            .name = name,
            .declaration_index = scan.classes.size() - 1,
            .specialization_source_args =
                identities.find_specialization_arguments(raw_name, location),
        });
        comment_targets.push_back(
            {.depth = depth, .kind = CommentTargetKind::Class, .primary = scan.classes.size() - 1});
        return;
    }

    if ((line.find("RecordDecl") != std::string::npos ||
         line.find("CXXRecordDecl") != std::string::npos) &&
        std::regex_search(line, match, record_decl)) {
        const std::string raw_name = match[3].str();
        if (raw_name == "definition") {
            return;
        }
        const std::string name = class_name(scan, namespaces, classes, raw_name);
        const bool public_record = !starts_with(raw_name, "__");
        const bool internal_definition =
            !public_record && line.find(" definition") != std::string::npos;
        if (!public_record && !internal_definition) {
            if (line.find(" definition") != std::string::npos) {
                classes.push_back(
                    {.depth = depth,
                     .name = name,
                     .declaration_index = std::nullopt,
                     .specialization_source_args = {}});
            }
            return;
        }

        const bool is_union = match[2].str() == "union";
        if (public_record) {
            NativeTypeDecl native_type{
                .name = name,
                .native_spelling = is_union ? "union " + raw_name : "",
                .type_ref = is_union ? named_type_ref(name, location) : TypeRef{},
                .identity = scanned_identity(identities, NativeCursorKind::Class, raw_name,
                                             location, name, current_file),
                .layout = scanned_layout(identities, NativeCursorKind::Class, raw_name, location),
                .location = location};
            if (!templates.empty() && templates.back().kind == TemplateContext::Kind::Class) {
                apply_generic_metadata(native_type, templates.back());
            }
            scan.types.push_back(std::move(native_type));
            comment_targets.push_back({.depth = depth,
                                       .kind = CommentTargetKind::Type,
                                       .primary = scan.types.size() - 1});
        }
        if (line.find(" definition") == std::string::npos) {
            if (public_record && !templates.empty() &&
                templates.back().kind == TemplateContext::Kind::Class) {
                ClassDecl klass;
                klass.name = name;
                klass.identity = scanned_identity(identities, NativeCursorKind::Class, raw_name,
                                                   location, name, current_file);
                klass.native_declaration = true;
                klass.location = location;
                apply_generic_metadata(klass, templates.back());
                scan.classes.push_back(std::move(klass));
            }
            return;
        }

        ClassDecl klass;
        klass.name = name;
        klass.native_declaration = true;
        if (!templates.empty() && templates.back().kind == TemplateContext::Kind::Class) {
            apply_generic_metadata(klass, templates.back());
        }
        klass.identity = scanned_identity(identities, NativeCursorKind::Class, raw_name, location,
                                          name, current_file);
        klass.layout = scanned_layout(identities, NativeCursorKind::Class, raw_name, location);
        klass.location = location;
        scan.classes.push_back(std::move(klass));
        classes.push_back(
            {.depth = depth,
             .name = name,
             .declaration_index = scan.classes.size() - 1,
             .specialization_source_args = {}});
        comment_targets.push_back(
            {.depth = depth, .kind = CommentTargetKind::Class, .primary = scan.classes.size() - 1});
        return;
    }

    if (line.find("EnumDecl") != std::string::npos && std::regex_search(line, match, enum_decl)) {
        const bool scoped = match[1].matched;
        const std::string raw_name = match[2].str();
        const std::string name = class_name(scan, namespaces, classes, raw_name);
        if (starts_with(raw_name, "__")) {
            return;
        }
        scan.types.push_back(
            {.name = name,
             .native_spelling = "",
             .type_ref = {},
             .enum_type = true,
             .identity = scanned_identity(identities, NativeCursorKind::Type, raw_name, location,
                                          name, current_file),
             .layout = scanned_layout(identities, NativeCursorKind::Type, raw_name, location),
             .location = location});
        comment_targets.push_back(
            {.depth = depth, .kind = CommentTargetKind::Type, .primary = scan.types.size() - 1});
        std::string value_scope;
        if (scoped) {
            value_scope = name;
        } else if (!classes.empty()) {
            value_scope = classes.back().name;
        } else {
            value_scope = join_scope(namespaces, "");
            if (!value_scope.empty()) {
                value_scope.pop_back();
            }
        }
        enums.push_back({.depth = depth, .type_name = name, .value_scope = std::move(value_scope)});
        return;
    }

    if (line.find("UsingShadowDecl") != std::string::npos &&
        std::regex_search(line, match, using_shadow_function)) {
        const std::string raw_name = match[1].str();
        if (!starts_with(raw_name, "__")) {
            append_native_function(scan, namespaces, identities, raw_name, match[2].str(), location,
                                   current_file);
        }
        return;
    }

    if (line.find("FunctionDecl") != std::string::npos &&
        std::regex_search(line, match, function_decl)) {
        const std::string raw_name = match[1].str();
        if (starts_with(raw_name, "__")) {
            return;
        }
        std::vector<std::string> template_params;
        std::vector<bool> template_param_is_value;
        std::vector<TypeRef> template_default_args;
        if (!templates.empty() && templates.back().kind == TemplateContext::Kind::Function) {
            template_params = templates.back().params;
            template_param_is_value = templates.back().param_is_value;
            template_default_args = templates.back().param_defaults;
        }
        const size_t function_index =
            append_native_function(scan, namespaces, identities, raw_name, match[2].str(), location,
                                   current_file, template_params, template_param_is_value,
                                   template_default_args,
                                   line.find(" delete", line.rfind('\'')) != std::string::npos);
        functions.push_back({depth, function_index});
        param_targets.push_back(
            {.depth = depth, .kind = ParamTargetKind::Function, .primary = function_index});
        comment_targets.push_back(
            {.depth = depth, .kind = CommentTargetKind::Function, .primary = function_index});
        return;
    }

    if (!classes.empty() && line.find("CXXMethodDecl") != std::string::npos &&
        std::regex_search(line, match, method_decl)) {
        const auto class_index = current_class_index(classes);
        if (!class_index) {
            return;
        }
        FunctionDecl method;
        method.name = match[1].str();
        method.native_identity = scanned_identity(
            identities, NativeCursorKind::Method, method.name, location,
            scan.classes[*class_index].identity.canonical_path + "." + method.name,
            current_file);
        if (!templates.empty() && templates.back().kind == TemplateContext::Kind::Function) {
            method.generic_params = templates.back().params;
            method.generic_param_is_value = templates.back().param_is_value;
            method.generic_default_args = templates.back().param_defaults;
        }
        if (line.find(" static", line.rfind('\'')) == std::string::npos) {
            method.receiver_type_ref =
                parse_native_type_text(signature_receiver_type(match[2].str()), location);
        }
        method.return_type_ref = parse_native_type_text(
            qualify_scoped_type(scan, namespaces, classes, signature_return_type(match[2].str())),
            location);
        for (const std::string& param : signature_params(match[2].str())) {
            ParamDecl decl;
            decl.name = "arg" + std::to_string(method.params.size());
            decl.type_ref = parse_native_type_text(
                qualify_scoped_type(scan, namespaces, classes, param), location);
            decl.variadic = type_ref_contains_kind(decl.type_ref, TypeKind::PackExpansion);
            decl.location = location;
            method.params.push_back(std::move(decl));
        }
        method.min_params = static_cast<int>(method.params.size());
        if (std::ranges::any_of(method.params,
                                [](const ParamDecl& param) { return param.variadic; })) {
            --method.min_params;
        }
        method.deleted = line.find(" delete", line.rfind('\'')) != std::string::npos;
        method.location = location;
        scan.classes[*class_index].methods.push_back(std::move(method));
        param_targets.push_back({.depth = depth,
                                 .kind = ParamTargetKind::Method,
                                 .primary = *class_index,
                                 .secondary = scan.classes[*class_index].methods.size() - 1});
        comment_targets.push_back({.depth = depth,
                                   .kind = CommentTargetKind::Method,
                                   .primary = *class_index,
                                   .secondary = scan.classes[*class_index].methods.size() - 1});
        return;
    }

    if (!classes.empty() && line.find("CXXConstructorDecl") != std::string::npos &&
        std::regex_search(line, match, constructor_decl)) {
        const auto class_index = current_class_index(classes);
        if (!class_index) {
            return;
        }
        const std::vector<std::string> params =
            qualify_scoped_types(scan, namespaces, classes, signature_params(match[2].str()));
        FunctionDecl constructor;
        constructor.name = "init";
        constructor.native_identity = scanned_identity(
            identities, NativeCursorKind::Constructor, match[1].str(), location,
            scan.classes[*class_index].identity.canonical_path + ".init", current_file);
        for (const std::string& param : params) {
            ParamDecl decl;
            decl.name = "arg" + std::to_string(constructor.params.size());
            decl.type_ref = parse_native_type_text(param, location);
            decl.variadic = type_ref_contains_kind(decl.type_ref, TypeKind::PackExpansion);
            decl.location = location;
            constructor.params.push_back(std::move(decl));
        }
        constructor.min_params = static_cast<int>(constructor.params.size());
        if (std::ranges::any_of(constructor.params,
                                [](const ParamDecl& param) { return param.variadic; })) {
            --constructor.min_params;
        }
        constructor.location = location;
        scan.classes[*class_index].methods.push_back(std::move(constructor));
        param_targets.push_back({.depth = depth,
                                 .kind = ParamTargetKind::Method,
                                 .primary = *class_index,
                                 .secondary = scan.classes[*class_index].methods.size() - 1});
        comment_targets.push_back({.depth = depth,
                                   .kind = CommentTargetKind::Method,
                                   .primary = *class_index,
                                   .secondary = scan.classes[*class_index].methods.size() - 1});
        return;
    }

    if (!classes.empty() && line.find("FieldDecl") != std::string::npos &&
        std::regex_search(line, match, field_decl)) {
        const auto class_index = current_class_index(classes);
        if (!class_index) {
            return;
        }
        const std::string type =
            qualify_scoped_type(scan, namespaces, classes, dudu_type(match[2].str()));
        scan.classes[*class_index].fields.push_back(
            {.name = match[1].str(),
             .type_ref = parse_native_type_text(type, location),
             .value_expr = {},
             .decorators = {},
             .location = location});
        comment_targets.push_back({.depth = depth,
                                   .kind = CommentTargetKind::Field,
                                   .primary = *class_index,
                                   .secondary = scan.classes[*class_index].fields.size() - 1});
        return;
    }

    if (!classes.empty() &&
        (line.find("public '") != std::string::npos ||
         line.find("protected '") != std::string::npos ||
         line.find("private '") != std::string::npos) &&
        std::regex_search(line, match, base_decl)) {
        if (const auto class_index = current_class_index(classes)) {
            add_base_class(
                scan.classes[*class_index],
                qualify_scoped_type(scan, namespaces, classes, dudu_type(match[2].str())),
                location);
        }
        return;
    }

    if (line.find("EnumConstantDecl") != std::string::npos &&
        std::regex_search(line, match, enum_value_decl)) {
        const std::string raw_name = match[1].str();
        if (starts_with(raw_name, "__")) {
            return;
        }
        const std::string type = enums.empty() ? dudu_type(match[2].str()) : enums.back().type_name;
        const std::string name = enums.empty() || enums.back().value_scope.empty()
                                     ? raw_name
                                     : enums.back().value_scope + "." + raw_name;
        scan.values.push_back({.name = name,
                               .native_spelling = type,
                               .type_ref = parse_native_type_text(type, location),
                               .enum_constant = true,
                               .identity = scanned_identity(identities, NativeCursorKind::Value,
                                                            raw_name, location, name, current_file),
                               .location = location});
        comment_targets.push_back(
            {.depth = depth, .kind = CommentTargetKind::Value, .primary = scan.values.size() - 1});
        return;
    }

    if (line.find("VarDecl") == std::string::npos ||
        line.find("ParmVarDecl") != std::string::npos ||
        !std::regex_search(line, match, variable_decl)) {
        return;
    }

    const std::string raw_name = match[1].str();
    if (starts_with(raw_name, "__")) {
        return;
    }
    const std::string type =
        qualify_scoped_type(scan, namespaces, classes, dudu_type(match[2].str()));
    if (!classes.empty()) {
        const auto class_index = current_class_index(classes);
        if (!class_index) {
            return;
        }
        scan.classes[*class_index].static_fields.push_back(
            {.name = raw_name,
             .cpp_name = {},
             .type_ref = parse_native_type_text(type, location),
             .value_expr = {},
             .decorators = {},
             .origin_module = {},
             .location = location});
        comment_targets.push_back(
            {.depth = depth,
             .kind = CommentTargetKind::StaticField,
             .primary = *class_index,
             .secondary = scan.classes[*class_index].static_fields.size() - 1});
        return;
    }
    if (raw_name == "dudu_probe") {
        return;
    }
    const std::string name = join_scope(namespaces, raw_name);
    scan.values.push_back({.name = name,
                           .native_spelling = type,
                           .type_ref = parse_native_type_text(type, location),
                           .identity = scanned_identity(identities, NativeCursorKind::Value,
                                                        raw_name, location, name, current_file),
                           .location = location});
    comment_targets.push_back(
        {.depth = depth, .kind = CommentTargetKind::Value, .primary = scan.values.size() - 1});
}

} // namespace dudu::native_ast_parse
