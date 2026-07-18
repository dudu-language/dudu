#include "dudu/lsp/language_server_symbols.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_documentation.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/sema/sema_constructors.hpp"

#include <algorithm>
#include <optional>
#include <sstream>

namespace dudu {

namespace {

const NativeParameterMetadata*
native_parameter_metadata(const std::vector<NativeParameterMetadata>& metadata,
                          std::string_view name, size_t index) {
    const auto named = std::ranges::find(metadata, name, &NativeParameterMetadata::name);
    if (named != metadata.end()) {
        return &*named;
    }
    return index < metadata.size() && metadata[index].name.empty() ? &metadata[index] : nullptr;
}

std::vector<Symbol::Parameter>
native_template_symbol_parameters(const NativeDeclarationMetadata& metadata) {
    std::vector<Symbol::Parameter> out;
    out.reserve(metadata.template_parameters.size());
    for (const NativeParameterMetadata& parameter : metadata.template_parameters) {
        std::string label = parameter.name;
        if (!parameter.default_value.empty()) {
            label += " = " + parameter.default_value;
        }
        out.push_back({.label = std::move(label), .documentation = parameter.doc_comment});
    }
    return out;
}

} // namespace

std::vector<Symbol::Parameter> function_symbol_parameters(const FunctionDecl& function) {
    std::vector<Symbol::Parameter> out;
    out.reserve(function.params.size());
    for (size_t index = 0; index < function.params.size(); ++index) {
        const ParamDecl& parameter = function.params[index];
        const NativeParameterMetadata* native =
            native_parameter_metadata(function.native_metadata.parameters, parameter.name, index);
        std::string label = parameter.name + ": " + type_ref_text(parameter.type_ref);
        if (native != nullptr && !native->default_value.empty()) {
            label += " = " + native->default_value;
        }
        out.push_back({
            .label = std::move(label),
            .documentation = native != nullptr && !native->doc_comment.empty()
                                 ? native->doc_comment
                                 : parameter_documentation(function.doc_comment, parameter.name),
        });
    }
    return out;
}

std::vector<Symbol::Parameter> constructor_symbol_parameters(const ClassDecl& klass) {
    const std::string docs = constructor_doc_comment(klass);
    std::vector<Symbol::Parameter> out;
    for (const ConstructorParam& parameter : constructor_params(klass)) {
        out.push_back({.label = parameter.name + ": " + type_ref_text(parameter.type_ref),
                       .documentation = parameter_documentation(docs, parameter.name)});
    }
    return out;
}

Symbol method_symbol(const FunctionDecl& method, bool native) {
    return {
        .name = method.name,
        .detail = function_detail(method),
        .location = method.location,
        .kind = is_constructor_method_name(method.name) ? lsp_symbol_kind::Constructor
                                                        : lsp_symbol_kind::Method,
        .native_identity_key = native ? native_identity_key(method.native_identity) : std::nullopt,
        .doc_comment = method.doc_comment,
        .native_declaration = native ? method.native_metadata.declaration : std::string{},
        .deprecated_message = native ? method.native_metadata.deprecated_message : std::string{},
        .return_documentation = native ? method.native_metadata.return_doc_comment : std::string{},
        .parameters = function_symbol_parameters(method),
        .template_parameters = native ? native_template_symbol_parameters(method.native_metadata)
                                      : std::vector<Symbol::Parameter>{}};
}

std::vector<Symbol::Parameter>
native_function_symbol_parameters(const NativeFunctionDecl& function) {
    const std::vector<TypeRef> types = native_function_param_type_refs(function);
    std::vector<Symbol::Parameter> out;
    out.reserve(types.size());
    for (size_t index = 0; index < types.size(); ++index) {
        const std::string name =
            index < function.param_names.size() && !function.param_names[index].empty()
                ? function.param_names[index]
                : "arg" + std::to_string(index);
        const NativeParameterMetadata* native =
            native_parameter_metadata(function.native_metadata.parameters, name, index);
        std::string label = name + ": " + type_ref_text(types[index]);
        if (native != nullptr && !native->default_value.empty()) {
            label += " = " + native->default_value;
        }
        out.push_back({
            .label = std::move(label),
            .documentation = native != nullptr && !native->doc_comment.empty()
                                 ? native->doc_comment
                                 : parameter_documentation(function.doc_comment, name),
        });
    }
    return out;
}

bool is_constructor_method_name(const std::string& name) {
    return name == "init";
}

std::string generic_params_label(const std::vector<std::string>& params) {
    if (params.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << params[i];
    }
    out << "]";
    return out.str();
}

std::string function_detail(const FunctionDecl& fn) {
    std::ostringstream out;
    out << "def " << fn.name << generic_params_label(fn.generic_params) << "(";
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << fn.params[i].name << ": " << type_ref_text(fn.params[i].type_ref);
    }
    out << ")";
    if (function_has_return_type(fn)) {
        out << " -> " << type_ref_text(fn.return_type_ref);
    }
    return out.str();
}

