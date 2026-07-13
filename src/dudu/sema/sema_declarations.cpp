#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/core/naming.hpp"
#include "dudu/core/source.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_inheritance.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

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

bool has_equivalent_base(const std::vector<TypeRef>& bases, const TypeRef& candidate) {
    return std::any_of(bases.begin(), bases.end(),
                       [&](const TypeRef& base) { return type_ref_equivalent(base, candidate); });
}

std::string decorator_arg(const Decorator& decorator, std::string_view name) {
    return decorator_first_string_literal_arg(decorator, name).value_or("");
}

std::string operator_decorator_arg(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (!decorator_call_matches(decorator, "operator")) {
            continue;
        }
        if (decorator.expr.children.size() != 1 ||
            decorator.expr.children.front().kind != ExprKind::StringLiteral) {
            fail(decorator.location, "@operator requires exactly one string literal argument");
        }
        if (std::string value = decorator_arg(decorator, "operator"); !value.empty()) {
            return value;
        }
    }
    return {};
}

void check_single_string_arg(const Decorator& decorator, std::string_view name,
                             std::string_view message) {
    if (decorator_call_matches(decorator, name) &&
        !decorator_has_single_string_literal_arg(decorator, name)) {
        fail(decorator.location, std::string(message));
    }
}

bool is_operator_method(const FunctionDecl& method) {
    return !operator_decorator_arg(method).empty();
}

void check_variadic_params(const FunctionDecl& fn) {
    std::optional<size_t> variadic_index;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (!fn.params[i].variadic) {
            continue;
        }
        if (variadic_index.has_value()) {
            fail(fn.params[i].location, "function can only have one variadic parameter");
        }
        variadic_index = i;
    }
    if (!variadic_index.has_value() || *variadic_index + 1 == fn.params.size()) {
        return;
    }
    const bool allowed_index_set = operator_decorator_arg(fn) == "[]=" &&
                                   *variadic_index + 2 == fn.params.size() &&
                                   fn.params.back().name == "value";
    if (!allowed_index_set) {
        fail(fn.params[*variadic_index].location, "variadic parameter must be last");
    }
}

bool receiver_type_is_reference_to_class(const Symbols& symbols, const TypeRef& type,
                                         std::string_view class_name) {
    TypeRef resolved = resolve_alias_ref(symbols, type);
    if (resolved.kind != TypeKind::Reference || resolved.children.size() != 1) {
        return false;
    }
    TypeRef target = resolve_alias_ref(symbols, resolved.children.front());
    if (target.kind == TypeKind::Const && target.children.size() == 1) {
        target = resolve_alias_ref(symbols, target.children.front());
    }
    return type_ref_head_name(target) == class_name;
}

void check_generic_params(const SourceLocation& location, const std::vector<std::string>& params) {
    std::set<std::string> seen;
    for (size_t i = 0; i < params.size(); ++i) {
        const std::string param = generic_param_base_name(params[i]);
        if (generic_param_is_pack(params[i]) && i + 1 != params.size()) {
            fail(location, "variadic generic parameter must be last: " + param);
        }
        if (!seen.insert(param).second) {
            fail(location, "duplicate generic parameter: " + param);
        }
    }
}

bool function_returns(const FunctionDecl& fn, std::string_view name) {
    return type_ref_is_name(function_return_type_ref(fn), name);
}

bool is_c_abi_primitive(const std::string& type, bool allow_void) {
    if (type == "void") {
        return allow_void;
    }
    static const std::set<std::string> primitives = {"bool",  "char",  "i8",  "i16", "i32",
                                                     "i64",   "u8",    "u16", "u32", "u64",
                                                     "isize", "usize", "f32", "f64", "cstr"};
    return primitives.contains(type);
}

bool is_c_tagged_type_ref(const TypeRef& type) {
    const std::string head = type_ref_head_name(type);
    return starts_with(head, "struct ") || starts_with(head, "union ") ||
           starts_with(head, "enum ");
}

bool is_c_abi_type_ref(const TypeRef& type, bool allow_void) {
    if (!has_type_ref(type)) {
        return false;
    }
    if (type.kind == TypeKind::Reference) {
        return false;
    }
    if (type.kind == TypeKind::Pointer && type.children.size() == 1) {
        const TypeRef& child = type.children.front();
        return is_c_abi_type_ref(child, false) || is_c_tagged_type_ref(child);
    }
    const std::string name = type_ref_head_name(type);
    if (name == "str" || name.find('.') != std::string::npos) {
        return false;
    }
    return is_c_abi_primitive(name, allow_void);
}

