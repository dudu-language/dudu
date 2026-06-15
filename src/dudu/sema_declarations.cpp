#include "dudu/cpp_lower.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/source.hpp"

#include <optional>
#include <set>
#include <string_view>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

void check_supported_type_shape(const SourceLocation& location, const TypeRef& type) {
    if (type.kind == TypeKind::Template && type.name == "tuple" &&
        (type.children.empty() || type.children.size() > 8)) {
        const SourceLocation error_location = type.location.line > 0 ? type.location : location;
        fail(error_location,
             "tuple supports 1 to 8 elements, got " + std::to_string(type.children.size()));
    }
    for (const TypeRef& child : type.children) {
        check_supported_type_shape(location, child);
    }
}

bool has_decorator(const FunctionDecl& fn, std::string_view name) {
    return dudu::has_decorator(fn.decorators, name);
}

bool is_test_decorator(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (decorator_matches(decorator, "test") || decorator_matches(decorator, "test.ignore") ||
            decorator_matches(decorator, "test.should_panic") ||
            decorator_call_matches(decorator, "test.should_panic"))
            return true;
    }
    return false;
}

bool is_virtual_like(const FunctionDecl& fn) {
    return has_decorator(fn, "virtual") || has_decorator(fn, "abstract");
}

std::string decorator_arg(const Decorator& decorator, std::string_view name) {
    return decorator_first_string_arg(decorator, name).value_or("");
}

std::string operator_decorator_arg(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (std::string value = decorator_arg(decorator, "operator"); !value.empty()) {
            return value;
        }
    }
    return {};
}

bool is_operator_method(const FunctionDecl& method) {
    return !operator_decorator_arg(method).empty();
}

bool is_comparison_operator_method(const FunctionDecl& method) {
    static const std::set<std::string> ops = {"==", "!=", "<", "<=", ">", ">="};
    return ops.contains(operator_decorator_arg(method));
}

void check_generic_params(const SourceLocation& location, const std::vector<std::string>& params) {
    std::set<std::string> seen;
    for (const std::string& param : params) {
        if (!seen.insert(param).second) {
            fail(location, "duplicate generic parameter: " + param);
        }
    }
}

Symbols with_generic_params(Symbols symbols, const std::vector<std::string>& params) {
    for (const std::string& param : params) {
        symbols.types.insert(param);
        symbols.generic_params.insert(param);
    }
    return symbols;
}

bool is_constructor_method(const FunctionDecl& method) {
    return method.name == "init";
}

bool is_destructor_method(const FunctionDecl& method) {
    return method.name == "drop";
}

bool is_c_abi_primitive(const std::string& type, bool allow_void) {
    if (type == "void") {
        return allow_void;
    }
    static const std::set<std::string> primitives = {"bool",  "i8",  "i16", "i32", "i64",
                                                     "u8",    "u16", "u32", "u64", "isize",
                                                     "usize", "f32", "f64", "cstr"};
    return primitives.contains(type);
}

bool is_c_abi_type_ref(const TypeRef& type, bool allow_void) {
    const std::string text = trim(type.text);
    if (text.empty() || text == "str" || text.find('.') != std::string::npos) {
        return false;
    }
    if (type.kind == TypeKind::Reference) {
        return false;
    }
    if (type.kind == TypeKind::Pointer && type.children.size() == 1) {
        const TypeRef& child = type.children.front();
        return is_c_abi_type_ref(child, false) || starts_with(trim(child.text), "struct ");
    }
    return is_c_abi_primitive(text, allow_void);
}

void check_extern_c_signature(const FunctionDecl& fn) {
    const std::string return_type = fn.return_type.empty() ? "void" : fn.return_type;
    const TypeRef return_type_ref =
        fn.return_type.empty() ? parse_type_text("void", fn.location) : fn.return_type_ref;
    if (!is_c_abi_type_ref(return_type_ref, true)) {
        fail(fn.location, "@extern_c return type is not C ABI safe: " + return_type);
    }
    for (const ParamDecl& param : fn.params) {
        if (!is_c_abi_type_ref(param.type_ref, false)) {
            fail(param.location, "@extern_c parameter type is not C ABI safe: " + param.type);
        }
    }
}