std::string constructor_detail(const ClassDecl& klass) {
    std::ostringstream out;
    out << klass.name << "(";
    const std::vector<ConstructorParam> params = constructor_params(klass);
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << params[i].name << ": " << type_ref_text(params[i].type_ref);
    }
    out << ")";
    return out.str();
}

std::string constructor_doc_comment(const ClassDecl& klass) {
    for (const FunctionDecl& method : klass.methods) {
        if (is_constructor_method_name(method.name) && !method.doc_comment.empty()) {
            return method.doc_comment;
        }
    }
    return klass.doc_comment;
}

std::string native_macro_detail(const NativeMacroDecl& macro) {
    if (!macro.function_like) {
        return "object-like macro " + macro.name;
    }
    std::ostringstream out;
    out << "function-like macro " << macro.name << "(";
    for (int i = 0; i < macro.arity; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << (static_cast<size_t>(i) < macro.param_names.size() &&
                        !macro.param_names[static_cast<size_t>(i)].empty()
                    ? macro.param_names[static_cast<size_t>(i)]
                    : "arg" + std::to_string(i));
    }
    out << ")";
    return out.str();
}

std::string native_import_provenance(const ModuleAst& module, std::string_view name) {
    for (const ImportDecl& import : module.imports) {
        if (import.kind != ImportKind::ForeignC && import.kind != ImportKind::ForeignCpp) {
            continue;
        }
        const std::string bound = bound_import_name(import);
        if (!import.alias.empty() && (name == bound || name.starts_with(bound + "."))) {
            return "Imported by: `" + render_import_decl(import) + "`";
        }
    }
    return {};
}

std::string native_function_detail(const NativeFunctionDecl& fn) {
    std::ostringstream out;
    out << fn.name << "(";
    const std::vector<TypeRef> params = native_function_param_type_refs(fn);
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (i < fn.param_names.size() && !fn.param_names[i].empty()) {
            out << fn.param_names[i] << ": ";
        }
        out << type_ref_text(params[i]);
        if (i < fn.native_metadata.parameters.size() &&
            !fn.native_metadata.parameters[i].default_value.empty()) {
            out << " = " << fn.native_metadata.parameters[i].default_value;
        }
    }
    if (fn.variadic) {
        if (!params.empty()) {
            out << ", ";
        }
        out << "...";
    }
    out << ") -> " << type_ref_text(native_function_return_type_ref(fn));
    return out.str();
}

std::optional<std::string> native_function_signature_doc(const NativeFunctionDecl& fn) {
    const TypeRef return_type = native_function_return_type_ref(fn);
    const std::vector<TypeRef> param_types = native_function_param_type_refs(fn);
    const bool has_concrete_return = has_type_ref(return_type) && !type_ref_is_auto(return_type);
    const bool has_concrete_param = std::ranges::any_of(param_types, [](const TypeRef& param) {
        return has_type_ref(param) && !type_ref_is_auto(param);
    });
    const bool has_native_signature = has_concrete_return || has_concrete_param;
    if (!has_native_signature) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "Native signature: `native " << type_ref_text(return_type) << "(";
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << type_ref_text(param_types[i]);
    }
    if (fn.variadic) {
        if (!fn.param_native_spellings.empty()) {
            out << ", ";
        }
        out << "...";
    }
    out << ")`";
    return out.str();
}

std::string combine_doc_comment(std::string existing, const std::optional<std::string>& extra) {
    if (!extra.has_value()) {
        return existing;
    }
    if (existing.empty()) {
        return *extra;
    }
    return existing + "\n\n" + *extra;
}

std::string native_type_detail(const NativeClassDefinitionIndex& class_index,
                               const NativeTypeDecl& type) {
    const bool alias_type = has_type_ref(type.type_ref) || !type.native_spelling.empty();
    if (!alias_type) {
        return "native type";
    }
    std::string detail = "native type = " + native_type_alias_type_text(type);
    if (native_alias_target_class_definition(class_index, type).has_value()) {
        detail += " resolves to native class " + native_type_alias_type_text(type);
    }
    return detail;
}

std::optional<std::string> native_identity_key(const NativeSymbolId& identity) {
    const std::string key = native_symbol_identity_key(identity);
    if (key.empty()) {
        return std::nullopt;
    }
    return key;
}

std::optional<std::string> native_class_member_identity_key(const ClassDecl& klass,
                                                            const std::string& member) {
    const std::string key = native_class_member_symbol_identity_key(klass, member);
    if (key.empty()) {
        return std::nullopt;
    }
    return key;
}