void check_extern_c_signature(const FunctionDecl& fn) {
    const TypeRef return_type_ref = function_return_type_ref(fn);
    if (!is_c_abi_type_ref(return_type_ref, true)) {
        fail(fn.location,
             "@extern_c return type is not C ABI safe: " + type_ref_text(return_type_ref));
    }
    for (const ParamDecl& param : fn.params) {
        if (!is_c_abi_type_ref(param.type_ref, false)) {
            fail(param.location,
                 "@extern_c parameter type is not C ABI safe: " + type_ref_text(param.type_ref));
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

void check_class_decorator(const ModuleAst& module, const Decorator& decorator) {
    const std::string text = decorator_name(decorator);
    if (text == "packed" || decorator_call_matches(decorator, "align") ||
        module.resolved_macro_decorators.contains(text)) {
        return;
    }
    fail(decorator.location, "unknown class decorator: @" + text);
}

void check_function_decorator(const ModuleAst& module, const Decorator& decorator) {
    const std::string text = decorator_name(decorator);
    if (module.resolved_macro_decorators.contains(text)) {
        return;
    }
    if (text == "staticmethod") {
        fail(decorator.location,
             "@staticmethod is not supported; omit self for a class-scoped function");
    }
    if (text == "classmethod") {
        fail(decorator.location,
             "@classmethod is not supported; use a class-scoped function or explicit type name");
    }
    if (text == "property") {
        fail(decorator.location, "@property is not supported; use an explicit method");
    }
    if (decorator_call_matches(decorator, "operator")) {
        check_single_string_arg(decorator, "operator",
                                "@operator requires exactly one string literal argument");
    }
    if (decorator_call_matches(decorator, "test.should_panic")) {
        check_single_string_arg(decorator, "test.should_panic",
                                "@test.should_panic requires exactly one string literal argument");
    }
    if (decorator_call_matches(decorator, "section")) {
        check_single_string_arg(decorator, "section",
                                "@section requires exactly one string literal argument");
    }
    if (text == "inline" || text == "constexpr" || text == "extern_c" || text == "macro" ||
        text == "cuda.global" ||
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
        if (has_type_ref(en.underlying_type_ref)) {
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
                if (!payload_fields.insert(field.name).second) {
                    fail(field.location, "duplicate enum payload field: " + en.name + "." +
                                             value.name + "." + field.name);
                }
            }
        }
    }
    for (const ClassDecl& klass : module.classes) {
        check_generic_params(klass.location, klass.generic_params);
        std::optional<Symbols> class_symbol_storage;
        const Symbols* class_symbols = &symbols;
        if (!klass.generic_params.empty()) {
            class_symbol_storage = with_generic_params(symbols, klass.generic_params,
                                                       generic_value_params_for_class(klass));
            class_symbols = &*class_symbol_storage;
        }
        class_symbol_storage = with_self_type(*class_symbols, klass.name);
        class_symbols = &*class_symbol_storage;
        for (const Decorator& decorator : klass.decorators) {
            check_class_decorator(module, decorator);
        }
        std::vector<TypeRef> bases;
        for (const BaseClassDecl& base : klass.base_class_refs) {
            check_supported_type_shape(base.location, base.type_ref);
            check_known_type_ref(*class_symbols, base.location, base.type_ref,
                                 "unknown base class: ");
            const TypeRef resolved_base = resolve_alias_ref(*class_symbols, base.type_ref);
            if (has_equivalent_base(bases, resolved_base)) {
                fail(base.location, "duplicate base class: " + type_ref_text(base.type_ref));
            }
            bases.push_back(resolved_base);
        }
        check_multiple_inheritance_rules(symbols, klass);
        std::set<std::string> fields;
        for (const FieldDecl& field : klass.fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate field: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(*class_symbols, field.location, field.type_ref,
                                 "unknown field type: ");
        }
        for (const ConstDecl& constant : klass.constants) {
            if (!fields.insert(constant.name).second) {
                fail(constant.location, "duplicate class member: " + constant.name);
            }
            check_supported_type_shape(constant.location, constant.type_ref);
            check_known_type_ref(*class_symbols, constant.location, constant.type_ref,
                                 "unknown class constant type: ");
        }
        for (const ConstDecl& field : klass.static_fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate class member: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(*class_symbols, field.location, field.type_ref,
                                 "unknown static field type: ");
        }
        for (const FunctionDecl& method : klass.methods) {
            check_generic_params(method.location, method.generic_params);
            std::optional<Symbols> method_symbol_storage;
            const Symbols* method_symbols = class_symbols;
            if (!method.generic_params.empty()) {
                method_symbol_storage =
                    with_generic_params(*class_symbols, method.generic_params,
                                        generic_value_params_for_function(method));
                method_symbols = &*method_symbol_storage;
            }
            if (fields.contains(method.name)) {
                fail(method.location, "duplicate class member: " + method.name);
            }
            const FunctionSignature method_signature = method_signature_without_self(method);
            for (const FunctionDecl& previous : klass.methods) {
                if (&previous == &method) {
                    break;
                }
                if (previous.name == method.name &&
                    same_signature(method_signature, method_signature_without_self(previous))) {
                    fail(method.location, "duplicate method overload: " + klass.name + "." +
                                              method.name);
                }
            }
            const bool is_static = method.params.empty() || method.params.front().name != "self";
            const bool is_abstract = has_decorator(method, "abstract");
            const bool is_virtual = has_decorator(method, "virtual");
            const bool is_override = has_decorator(method, "override");
            if ((is_abstract || is_virtual || is_override) && is_static) {
                fail(method.location, "inheritance method decorators require self");
            }
            if (!is_static && !receiver_type_is_reference_to_class(
                                  *method_symbols, method.params.front().type_ref, klass.name)) {
                fail(method.params.front().location,
                     "self must be a receiver reference: use self, self: &Self, or "
                     "self: &const[Self]");
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
                    if (!function_returns(method, "bool")) {
                        fail(method.location, "bool operator methods must return bool");
                    }
                } else if (op == "[]") {
                    if (method.params.size() < 2 || method.params.front().name != "self") {
                        fail(method.location,
                             "index operator methods require self and at least one index");
                    }
                } else if (op == "[]=") {
                    if (method.params.size() < 3 || method.params.front().name != "self") {
                        fail(method.location,
                             "indexed assignment operator methods require self, at least one "
                             "index, and value");
                    }
                    if (function_has_return_type(method) && !function_returns(method, "void")) {
                        fail(method.location,
                             "indexed assignment operator methods must return void");
                    }
                } else if (op == "()") {
                    if (method.params.empty() || method.params.front().name != "self") {
                        fail(method.location, "call operator methods require self");
                    }
                } else if (method.params.size() != 2 || method.params.front().name != "self") {
                    fail(method.location, "operator methods require self and one parameter");
                }
            }
            if (is_constructor_method(method) && function_has_return_type(method) &&
                !function_returns(method, "void")) {
                fail(method.location, method.name + " cannot declare a return type");
            }
            if (is_destructor_method(method)) {
                if (method.params.size() != 1) {
                    fail(method.location, method.name + " cannot take parameters");
                }
                if (function_has_return_type(method) && !function_returns(method, "void")) {
                    fail(method.location, method.name + " cannot declare a return type");
                }
            }
            if (is_override) {
                std::optional<InheritedMethod> base_method;
                for (const BaseClassDecl& base : klass.base_class_refs) {
                    base_method = find_inherited_method(symbols, base.type_ref, method.name);
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
            check_variadic_params(method);
            std::set<std::string> params;
            for (const ParamDecl& param : method.params) {
                if (!params.insert(param.name).second) {
                    fail(param.location, "duplicate parameter: " + param.name);
                }
                check_supported_type_shape(param.location, param.type_ref);
                check_known_type_ref(*method_symbols, param.location, param.type_ref,
                                     "unknown parameter type: ");
            }
            if (function_has_return_type(method)) {
                check_supported_type_shape(method.location, method.return_type_ref);
            }
            check_known_type_ref(*method_symbols, method.location, method.return_type_ref,
                                 "unknown return type: ");
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        check_generic_params(fn.location, fn.generic_params);
        std::optional<Symbols> function_symbol_storage;
        const Symbols* function_symbols = &symbols;
        if (!fn.generic_params.empty()) {
            function_symbol_storage = with_generic_params(symbols, fn.generic_params,
                                                          generic_value_params_for_function(fn));
            function_symbols = &*function_symbol_storage;
        }
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
            const TypeRef return_type = function_return_type_ref(fn);
            if (!fn.params.empty()) {
                fail(fn.location, "@test functions cannot take parameters");
            }
            if (!type_ref_is_name(return_type, "void") && !type_ref_is_name(return_type, "bool") &&
                !type_ref_is_name(return_type, "i32")) {
                fail(fn.location, "@test return type must be void, bool, or i32");
            }
        }
        check_variadic_params(fn);
        std::set<std::string> params;
        for (const ParamDecl& param : fn.params) {
            if (!params.insert(param.name).second) {
                fail(param.location, "duplicate parameter: " + param.name);
            }
            check_supported_type_shape(param.location, param.type_ref);
            check_known_type_ref(*function_symbols, param.location, param.type_ref,
                                 "unknown parameter type: ");
        }
        if (function_has_return_type(fn)) {
            check_supported_type_shape(fn.location, fn.return_type_ref);
        }
        check_known_type_ref(*function_symbols, fn.location, fn.return_type_ref,
                             "unknown return type: ");
    }
}

} // namespace dudu
