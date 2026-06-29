#include "dudu/lsp/language_server_symbols.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_headers.hpp"

#include <algorithm>
#include <optional>
#include <sstream>

namespace dudu {

bool is_constructor_method_name(const std::string& name) {
    return name == "init";
}

std::string function_detail(const FunctionDecl& fn) {
    std::ostringstream out;
    out << "def " << fn.name << "(";
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

std::string native_macro_detail(const NativeMacroDecl& macro) {
    if (!macro.function_like) {
        return "macro " + macro.name;
    }
    std::ostringstream out;
    out << "macro " << macro.name << "(";
    for (int i = 0; i < macro.arity; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "arg" << i;
    }
    out << ")";
    return out.str();
}

std::string native_function_detail(const NativeFunctionDecl& fn) {
    std::ostringstream out;
    out << fn.name << "(";
    const std::vector<TypeRef> params = native_function_param_type_refs(fn);
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << type_ref_text(params[i]);
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
    const bool has_concrete_return =
        !fn.return_native_spelling.empty() && fn.return_native_spelling != "auto";
    const bool has_concrete_param =
        std::ranges::any_of(fn.param_native_spellings, [](const std::string& param) {
            return !param.empty() && param != "auto";
        });
    const bool has_native_signature = has_concrete_return || has_concrete_param;
    if (!has_native_signature) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "Native signature: `native " << fn.return_native_spelling << "(";
    for (size_t i = 0; i < fn.param_native_spellings.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << fn.param_native_spellings[i];
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
    if (const std::optional<NativeClassDefinition> target =
            native_alias_target_class_definition(class_index, type)) {
        detail += " resolves to native class " + target->name;
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

bool suffix_symbol_match(const std::string& symbol, const std::string& query) {
    if (symbol == query) {
        return false;
    }
    const size_t dot = symbol.rfind('.');
    return dot != std::string::npos && symbol.substr(dot + 1) == query;
}

std::optional<Symbol> preferred_symbol_match(const std::vector<Symbol>& matches) {
    if (matches.empty()) {
        return std::nullopt;
    }
    for (const Symbol& symbol : matches) {
        if (symbol.kind == lsp_symbol_kind::Class) {
            return symbol;
        }
    }
    return matches.front();
}

std::vector<Symbol> symbols_for_module(const ModuleAst& module, bool include_native) {
    std::vector<Symbol> out;
    for (const ClassDecl& klass : module.classes) {
        out.push_back({.name = klass.name,
                       .detail = "class " + klass.name,
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
                           .doc_comment = field.doc_comment});
        }
        for (const ConstDecl& constant : klass.constants) {
            out.push_back({.name = constant.name,
                           .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                           .location = constant.location,
                           .kind = lsp_symbol_kind::Constant,
                           .native_identity_key = std::nullopt,
                           .doc_comment = constant.doc_comment});
        }
        for (const ConstDecl& field : klass.static_fields) {
            out.push_back({.name = field.name,
                           .detail = field.name + ": " + type_ref_text(field.type_ref),
                           .location = field.location,
                           .kind = lsp_symbol_kind::Field,
                           .native_identity_key = std::nullopt,
                           .doc_comment = field.doc_comment});
        }
        for (const FunctionDecl& method : klass.methods) {
            out.push_back({.name = method.name,
                           .detail = function_detail(method),
                           .location = method.location,
                           .kind = is_constructor_method_name(method.name)
                                       ? lsp_symbol_kind::Constructor
                                       : lsp_symbol_kind::Method,
                           .native_identity_key = std::nullopt,
                           .doc_comment = method.doc_comment});
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
                       .doc_comment = fn.doc_comment});
    }
    if (!include_native) {
        return out;
    }
    const NativeClassDefinitionIndex native_class_index = native_class_definition_index(module);
    for (const NativeTypeDecl& type : module.native_types) {
        out.push_back({.name = type.name,
                       .detail = native_type_detail(native_class_index, type),
                       .location = type.location,
                       .kind = lsp_symbol_kind::Struct,
                       .native_identity_key = native_identity_key(type.identity),
                       .doc_comment = type.doc_comment});
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
             .native_identity_key = native_identity_key(macro.identity)});
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        out.push_back({.name = fn.name,
                       .detail = native_function_detail(fn),
                       .location = fn.location,
                       .kind = lsp_symbol_kind::Function,
                       .native_identity_key = native_identity_key(fn.identity),
                       .doc_comment =
                           combine_doc_comment(fn.doc_comment, native_function_signature_doc(fn))});
    }
    for (const ClassDecl& klass : module.native_classes) {
        out.push_back({.name = klass.name,
                       .detail = "native class " + klass.name,
                       .location = klass.location,
                       .kind = lsp_symbol_kind::Class,
                       .native_identity_key = native_identity_key(klass.identity),
                       .doc_comment = klass.doc_comment});
        for (const FunctionDecl& method : klass.methods) {
            out.push_back({.name = klass.name + "." + method.name,
                           .detail = function_detail(method),
                           .location = method.location,
                           .kind = is_constructor_method_name(method.name)
                                       ? lsp_symbol_kind::Constructor
                                       : lsp_symbol_kind::Method,
                           .native_identity_key = native_identity_key(method.native_identity),
                           .doc_comment = method.doc_comment});
        }
    }
    return out;
}

std::optional<Symbol> exact_symbol_match(const std::vector<Symbol>& symbols,
                                         const std::string& query) {
    std::vector<Symbol> matches;
    for (const Symbol& symbol : symbols) {
        if (symbol.name == query) {
            matches.push_back(symbol);
        }
    }
    return preferred_symbol_match(matches);
}

std::optional<Symbol> unambiguous_suffix_symbol_match(const std::vector<Symbol>& symbols,
                                                      const std::string& query) {
    std::vector<Symbol> matches;
    for (const Symbol& symbol : symbols) {
        if (suffix_symbol_match(symbol.name, query)) {
            matches.push_back(symbol);
        }
    }
    if (matches.size() != 1) {
        return std::nullopt;
    }
    return matches.front();
}

} // namespace dudu
