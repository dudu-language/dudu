#include "dudu/language_server_symbols.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"

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

std::vector<Symbol> symbols_for_document(const Document& doc, bool include_native) {
    std::vector<Symbol> out;
    try {
        ModuleAst module = parse_source(doc.text, doc.path);
        if (include_native) {
            const ProjectConfig config = config_for_file(doc.path);
            merge_native_header_types(module,
                                      {.config = config, .source_dir = doc.path.parent_path()});
        }
        for (const ClassDecl& klass : module.classes) {
            out.push_back({.name = klass.name,
                           .detail = "class " + klass.name,
                           .location = klass.location,
                           .kind = 5});
            for (const FieldDecl& field : klass.fields) {
                out.push_back({.name = field.name,
                               .detail = field.name + ": " + type_ref_text(field.type_ref),
                               .location = field.location,
                               .kind = 8});
            }
            for (const FunctionDecl& method : klass.methods) {
                out.push_back({.name = method.name,
                               .detail = function_detail(method),
                               .location = method.location,
                               .kind = is_constructor_method_name(method.name) ? 9 : 6});
            }
        }
        for (const EnumDecl& en : module.enums) {
            out.push_back({.name = en.name,
                           .detail = "enum " + en.name,
                           .location = en.location,
                           .kind = 10});
        }
        for (const ConstDecl& constant : module.constants) {
            out.push_back({.name = constant.name,
                           .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                           .location = constant.location,
                           .kind = 14});
        }
        for (const FunctionDecl& fn : module.functions) {
            out.push_back({.name = fn.name,
                           .detail = function_detail(fn),
                           .location = fn.location,
                           .kind = 12});
        }
        if (!include_native) {
            return out;
        }
        for (const NativeTypeDecl& type : module.native_types) {
            const bool alias_type = has_type_ref(type.type_ref) || !type.native_spelling.empty();
            out.push_back({.name = type.name,
                           .detail = alias_type
                                         ? "native type = " + native_type_alias_type_text(type)
                                         : "native type",
                           .location = type.location,
                           .kind = 23});
        }
        for (const NativeValueDecl& value : module.native_values) {
            out.push_back({.name = value.name,
                           .detail = value.name + ": " + native_value_type_text(value),
                           .location = value.location,
                           .kind = 14});
        }
        for (const NativeMacroDecl& macro : module.native_macros) {
            out.push_back({.name = macro.name,
                           .detail = native_macro_detail(macro),
                           .location = macro.location,
                           .kind = macro.function_like ? 3 : 14});
        }
        for (const NativeFunctionDecl& fn : module.native_functions) {
            out.push_back({.name = fn.name,
                           .detail = native_function_detail(fn),
                           .location = fn.location,
                           .kind = 12});
        }
        for (const ClassDecl& klass : module.native_classes) {
            out.push_back({.name = klass.name,
                           .detail = "native class " + klass.name,
                           .location = klass.location,
                           .kind = 5});
            for (const FunctionDecl& method : klass.methods) {
                out.push_back({.name = klass.name + "." + method.name,
                               .detail = function_detail(method),
                               .location = method.location,
                               .kind = is_constructor_method_name(method.name) ? 9 : 6});
            }
        }
    } catch (const std::exception&) {
    }
    return out;
}

} // namespace dudu