std::string target_mode(const ModuleAst& module) {
    const auto found = module.build_values.find("TARGET_MODE");
    if (found == module.build_values.end()) {
        return "hosted";
    }
    std::string value = trim(found->second);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

void check_target_decorator_mode(const ModuleAst& module, const Decorator& decorator,
                                 const std::string& text) {
    if (!module.target_mode_explicit) {
        return;
    }
    const std::string mode = target_mode(module);
    if ((text == "cuda.global" || text == "cuda.device" || text == "cuda.host") && mode != "cuda") {
        fail(decorator.location, "@" + text + " requires [target] mode = \"cuda\"");
    }
    if ((text == "shader.compute" || decorator_call_matches(decorator, "workgroup_size")) &&
        mode != "shader") {
        fail(decorator.location, "@" + text + " requires [target] mode = \"shader\"");
    }
}

void check_class_decorator(const Decorator& decorator) {
    const std::string text = decorator_name(decorator);
    if (text == "packed" || decorator_call_matches(decorator, "align")) {
        return;
    }
    fail(decorator.location, "unknown class decorator: @" + text);
}

void check_function_decorator(const ModuleAst& module, const Decorator& decorator) {
    const std::string text = decorator_name(decorator);
    if (text == "inline" || text == "constexpr" || text == "extern_c" || text == "cuda.global" ||
        text == "cuda.device" || text == "cuda.host" || text == "shader.compute" ||
        text == "virtual" || text == "override" || text == "abstract" || text == "test" ||
        text == "test.ignore" || text == "test.should_panic" ||
        decorator_call_matches(decorator, "operator") ||
        decorator_call_matches(decorator, "test.should_panic") ||
        decorator_call_matches(decorator, "workgroup_size") ||
        decorator_call_matches(decorator, "section")) {
        check_target_decorator_mode(module, decorator, text);
        return;
    }
    fail(decorator.location, "unknown function decorator: @" + text);
}

} // namespace