std::vector<Symbol> symbols_for_module(const ModuleAst& module, bool include_native) {
    std::vector<Symbol> out;
    for (const ClassDecl& klass : module.classes) {
        out.push_back({.name = klass.name,
                       .detail = "class " + klass.name + generic_params_label(klass.generic_params),
                       .location = klass.location,
                       .kind = lsp_symbol_kind::Class,
                       .native_identity_key = std::nullopt,
                       .doc_comment = klass.doc_comment});
        for (const FieldDecl& field : klass.fields) {
            out.push_back({.name = field.name,
                           .detail = field.name + ": " + type_ref_text(field.type_ref),
                           .location = field.location,
                           .kind = lsp_symbol_kind::Field,
                           .native_identity_key = std::nullopt,
                           .doc_comment = field.doc_comment,
                           .qualified_name = klass.name + "." + field.name});
        }
        for (const ConstDecl& constant : klass.constants) {
            out.push_back({.name = constant.name,
                           .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                           .location = constant.location,
                           .kind = lsp_symbol_kind::Constant,
                           .native_identity_key = std::nullopt,
                           .doc_comment = constant.doc_comment,
                           .qualified_name = klass.name + "." + constant.name});
        }
        for (const ConstDecl& field : klass.static_fields) {
            out.push_back({.name = field.name,
                           .detail = field.name + ": " + type_ref_text(field.type_ref),
                           .location = field.location,
                           .kind = lsp_symbol_kind::Field,
                           .native_identity_key = std::nullopt,
                           .doc_comment = field.doc_comment,
                           .qualified_name = klass.name + "." + field.name});
        }
        for (const FunctionDecl& method : klass.methods) {
            Symbol symbol = method_symbol(method, false);
            symbol.qualified_name = klass.name + "." + method.name;
            out.push_back(std::move(symbol));
        }
    }
    for (const EnumDecl& en : module.enums) {
        out.push_back({.name = en.name,
                       .detail = "enum " + en.name,
                       .location = en.location,
                       .kind = lsp_symbol_kind::Enum,
                       .native_identity_key = std::nullopt,
                       .doc_comment = en.doc_comment});
        for (const EnumValueDecl& value : en.values) {
            const std::string name = en.name + "." + value.name;
            out.push_back({.name = name,
                           .detail = "enum variant " + name,
                           .location = value.location,
                           .kind = lsp_symbol_kind::EnumMember,
                           .native_identity_key = std::nullopt,
                           .doc_comment = value.doc_comment});
        }
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        out.push_back({.name = alias.name,
                       .detail = "type " + alias.name + " = " + type_ref_text(alias.type_ref),
                       .location = alias.location,
                       .kind = lsp_symbol_kind::Struct,
                       .native_identity_key = std::nullopt,
                       .doc_comment = alias.doc_comment});
    }
    for (const ConstDecl& constant : module.constants) {
        out.push_back({.name = constant.name,
                       .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                       .location = constant.location,
                       .kind = lsp_symbol_kind::Constant,
                       .native_identity_key = std::nullopt,
                       .doc_comment = constant.doc_comment});
    }
    for (const FunctionDecl& fn : module.functions) {
        out.push_back({.name = fn.name,
                       .detail = function_detail(fn),
                       .location = fn.location,
                       .kind = lsp_symbol_kind::Function,
                       .native_identity_key = std::nullopt,
                       .doc_comment = fn.doc_comment,
                       .parameters = function_symbol_parameters(fn)});
    }
    if (!include_native) {
        return out;
    }
    const NativeClassDefinitionIndex native_class_index = native_class_definition_index(module);
    for (const NativeNamespaceDecl& ns : module.native_namespaces) {
        out.push_back({.name = ns.name,
                       .detail = "native namespace " + ns.name,
                       .location = ns.location,
                       .kind = lsp_symbol_kind::Namespace,
                       .native_identity_key = native_identity_key(ns.identity),
                       .doc_comment = ns.doc_comment});
    }
    for (const NativeTypeDecl& type : module.native_types) {
        std::optional<TypeLayout> layout = type.layout;
        if (!layout) {
            const std::optional<NativeClassDefinition> target =
                native_alias_target_class_definition(native_class_index, type);
            if (target && target->declaration != nullptr) {
                layout = target->declaration->layout;
            }
        }
        out.push_back({.name = type.name,
                       .detail = native_type_detail(native_class_index, type),
                       .location = type.location,
                       .kind = lsp_symbol_kind::Struct,
                       .native_identity_key = native_identity_key(type.identity),
                       .doc_comment = type.doc_comment,
                       .layout_size = layout ? std::optional<size_t>(layout->size) : std::nullopt,
                       .layout_alignment =
                           layout ? std::optional<size_t>(layout->alignment) : std::nullopt});
    }
    for (const NativeValueDecl& value : module.native_values) {
        out.push_back({.name = value.name,
                       .detail = value.name + ": " + native_value_type_text(value),
                       .location = value.location,
                       .kind = lsp_symbol_kind::Constant,
                       .native_identity_key = native_identity_key(value.identity),
                       .doc_comment = value.doc_comment});
    }
    for (const NativeMacroDecl& macro : module.native_macros) {
        out.push_back(
            {.name = macro.name,
             .detail = native_macro_detail(macro),
             .location = macro.location,
             .kind = macro.function_like ? lsp_symbol_kind::Namespace : lsp_symbol_kind::Constant,
             .native_identity_key = native_identity_key(macro.identity),
             .doc_comment = macro.doc_comment,
             .provenance = native_import_provenance(module, macro.name)});
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        out.push_back(
            {.name = fn.name,
             .detail = native_function_detail(fn),
             .location = fn.location,
             .kind = lsp_symbol_kind::Function,
             .native_identity_key = native_identity_key(fn.identity),
             .doc_comment = combine_doc_comment(fn.doc_comment, native_function_signature_doc(fn)),
             .native_declaration = fn.native_metadata.declaration,
             .deprecated_message = fn.native_metadata.deprecated_message,
             .return_documentation = fn.native_metadata.return_doc_comment,
             .provenance = native_import_provenance(module, fn.name),
             .parameters = native_function_symbol_parameters(fn),
             .template_parameters = native_template_symbol_parameters(fn.native_metadata)});
    }
    for (const ClassDecl& klass : module.native_classes) {
        out.push_back(
            {.name = klass.name,
             .detail = "native class " + klass.name,
             .location = klass.location,
             .kind = lsp_symbol_kind::Class,
             .native_identity_key = native_identity_key(klass.identity),
             .doc_comment = klass.doc_comment,
             .native_declaration = klass.native_metadata.declaration,
             .deprecated_message = klass.native_metadata.deprecated_message,
             .provenance = native_import_provenance(module, klass.name),
             .template_parameters = native_template_symbol_parameters(klass.native_metadata)});
        for (const FieldDecl& field : klass.fields) {
            out.push_back(
                {.name = klass.name + "." + field.name,
                 .detail = field.name + ": " + type_ref_text(field.type_ref),
                 .location = field.location,
                 .kind = lsp_symbol_kind::Field,
                 .native_identity_key = native_class_member_identity_key(klass, field.name),
                 .doc_comment = field.doc_comment});
        }
        for (const ConstDecl& constant : klass.constants) {
            out.push_back(
                {.name = klass.name + "." + constant.name,
                 .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                 .location = constant.location,
                 .kind = lsp_symbol_kind::Constant,
                 .native_identity_key = native_class_member_identity_key(klass, constant.name),
                 .doc_comment = constant.doc_comment});
        }
        for (const ConstDecl& field : klass.static_fields) {
            out.push_back(
                {.name = klass.name + "." + field.name,
                 .detail = field.name + ": " + type_ref_text(field.type_ref),
                 .location = field.location,
                 .kind = lsp_symbol_kind::Field,
                 .native_identity_key = native_class_member_identity_key(klass, field.name),
                 .doc_comment = field.doc_comment});
        }
        for (const FunctionDecl& method : klass.methods) {
            Symbol symbol = method_symbol(method, true);
            symbol.name = klass.name + "." + method.name;
            symbol.provenance = native_import_provenance(module, klass.name + "." + method.name);
            out.push_back(std::move(symbol));
        }
    }
    return out;
}

std::optional<Symbol> exact_symbol_match(const std::vector<Symbol>& symbols,
                                         const std::string& query) {
    std::vector<Symbol> matches;
    for (const Symbol& symbol : symbols) {
        if (symbol.qualified_name.value_or(symbol.name) == query) {
            matches.push_back(symbol);
        }
    }
    if (matches.empty()) {
        return std::nullopt;
    }
    if (matches.size() == 1) {
        return matches.front();
    }
    const std::optional<std::string>& identity = matches.front().native_identity_key;
    if (!identity.has_value() ||
        !std::all_of(matches.begin(), matches.end(), [&](const Symbol& symbol) {
            return symbol.native_identity_key == identity;
        })) {
        return std::nullopt;
    }
    for (const Symbol& symbol : matches) {
        if (symbol.kind == lsp_symbol_kind::Class) {
            return symbol;
        }
    }
    return matches.front();
}

std::optional<Symbol> native_namespace_segment_symbol(const std::vector<Symbol>& symbols,
                                                      const std::optional<std::string>& selected,
                                                      const std::string& query) {
    if (!selected || *selected == query) {
        return std::nullopt;
    }
    const std::optional<Symbol> symbol = exact_symbol_match(symbols, *selected);
    if (symbol && symbol->kind == lsp_symbol_kind::Namespace &&
        symbol->native_identity_key.has_value()) {
        return symbol;
    }
    return std::nullopt;
}

} // namespace dudu