void check_declarations(const ModuleAst& module, const Symbols& symbols) {
    for (const TypeAliasDecl& alias : module.aliases) {
        check_supported_type_shape(alias.location, alias.type_ref);
        check_known_type_ref(symbols, alias.location, alias.type_ref,
                             "unknown type alias target: ");
    }
    for (const EnumDecl& en : module.enums) {
        if (!en.underlying_type.empty()) {
            check_supported_type_shape(en.location, en.underlying_type_ref);
            check_known_type_ref(symbols, en.location, en.underlying_type_ref,
                                 "unknown enum underlying type: ");
        }
        std::set<std::string> values;
        for (const EnumValueDecl& value : en.values) {
            if (!values.insert(value.name).second) {
                fail(value.location, "duplicate enum value: " + value.name);
            }
            std::set<std::string> payload_fields;
            for (const EnumPayloadField& field : value.payload_fields) {
                check_supported_type_shape(field.location, field.type_ref);
                check_known_type_ref(symbols, field.location, field.type_ref,
                                     "unknown enum payload field type: ");
                if (!value.tuple_payload && !payload_fields.insert(field.name).second) {
                    fail(field.location, "duplicate enum payload field: " + en.name + "." +
                                             value.name + "." + field.name);
                }
            }
        }
    }
    for (const ClassDecl& klass : module.classes) {
        check_generic_params(klass.location, klass.generic_params);
        const Symbols class_symbols = with_generic_params(symbols, klass.generic_params);
        for (const Decorator& decorator : klass.decorators) {
            check_class_decorator(decorator);
        }
        std::set<std::string> bases;
        for (const std::string& base : klass.base_classes) {
            const TypeRef base_ref = parse_type_text(base, klass.location);
            check_supported_type_shape(klass.location, base_ref);
            check_known_type_ref(class_symbols, klass.location, base_ref, "unknown base class: ");
            if (!bases.insert(base).second) {
                fail(klass.location, "duplicate base class: " + base);
            }
        }
        check_multiple_inheritance_rules(symbols, klass);
        std::set<std::string> fields;
        for (const FieldDecl& field : klass.fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate field: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(class_symbols, field.location, field.type_ref,
                                 "unknown field type: ");
        }
        for (const ConstDecl& constant : klass.constants) {
            if (!fields.insert(constant.name).second) {
                fail(constant.location, "duplicate class member: " + constant.name);
            }
            check_supported_type_shape(constant.location, constant.type_ref);
            check_known_type_ref(class_symbols, constant.location, constant.type_ref,
                                 "unknown class constant type: ");
        }
        for (const ConstDecl& field : klass.static_fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate class member: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(class_symbols, field.location, field.type_ref,
                                 "unknown static field type: ");
        }
        for (const FunctionDecl& method : klass.methods) {
            check_generic_params(method.location, method.generic_params);
            const Symbols method_symbols =
                with_generic_params(class_symbols, method.generic_params);
            if (!fields.insert(method.name).second) {
                fail(method.location, "duplicate class member: " + method.name);
            }
            const bool is_static = method.params.empty() || method.params.front().name != "self";
            const bool is_abstract = has_decorator(method, "abstract");
            const bool is_virtual = has_decorator(method, "virtual");
            const bool is_override = has_decorator(method, "override");
            if ((is_abstract || is_virtual || is_override) && is_static) {
                fail(method.location, "inheritance method decorators require self");
            }
            if (is_abstract && !method.statements.empty()) {
                fail(method.location, "@abstract methods cannot have a body");
            }
            if (!is_abstract && method.statements.empty()) {
                fail(method.location, "bodyless method requires @abstract: " + method.name);
            }
            if ((is_constructor_method(method) || is_destructor_method(method)) &&
                (method.params.empty() || method.params.front().name != "self")) {
                fail(method.location, method.name + " requires self parameter");
            }
            if (is_operator_method(method)) {
                if (is_static) {
                    fail(method.location, "operator methods cannot be static");
                }
                const std::string op = operator_decorator_arg(method);
                if (op == "bool") {
                    if (method.params.size() != 1 || method.params.front().name != "self") {
                        fail(method.location, "bool operator methods require only self");
                    }
                    if (method.return_type.empty() || method.return_type != "bool") {
                        fail(method.location, "bool operator methods must return bool");
                    }
                } else if (op == "[]=") {
                    if (method.params.size() != 3 || method.params.front().name != "self") {
                        fail(method.location,
                             "indexed assignment operator methods require self, index, and value");
                    }
                    if (!method.return_type.empty() && method.return_type != "void") {
                        fail(method.location,
                             "indexed assignment operator methods must return void");
                    }
                } else if (method.params.size() != 2 || method.params.front().name != "self") {
                    fail(method.location, "operator methods require self and one parameter");
                } else if (is_comparison_operator_method(method) &&
                           (method.return_type.empty() || method.return_type != "bool")) {
                    fail(method.location, "comparison operator methods must return bool");
                }
            }
            if (is_constructor_method(method) && !method.return_type.empty() &&
                method.return_type != "void") {
                fail(method.location, method.name + " cannot declare a return type");
            }
            if (is_destructor_method(method)) {
                if (method.params.size() != 1) {
                    fail(method.location, method.name + " cannot take parameters");
                }
                if (!method.return_type.empty() && method.return_type != "void") {
                    fail(method.location, method.name + " cannot declare a return type");
                }
            }
            if (is_override) {
                std::optional<InheritedMethod> base_method;
                for (const std::string& base : klass.base_classes) {
                    base_method = find_inherited_method(symbols, base, method.name);
                    if (base_method) {
                        break;
                    }
                }
                if (!base_method) {
                    fail(method.location, "@override method has no matching base method: " +
                                              klass.name + "." + method.name);
                }
                if (!same_signature(method_signature_without_self(method),
                                    base_method->signature)) {
                    fail(method.location, "@override signature does not match base method: " +
                                              klass.name + "." + method.name);
                }
                if (!is_virtual_like(*base_method->method)) {
                    fail(method.location, "@override target must be @virtual or @abstract: " +
                                              klass.name + "." + method.name);
                }
            }
            for (const Decorator& decorator : method.decorators) {
                check_function_decorator(module, decorator);
            }
            if (has_decorator(method, "extern_c")) {
                fail(method.location, "@extern_c is only valid on free functions");
            }
            if (is_test_decorator(method)) {
                fail(method.location, "@test is only valid on free functions");
            }
            std::set<std::string> params;
            for (const ParamDecl& param : method.params) {
                if (!params.insert(param.name).second) {
                    fail(param.location, "duplicate parameter: " + param.name);
                }
                check_supported_type_shape(param.location, param.type_ref);
                check_known_type_ref(method_symbols, param.location, param.type_ref,
                                     "unknown parameter type: ");
            }
            if (!method.return_type.empty()) {
                check_supported_type_shape(method.location, method.return_type_ref);
            }
            check_known_type_ref(method_symbols, method.location, method.return_type_ref,
                                 "unknown return type: ");
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        check_generic_params(fn.location, fn.generic_params);
        const Symbols function_symbols = with_generic_params(symbols, fn.generic_params);
        for (const Decorator& decorator : fn.decorators) {
            check_function_decorator(module, decorator);
        }
        if (has_decorator(fn, "virtual") || has_decorator(fn, "override") ||
            has_decorator(fn, "abstract")) {
            fail(fn.location, "inheritance method decorators are only valid on methods");
        }
        if (has_decorator(fn, "extern_c")) {
            check_extern_c_signature(fn);
        }
        if (is_test_decorator(fn)) {
            const std::string return_type = fn.return_type.empty() ? "void" : fn.return_type;
            if (!fn.params.empty()) {
                fail(fn.location, "@test functions cannot take parameters");
            }
            if (return_type != "void" && return_type != "bool" && return_type != "i32") {
                fail(fn.location, "@test return type must be void, bool, or i32");
            }
        }
        std::set<std::string> params;
        for (const ParamDecl& param : fn.params) {
            if (!params.insert(param.name).second) {
                fail(param.location, "duplicate parameter: " + param.name);
            }
            check_supported_type_shape(param.location, param.type_ref);
            check_known_type_ref(function_symbols, param.location, param.type_ref,
                                 "unknown parameter type: ");
        }
        if (!fn.return_type.empty()) {
            check_supported_type_shape(fn.location, fn.return_type_ref);
        }
        check_known_type_ref(function_symbols, fn.location, fn.return_type_ref,
                             "unknown return type: ");
    }
}

} // namespace dudu
