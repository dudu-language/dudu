#include "dudu/sema.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/build_flags.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/escapes.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_bindings.hpp"
#include "dudu/sema_constexpr.hpp"
#include "dudu/sema_constructors.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_native.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/type_compat.hpp"
#include "dudu/unsupported.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
namespace dudu {
namespace {
[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}
std::string infer_cpp_escape_expr(const FunctionScope& scope, std::string expr,
                                  const SourceLocation* location = nullptr);
std::string infer_expr_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* location = nullptr);
void check_call_args_ast(const FunctionScope& scope, const std::string& callee,
                         const FunctionSignature& signature, const std::vector<Expr>& args,
                         const SourceLocation* location);
std::optional<FunctionSignature>
matching_signature_ast(const FunctionScope& scope, const std::vector<FunctionSignature>& options,
                       const std::vector<Expr>& args);
std::vector<std::string> call_args(std::string expr, size_t open) {
    std::string args = trim(expr.substr(open + 1, expr.size() - open - 2));
    return args.empty() ? std::vector<std::string>{} : split_top_level_args(args);
}

std::vector<Expr> call_arg_exprs(std::string expr, size_t open, SourceLocation location) {
    std::vector<Expr> out;
    for (const std::string& arg : call_args(std::move(expr), open)) {
        out.push_back(parse_expr_text(arg, location));
    }
    return out;
}

std::vector<Expr> parse_exprs(const std::vector<std::string>& exprs, SourceLocation location) {
    std::vector<Expr> out;
    for (const std::string& expr : exprs) {
        out.push_back(parse_expr_text(expr, location));
    }
    return out;
}
bool can_assign_ast(const FunctionScope& scope, const std::string& expected, const Expr& expr,
                    const std::string& got) {
    return assignment_type_allowed(expected, expr, got) ||
           assignment_type_allowed(resolve_alias(scope.symbols, expected), expr,
                                   resolve_alias(scope.symbols, got)) ||
           native_base_assignable(scope.symbols, expected, got);
}

std::string assignment_error_ast(const std::string& expected, const Expr& expr,
                                 const std::string& got) {
    return assignment_error(expected, expr, got);
}

bool is_builtin_call(const std::string& callee) {
    static const std::set<std::string> builtins = {"align_up", "delete", "free",  "len",
                                                   "max",      "min",    "print", "range"};
    return builtins.contains(callee);
}
bool is_local_member_call(const FunctionScope& scope, const std::string& callee) {
    const size_t dot = callee.find('.');
    return dot != std::string::npos && scope.locals.contains(trim(callee.substr(0, dot)));
}
bool freestanding_like(const FunctionScope& scope) {
    return scope.target_mode == "freestanding" || scope.target_mode == "embedded";
}

bool is_array_literal(const Expr& expr) {
    return expr.kind == ExprKind::ListLiteral;
}

bool function_has_decorator(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (trim(decorator.text) == name) {
            return true;
        }
    }
    return false;
}

std::string super_base_type(const FunctionScope& scope, const SourceLocation* location) {
    if (scope.current_class.empty()) {
        if (location != nullptr) {
            fail(*location, "super access outside class method");
        }
        return {};
    }
    const auto klass = scope.symbols.classes.find(scope.current_class);
    if (klass == scope.symbols.classes.end()) {
        return {};
    }
    if (klass->second->base_classes.empty()) {
        if (location != nullptr) {
            fail(*location, "super access requires a base class");
        }
        return {};
    }
    if (klass->second->base_classes.size() > 1) {
        if (location != nullptr) {
            fail(*location, "super access is ambiguous with multiple base classes");
        }
        return {};
    }
    return klass->second->base_classes.front();
}

std::string super_init_base_type(const FunctionScope& scope, const SourceLocation* location) {
    if (scope.current_class.empty()) {
        if (location != nullptr) {
            fail(*location, "super access outside class method");
        }
        return {};
    }
    const auto klass = scope.symbols.classes.find(scope.current_class);
    if (klass == scope.symbols.classes.end()) {
        return {};
    }
    if (klass->second->base_classes.empty()) {
        if (location != nullptr) {
            fail(*location, "super access requires a base class");
        }
        return {};
    }
    if (klass->second->base_classes.size() == 1) {
        return klass->second->base_classes.front();
    }
    std::vector<std::string> storage_bases;
    for (const std::string& base : klass->second->base_classes) {
        if (class_type_has_instance_storage(scope.symbols, base)) {
            storage_bases.push_back(base);
        }
    }
    if (storage_bases.size() == 1) {
        return storage_bases.front();
    }
    if (location != nullptr) {
        fail(*location, "super.init requires exactly one storage-bearing base class");
    }
    return {};
}

bool is_super_call(const std::string& callee) {
    return callee == "super" || starts_with(callee, "super.");
}

bool is_super_init_stmt(const Stmt& stmt) {
    return stmt.kind == StmtKind::Expr && stmt.expr.kind == ExprKind::Call &&
           call_callee_text(stmt.expr) == "super.init";
}

std::string infer_super_call_ast(const FunctionScope& scope, const Expr& expr,
                                 const std::string& callee,
                                 const SourceLocation* location) {
    const size_t dot = callee.find('.');
    if (dot == std::string::npos) {
        if (location != nullptr) {
            fail(*location, "super call requires a method name");
        }
        return {};
    }
    const std::string method_name = trim(callee.substr(dot + 1));
    if (method_name.empty()) {
        if (location != nullptr) {
            fail(*location, "super call requires a method name");
        }
        return {};
    }
    if (method_name == "init") {
        if (!scope.allow_super_init) {
            if (location != nullptr) {
                fail(*location, "super.init must be the first statement in init");
            }
            return {};
        }
        const std::string base = super_init_base_type(scope, location);
        if (base.empty()) {
            return {};
        }
        const auto base_class = scope.symbols.classes.find(base_type(base));
        if (base_class == scope.symbols.classes.end()) {
            if (location != nullptr) {
                fail(*location, "unknown base constructor type: " + base);
            }
            return {};
        }
        check_constructor_args_ast(
            scope, *base_class->second, expr.children, location, infer_expr_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            });
        return "void";
    }
    const std::string base = super_base_type(scope, location);
    if (base.empty()) {
        return {};
    }
    FunctionSignature signature;
    if (!method_signature_for_type(scope.symbols, base, method_name, signature, location)) {
        return {};
    }
    const std::vector<FunctionSignature> signatures =
        method_signatures_for_type(scope.symbols, base, method_name);
    if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
        check_call_args_ast(scope, callee, *match, expr.children, location);
        return match->return_type;
    }
    check_call_args_ast(scope, callee, signature, expr.children, location);
    return signature.return_type;
}

void reject_abstract_construction(const Symbols& symbols, const std::string& type,
                                  const SourceLocation* location) {
    if (location == nullptr) {
        return;
    }
    const std::vector<std::string> missing = unimplemented_abstract_methods(symbols, type);
    if (missing.empty()) {
        return;
    }
    std::ostringstream out;
    out << "cannot construct abstract class: " << type << "; missing ";
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << missing[i];
    }
    fail(*location, out.str());
}

const EnumDecl* enum_decl_for_type(const Symbols& symbols, const std::string& type) {
    const std::string resolved = resolve_alias(symbols, type);
    const auto found = symbols.enums.find(resolved);
    return found == symbols.enums.end() ? nullptr : found->second;
}

bool enum_has_payloads(const EnumDecl& en) {
    for (const EnumValueDecl& value : en.values) {
        if (!value.payload_fields.empty()) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> enum_case_variant(const EnumDecl& en, const Stmt& stmt) {
    if (stmt.pattern == "_") {
        return std::string{"_"};
    }
    const std::optional<std::string> path = member_path_from_expr(stmt.pattern_expr);
    if (!path) {
        return std::nullopt;
    }
    const std::string prefix = en.name + ".";
    if (!starts_with(*path, prefix)) {
        return std::nullopt;
    }
    const std::string variant = path->substr(prefix.size());
    if (variant.find('.') != std::string::npos) {
        return std::nullopt;
    }
    return variant;
}

bool enum_contains_variant(const EnumDecl& en, const std::string& variant) {
    for (const EnumValueDecl& value : en.values) {
        if (value.name == variant) {
            return true;
        }
    }
    return false;
}

std::string missing_enum_cases(const EnumDecl& en, const std::set<std::string>& covered) {
    std::ostringstream out;
    bool first = true;
    for (const EnumValueDecl& value : en.values) {
        if (covered.contains(value.name)) {
            continue;
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << en.name << "." << value.name;
    }
    return out.str();
}

bool has_expr(const Expr& expr) {
    return !expr.text.empty();
}

bool is_comparison_op(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

const SourceLocation& node_location(const SourceLocation& fallback, const Expr& expr) {
    return expr.range.end.column > expr.range.start.column ? expr.location : fallback;
}

const SourceLocation& node_location(const SourceLocation& fallback, const TypeRef& type) {
    return type.range.end.column > type.range.start.column ? type.location : fallback;
}

void bind_local(FunctionScope& scope, const std::string& name, const std::string& type,
                const TypeRef& type_ref = {}) {
    scope.locals[name] = type;
    if (type_ref.kind != TypeKind::Unknown || !type_ref.text.empty()) {
        scope.local_type_refs[name] = type_ref;
    } else {
        scope.local_type_refs.erase(name);
    }
}

bool parse_local_function_type(const FunctionScope& scope, const std::string& name,
                               const std::string& type, FunctionSignature& out) {
    std::set<std::string> seen_aliases;
    std::function<bool(const TypeRef&)> parse_ref = [&](const TypeRef& type_ref) -> bool {
        if (parse_function_type(type_ref, out)) {
            return true;
        }
        if (type_ref.kind != TypeKind::Named || !seen_aliases.insert(type_ref.name).second) {
            return false;
        }
        const auto alias = scope.symbols.alias_type_refs.find(type_ref.name);
        return alias != scope.symbols.alias_type_refs.end() && parse_ref(alias->second);
    };
    if (const auto local_type = scope.local_type_refs.find(name);
        local_type != scope.local_type_refs.end() && parse_ref(local_type->second)) {
        return true;
    }
    return parse_function_type(resolve_alias(scope.symbols, type), out);
}

bool foreign_cpp_type_name(const std::string& type) {
    return type.find('.') != std::string::npos || type.find("::") != std::string::npos;
}

void check_call_args_ast(const FunctionScope& scope, const std::string& callee,
                         const FunctionSignature& signature, const std::vector<Expr>& args,
                         const SourceLocation* location) {
    if (location == nullptr)
        return;
    if ((!signature.variadic && args.size() != signature.params.size()) ||
        (signature.variadic && args.size() < signature.params.size())) {
        fail(*location, "function " + callee + " expects " +
                            std::to_string(signature.params.size()) + " arguments, got " +
                            std::to_string(args.size()));
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string got = infer_expr_ast(scope, args[i], location);
        const std::string& expected = signature.params[i];
        if (!can_assign_ast(scope, expected, args[i], got)) {
            fail(*location, "argument " + std::to_string(i + 1) + " for " + callee + " expects " +
                                expected + ", got " + got);
        }
    }
}

bool call_args_match_ast(const FunctionScope& scope, const FunctionSignature& signature,
                         const std::vector<Expr>& args) {
    if ((!signature.variadic && args.size() != signature.params.size()) ||
        (signature.variadic && args.size() < signature.params.size())) {
        return false;
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string got = infer_expr_ast(scope, args[i], nullptr);
        if (!can_assign_ast(scope, signature.params[i], args[i], got)) {
            return false;
        }
    }
    return true;
}

std::optional<FunctionSignature>
matching_signature_ast(const FunctionScope& scope, const std::vector<FunctionSignature>& options,
                       const std::vector<Expr>& args) {
    for (const FunctionSignature& signature : options) {
        if (call_args_match_ast(scope, signature, args)) {
            return signature;
        }
    }
    return std::nullopt;
}

std::string template_call_callee(const Expr& expr) {
    std::ostringstream out;
    out << call_callee_text(expr) << "[";
    if (!expr.template_type_args.empty()) {
        for (size_t i = 0; i < expr.template_type_args.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << expr.template_type_args[i].text;
        }
    } else {
        for (size_t i = 0; i < expr.template_args.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << expr.template_args[i].text;
        }
    }
    out << "]";
    return out.str();
}

std::string template_args_lookup_text(const Expr& expr) {
    std::ostringstream out;
    const size_t count = !expr.template_type_args.empty() ? expr.template_type_args.size()
                                                          : expr.template_args.size();
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << (!expr.template_type_args.empty() ? expr.template_type_args[i].text
                                                 : expr.template_args[i].text);
    }
    return out.str();
}

std::vector<TypeRef> template_type_refs(const Expr& expr) {
    if (!expr.template_type_args.empty()) {
        return expr.template_type_args;
    }
    std::vector<TypeRef> out;
    out.reserve(expr.template_args.size());
    for (const Expr& arg : expr.template_args) {
        out.push_back(parse_type_text(arg.text, arg.location));
    }
    return out;
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string replace_type_parameter(std::string type, const std::string& from,
                                   const std::string& to) {
    if (from.empty() || to.empty()) {
        return type;
    }
    size_t pos = type.find(from);
    while (pos != std::string::npos) {
        const bool left_ok = pos == 0 || !is_identifier_char(type[pos - 1]);
        const size_t end = pos + from.size();
        const bool right_ok = end == type.size() || !is_identifier_char(type[end]);
        if (left_ok && right_ok) {
            type.replace(pos, from.size(), to);
            pos = type.find(from, pos + to.size());
        } else {
            pos = type.find(from, end);
        }
    }
    return type;
}

std::string substitute_generic_type(std::string type, const std::vector<std::string>& params,
                                    const std::vector<TypeRef>& args) {
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        type = replace_type_parameter(std::move(type), params[i], args[i].text);
    }
    return type;
}

bool generic_param_named(const std::vector<std::string>& params, const std::string& name) {
    return std::find(params.begin(), params.end(), name) != params.end();
}

std::string generic_base_name(const std::string& type) {
    const size_t open = type.find('[');
    return open == std::string::npos ? trim(type) : trim(type.substr(0, open));
}

std::vector<std::string> generic_type_args(const std::string& type) {
    const size_t open = type.find('[');
    if (open == std::string::npos || type.empty() || type.back() != ']') {
        return {};
    }
    return split_top_level_args(type.substr(open + 1, type.size() - open - 2));
}

bool infer_generic_binding(const std::string& param_type, const std::string& arg_type,
                           const std::vector<std::string>& params,
                           std::map<std::string, std::string>& bindings,
                           std::string& error) {
    const std::string param = trim(param_type);
    const std::string arg = trim(arg_type);
    if (generic_param_named(params, param)) {
        const auto [it, inserted] = bindings.emplace(param, arg);
        if (!inserted && it->second != arg) {
            error = "conflicting inferred type argument " + param + ": " + it->second + " vs " +
                    arg;
            return false;
        }
        return true;
    }
    if (param.empty() || arg.empty()) {
        return true;
    }
    if ((param.front() == '*' || param.front() == '&') && param.front() == arg.front()) {
        return infer_generic_binding(param.substr(1), arg.substr(1), params, bindings, error);
    }
    const std::string param_base = generic_base_name(param);
    const std::string arg_base = generic_base_name(arg);
    if (param_base != arg_base) {
        return true;
    }
    const std::vector<std::string> param_args = generic_type_args(param);
    const std::vector<std::string> arg_args = generic_type_args(arg);
    if (param_args.empty() || param_args.size() != arg_args.size()) {
        return true;
    }
    for (size_t i = 0; i < param_args.size(); ++i) {
        if (!infer_generic_binding(param_args[i], arg_args[i], params, bindings, error)) {
            return false;
        }
    }
    return true;
}

std::optional<std::vector<TypeRef>>
infer_generic_call_type_args(const FunctionScope& scope, const FunctionDecl& fn,
                             const std::string& callee, const std::vector<Expr>& args,
                             const SourceLocation* location) {
    if (fn.generic_params.empty()) {
        return std::nullopt;
    }
    if (location != nullptr && args.size() != fn.params.size()) {
        fail(*location, "function " + callee + " expects " + std::to_string(fn.params.size()) +
                            " arguments, got " + std::to_string(args.size()));
    }
    if (args.size() != fn.params.size()) {
        return std::nullopt;
    }
    std::map<std::string, std::string> bindings;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const std::string got = infer_expr_ast(scope, args[i], location);
        std::string error;
        if (!infer_generic_binding(fn.params[i].type, got, fn.generic_params, bindings, error)) {
            if (location != nullptr) {
                fail(node_location(*location, args[i]), error + " for " + callee);
            }
            return std::nullopt;
        }
    }
    std::vector<TypeRef> out;
    out.reserve(fn.generic_params.size());
    for (const std::string& param : fn.generic_params) {
        const auto binding = bindings.find(param);
        if (binding == bindings.end() || binding->second.empty() || binding->second == "auto" ||
            binding->second == "list" || binding->second == "dict" || binding->second == "set") {
            if (location != nullptr) {
                fail(*location, "cannot infer type argument " + param + " for " + callee);
            }
            return std::nullopt;
        }
        out.push_back(parse_type_text(binding->second, location == nullptr ? fn.location
                                                                           : *location));
    }
    return out;
}

FunctionSignature instantiate_generic_signature(const FunctionDecl& fn,
                                                const std::vector<TypeRef>& args) {
    FunctionSignature signature;
    signature.return_type =
        substitute_generic_type(fn.return_type.empty() ? "void" : fn.return_type,
                                fn.generic_params, args);
    for (const ParamDecl& param : fn.params) {
        signature.params.push_back(
            substitute_generic_type(param.type, fn.generic_params, args));
    }
    return signature;
}

ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name) {
    klass.name = instantiated_name;
    for (FieldDecl& field : klass.fields) {
        field.type = substitute_generic_type(std::move(field.type), klass.generic_params, args);
    }
    for (ConstDecl& field : klass.static_fields) {
        field.type = substitute_generic_type(std::move(field.type), klass.generic_params, args);
    }
    for (ConstDecl& constant : klass.constants) {
        constant.type = substitute_generic_type(std::move(constant.type), klass.generic_params,
                                                args);
    }
    for (FunctionDecl& method : klass.methods) {
        method.return_type = substitute_generic_type(std::move(method.return_type),
                                                     klass.generic_params, args);
        for (ParamDecl& param : method.params) {
            param.type = substitute_generic_type(std::move(param.type), klass.generic_params, args);
        }
    }
    return klass;
}

std::string join_type_ref_texts(const std::vector<TypeRef>& types) {
    std::ostringstream out;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << types[i].text;
    }
    return out.str();
}

bool is_offsetof_field_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Name || expr.kind == ExprKind::StringLiteral) {
        return true;
    }
    if (expr.kind == ExprKind::Member) {
        return member_path_from_expr(expr).has_value();
    }
    return false;
}

std::string template_method_name(const Expr& expr, const std::string& callee_base,
                                 size_t method_dot) {
    std::ostringstream out;
    out << trim(callee_base.substr(method_dot + 1)) << "[" << template_args_lookup_text(expr)
        << "]";
    return out.str();
}

bool known_template_constructor_type(const FunctionScope& scope, const std::string& callee) {
    const std::string base = base_type(callee);
    if (base.find('.') != std::string::npos || base.find("::") != std::string::npos) {
        return scope.symbols.types.contains(base) || scope.symbols.native_classes.contains(base) ||
               scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
    }
    return known_type(scope.symbols, callee) ||
           scope.symbols.classes.contains(resolve_alias(scope.symbols, callee));
}

std::string normalize_current_class_path(const FunctionScope& scope, const std::string& path,
                                         const SourceLocation* location) {
    if (path == "class" || starts_with(path, "class.")) {
        if (scope.current_class.empty()) {
            if (location != nullptr) {
                fail(*location, "class static access outside class");
            }
            return {};
        }
        return path == "class" ? scope.current_class : scope.current_class + path.substr(5);
    }
    return path;
}

std::string infer_cpp_escape_expr(const FunctionScope& scope, std::string expr,
                                  const SourceLocation* location) {
    expr = trim(std::move(expr));
    if (expr.empty())
        return "void";
    if (starts_with(expr, "{") && expr.back() == '}') {
        for (const std::string& entry : split_top_level(expr.substr(1, expr.size() - 2))) {
            if (find_top_level_char(entry, ':') != std::string::npos)
                return "dict";
        }
        return "set";
    }
    if (starts_with(expr, "lambda "))
        return "lambda";
    const size_t pointer_cast_call = find_call_open(expr);
    if (expr.size() > 1 && expr.front() == '*' && pointer_cast_call != std::string::npos &&
        find_call_close(expr, pointer_cast_call) == expr.size() - 1) {
        const TypeRef type_ref =
            parse_type_text(expr.substr(1, pointer_cast_call - 1),
                            location == nullptr ? SourceLocation{} : *location);
        const std::string type = trim(type_ref.text);
        if (const auto unknown = unknown_type_ref(scope.symbols, type_ref)) {
            if (location != nullptr) {
                const SourceLocation error_location =
                    unknown->second.line > 0 ? unknown->second : *location;
                fail(error_location, "unknown pointer cast type: " + unknown->first);
            }
        } else {
            const std::vector<Expr> args =
                call_arg_exprs(expr, pointer_cast_call,
                               location == nullptr ? SourceLocation{} : *location);
            for (const Expr& arg : args) {
                (void)infer_expr_ast(scope, arg, location);
            }
            return "*" + type;
        }
    }
    if (expr.size() > 1 && expr.front() == '*') {
        const std::string name = trim(expr.substr(1));
        if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
            std::string type = trim(local->second);
            if (!type.empty() && type.front() == '*') {
                return trim(type.substr(1));
            }
        }
    }
    if (expr.size() > 1 && expr.front() == '&') {
        const std::string name = trim(expr.substr(1));
        if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
            return "*" + trim(local->second);
        }
        const std::string value_type = infer_cpp_escape_expr(scope, name, location);
        if (!value_type.empty() && value_type != "void") {
            return "*" + value_type;
        }
    }
    const size_t call = find_call_open(expr);
    if (call != std::string::npos && find_call_close(expr, call) == expr.size() - 1) {
        const std::string callee = trim(expr.substr(0, call));
        const std::vector<Expr> args =
            call_arg_exprs(expr, call, location == nullptr ? SourceLocation{} : *location);
        if (const auto type = infer_raw_allocation_call(scope.symbols, location, callee, args))
            return *type;
        if (is_deallocation_call(callee)) {
            std::vector<std::string> types;
            for (const Expr& arg : args)
                types.push_back(infer_expr_ast(scope, arg, location));
            if (location != nullptr)
                check_deallocation_args(*location, callee, types);
            return "void";
        }
        if (callee == "Ok" || callee == "Err") {
            if (args.size() != 1 && location != nullptr) {
                fail(*location, callee + " expects 1 argument, got " + std::to_string(args.size()));
            }
            return callee + "[" +
                   (args.size() == 1 ? infer_expr_ast(scope, args.front(), location) : "") + "]";
        }
        if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee));
            klass != scope.symbols.classes.end()) {
            check_constructor_args_ast(
                scope, *klass->second, args, location, infer_expr_ast,
                [&](const std::string& expected, const Expr& value, const std::string& got) {
                    return can_assign_ast(scope, expected, value, got);
                });
            return callee;
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args_ast(scope, callee, fn->second, args, location);
            return fn->second.return_type;
        }
        if (const auto signature = native_signature_for_call(
                scope, callee, args, location, infer_expr_ast,
                [&](const std::string& expected, const Expr& value, const std::string& got) {
                    return can_assign_ast(scope, expected, value, got);
                })) {
            return signature->return_type;
        }
        if (!is_local_member_call(scope, callee) && callee.find('.') == std::string::npos &&
            known_type(scope.symbols, callee)) {
            return callee;
        }
        if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
            FunctionSignature signature;
            if (parse_local_function_type(scope, callee, local->second, signature)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature.return_type;
            }
        }
        const size_t method_dot = callee.rfind('.');
        if (method_dot != std::string::npos && is_member_path(callee)) {
            const std::string receiver = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            FunctionSignature signature;
            if (!scope.locals.contains(receiver) && scope.symbols.classes.contains(receiver) &&
                static_method_signature_for_type(scope.symbols, receiver, method_name, signature,
                                                 location)) {
                check_call_args_ast(scope, callee, signature, args, location);
                return signature.return_type;
            }
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, nullptr, receiver, "");
            if (scope.locals.contains(receiver) &&
                foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                for (const Expr& arg : args) {
                    (void)infer_expr_ast(scope, arg, location);
                }
                return "auto";
            }
            if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                          location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type, method_name);
                if (const auto match = matching_signature_ast(scope, signatures, args)) {
                    check_call_args_ast(scope, callee, *match, args, location);
                    return match->return_type;
                }
                if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                    for (const Expr& arg : args) {
                        (void)infer_expr_ast(scope, arg, location);
                    }
                    return "auto";
                }
                check_call_args_ast(scope, callee, signature, args, location);
                return signature.return_type;
            }
        }
        if (method_dot != std::string::npos) {
            const std::string prefix = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            if (const auto local = scope.locals.find(prefix); local != scope.locals.end()) {
                FunctionSignature signature;
                if (method_signature_for_type(scope.symbols, local->second, method_name,
                                              signature, nullptr)) {
                    check_call_args_ast(scope, callee, signature, args, location);
                    return signature.return_type;
                }
            }
            if (const auto local = scope.locals.find(prefix);
                local != scope.locals.end() &&
                foreign_cpp_type_name(resolve_alias(scope.symbols, local->second))) {
                for (const Expr& arg : args) {
                    (void)infer_expr_ast(scope, arg, location);
                }
                return "auto";
            }
            if (scope.symbols.native_import_prefixes.contains(prefix)) {
                for (const Expr& arg : args) {
                    (void)infer_expr_ast(scope, arg, location);
                }
                return "auto";
            }
        }
        if (location != nullptr && callee.find('.') == std::string::npos &&
            callee.find('[') == std::string::npos && is_plain_identifier(callee) &&
            !known_type(scope.symbols, callee) && !is_builtin_call(callee)) {
            if (is_dudu_all_caps(callee))
                return "auto";
            fail(*location, "unknown function: " + callee);
        }
    }
    const std::vector<std::string> tuple_parts = split_top_level(expr);
    if (tuple_parts.size() > 1) {
        std::ostringstream out;
        out << "tuple[";
        for (size_t i = 0; i < tuple_parts.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << infer_cpp_escape_expr(scope, tuple_parts[i], location);
        }
        out << "]";
        return out.str();
    }
    const Expr parsed_expr =
        parse_expr_text(expr, location == nullptr ? SourceLocation{} : *location);
    if (parsed_expr.kind == ExprKind::BoolLiteral) {
        return "bool";
    }
    if (parsed_expr.kind == ExprKind::StringLiteral) {
        return "str";
    }
    if (parsed_expr.kind == ExprKind::Unary && parsed_expr.op == "not") {
        return infer_expr_ast(scope, parsed_expr, location);
    }
    if (parsed_expr.kind == ExprKind::Binary) {
        return infer_expr_ast(scope, parsed_expr, location);
    }
    if (std::isdigit(static_cast<unsigned char>(expr.front())) != 0) {
        return expr.find('.') == std::string::npos ? "i32" : "f64";
    }
    if (expr == "None") {
        return "None";
    }
    const size_t index = expr.find('[');
    if (location != nullptr && index != std::string::npos && expr.back() == ']') {
        const std::string name = trim(expr.substr(0, index));
        const std::string index_expr = expr.substr(index + 1, expr.size() - index - 2);
        if (is_plain_identifier(name)) {
            if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
                if (const auto signature =
                        dudu_operator_signature(scope.symbols, "[]", local->second)) {
                    check_call_args_ast(scope, name + "[]", *signature,
                                        parse_exprs(split_top_level_args(index_expr), *location),
                                        location);
                }
            }
            return indexed_value_type(scope.symbols, scope.locals, *location, name, index_expr,
                                      "indexed access to unknown local: ");
        }
        if (is_member_path(name)) {
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, location, name, "");
            if (!receiver_type.empty()) {
                return indexed_type_from_type(scope.symbols, *location, receiver_type, index_expr,
                                              name);
            }
        }
    }
    const size_t dot = expr.find('.');
    if (dot != std::string::npos && is_member_path(expr)) {
        return member_path_type(scope.symbols, scope.locals, location, expr, "");
    }
    if (const auto local = scope.locals.find(expr); local != scope.locals.end()) {
        return local->second;
    }
    if (const auto fn = scope.symbols.function_signatures.find(expr);
        fn != scope.symbols.function_signatures.end()) {
        return function_type(fn->second);
    }
    if (const auto value = scope.symbols.native_values.find(expr);
        value != scope.symbols.native_values.end()) {
        return value->second;
    }
    if (const auto native = scope.symbols.native_function_signatures.find(expr);
        native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
        return function_type(native->second.front());
    }
    if (is_dudu_all_caps(expr))
        return "i32";
    if (location != nullptr && is_plain_identifier(expr)) {
        fail(*location, "unknown identifier: " + expr);
    }
    return {};
}

std::string infer_template_call_ast(const FunctionScope& scope, const Expr& expr,
                                    const SourceLocation* location) {
    if (expr.template_args.empty() && expr.template_type_args.empty()) {
        if (location != nullptr) {
            fail(*location, "template call expects at least 1 type argument");
        }
        return {};
    }
    const std::string callee = template_call_callee(expr);
    const std::string callee_base = call_callee_text(expr);

    if (starts_with(expr.name, "*")) {
        const size_t arg_count = !expr.template_type_args.empty() ? expr.template_type_args.size()
                                                                  : expr.template_args.size();
        if (location != nullptr && arg_count == 0) {
            fail(*location, "pointer casts expect at least 1 type argument");
        }
        if (location != nullptr && expr.children.size() != 1) {
            fail(*location,
                 "pointer casts expect 1 argument, got " + std::to_string(expr.children.size()));
        }
        const std::string pointee = !expr.template_type_args.empty()
                                        ? trim(expr.name.substr(1)) + "[" +
                                              join_type_ref_texts(expr.template_type_args) + "]"
                                        : trim(callee.substr(1));
        const TypeRef pointee_ref = parse_type_text(pointee, expr.location);
        if (const auto unknown = unknown_type_ref(scope.symbols, pointee_ref)) {
            if (location != nullptr) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : expr.location;
                fail(type_location, "unknown pointer cast type: " + unknown->first);
            }
            return {};
        }
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "*" + pointee_ref.text;
    }

    const auto allocation = infer_allocation_call(scope.symbols, location, expr.name,
                                                 template_type_refs(expr), expr.children.size());
    if (allocation) {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return *allocation;
    }
    if (expr.name == "sizeof" || expr.name == "alignof") {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        const size_t arg_count = type_args.size();
        if (location != nullptr && arg_count != 1) {
            fail(*location,
                 expr.name + " expects 1 type argument, got " + std::to_string(arg_count));
        }
        if (arg_count == 1 && location != nullptr) {
            if (const auto unknown = unknown_type_ref(scope.symbols, type_args.front())) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : type_args.front().location;
                fail(type_location, "unknown " + expr.name + " type: " + unknown->first);
            }
        }
        return "usize";
    }
    if (expr.name == "offsetof") {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        const size_t arg_count = type_args.size();
        if (location != nullptr && arg_count != 1) {
            fail(*location, "offsetof expects 1 type argument, got " + std::to_string(arg_count));
        }
        if (location != nullptr && expr.children.size() != 1) {
            fail(*location,
                 "offsetof expects 1 field argument, got " + std::to_string(expr.children.size()));
        }
        if (location != nullptr && expr.children.size() == 1 &&
            !is_offsetof_field_expr(expr.children.front())) {
            fail(node_location(*location, expr.children.front()),
                 "offsetof field argument must be a field name");
        }
        if (arg_count == 1 && location != nullptr) {
            if (const auto unknown = unknown_type_ref(scope.symbols, type_args.front())) {
                const SourceLocation type_location =
                    unknown->second.line > 0 ? unknown->second : type_args.front().location;
                fail(type_location, "unknown offsetof type: " + unknown->first);
            }
        }
        return "usize";
    }

    if (const auto fn = scope.symbols.function_decls.find(callee_base);
        fn != scope.symbols.function_decls.end() && !fn->second->generic_params.empty()) {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        if (location != nullptr && type_args.size() != fn->second->generic_params.size()) {
            fail(*location, "function " + callee_base + " expects " +
                                std::to_string(fn->second->generic_params.size()) +
                                " type arguments, got " + std::to_string(type_args.size()));
        }
        if (location != nullptr) {
            for (const TypeRef& type_arg : type_args) {
                if (const auto unknown = unknown_type_ref(scope.symbols, type_arg)) {
                    const SourceLocation type_location =
                        unknown->second.line > 0 ? unknown->second : type_arg.location;
                    fail(type_location, "unknown generic argument type: " + unknown->first);
                }
            }
        }
        const FunctionSignature signature = instantiate_generic_signature(*fn->second, type_args);
        check_call_args_ast(scope, callee, signature, expr.children, location);
        return signature.return_type;
    }

    if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee_base));
        klass != scope.symbols.classes.end() && !klass->second->generic_params.empty()) {
        const std::vector<TypeRef> type_args = template_type_refs(expr);
        if (location != nullptr && type_args.size() != klass->second->generic_params.size()) {
            fail(*location, "type " + callee_base + " expects " +
                                std::to_string(klass->second->generic_params.size()) +
                                " type arguments, got " + std::to_string(type_args.size()));
        }
        if (location != nullptr) {
            for (const TypeRef& type_arg : type_args) {
                if (const auto unknown = unknown_type_ref(scope.symbols, type_arg)) {
                    const SourceLocation type_location =
                        unknown->second.line > 0 ? unknown->second : type_arg.location;
                    fail(type_location, "unknown generic argument type: " + unknown->first);
                }
            }
        }
        const ClassDecl instantiated =
            instantiate_generic_class(*klass->second, type_args, callee);
        reject_abstract_construction(scope.symbols, callee_base, location);
        check_constructor_args_ast(
            scope, instantiated, expr.children, location, infer_expr_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            });
        return callee;
    }

    if (const auto signature = native_signature_for_call(
            scope, callee, expr.children, location, infer_expr_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            })) {
        return signature->return_type;
    }
    const size_t method_dot = callee_base.rfind('.');
    if (method_dot != std::string::npos && is_member_path(callee_base)) {
        const std::string receiver = trim(callee_base.substr(0, method_dot));
        const std::string method_name = template_method_name(expr, callee_base, method_dot);
        FunctionSignature signature;
        if (!scope.locals.contains(receiver) && scope.symbols.classes.contains(receiver) &&
            static_method_signature_for_type(scope.symbols, receiver, method_name, signature,
                                             location)) {
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature.return_type;
        }
        const std::string receiver_type =
            member_path_type(scope.symbols, scope.locals, nullptr, receiver, "");
        if (scope.locals.contains(receiver) && (receiver_type.empty() || receiver_type == "auto")) {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_ast(scope, arg, location);
            }
            return "auto";
        }
        if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                      location)) {
            const std::vector<FunctionSignature> signatures =
                method_signatures_for_type(scope.symbols, receiver_type, method_name);
            if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
                check_call_args_ast(scope, callee, *match, expr.children, location);
                return match->return_type;
            }
            if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                for (const Expr& arg : expr.children) {
                    (void)infer_expr_ast(scope, arg, location);
                }
                return "auto";
            }
            check_call_args_ast(scope, callee, signature, expr.children, location);
            return signature.return_type;
        }
        if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_ast(scope, arg, location);
            }
            return "auto";
        }
    }
    if (known_template_constructor_type(scope, callee)) {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return callee;
    }
    if (location != nullptr && callee_base.find('.') == std::string::npos &&
        is_plain_identifier(callee_base) && !known_type(scope.symbols, callee_base)) {
        fail(*location, "unknown function: " + callee);
    }
    if (method_dot != std::string::npos) {
        const std::string prefix = trim(callee_base.substr(0, method_dot));
        if (scope.symbols.native_import_prefixes.contains(prefix)) {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_ast(scope, arg, location);
            }
            return "auto";
        }
        if (location != nullptr) {
            fail(*location, "unknown function: " + callee);
        }
    }
    if (!expr.callee.empty() && expr.callee.front().kind != ExprKind::Name &&
        expr.callee.front().kind != ExprKind::Member) {
        if (location != nullptr) {
            fail(*location, "unsupported template call expression: " + callee_base);
        }
        return {};
    }
    if (location != nullptr) {
        fail(*location, "unknown template call: " + callee);
    }
    return {};
}

std::string infer_constructor_call_ast(const FunctionScope& scope, const Expr& expr,
                                       const std::string& callee, const SourceLocation* location) {
    if (const auto klass = scope.symbols.classes.find(resolve_alias(scope.symbols, callee));
        klass != scope.symbols.classes.end()) {
        reject_abstract_construction(scope.symbols, callee, location);
        check_constructor_args_ast(
            scope, *klass->second, expr.children, location, infer_expr_ast,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return can_assign_ast(scope, expected, value, got);
            });
        return callee;
    }
    for (const Expr& arg : expr.children) {
        (void)infer_expr_ast(scope, arg, location);
    }
    return callee;
}

std::string infer_builtin_call_ast(const FunctionScope& scope, const Expr& expr,
                                   const std::string& callee, const SourceLocation* location) {
    auto check_arity = [&](size_t min, size_t max) {
        if (location != nullptr && (expr.children.size() < min || expr.children.size() > max)) {
            std::ostringstream message;
            message << callee << " expects ";
            if (min == max) {
                message << min;
            } else {
                message << min << " to " << max;
            }
            message << " arguments, got " << expr.children.size();
            fail(*location, message.str());
        }
    };

    if (callee == "len") {
        check_arity(1, 1);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "usize";
    }
    if (callee == "range") {
        check_arity(1, 3);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "range";
    }
    if (callee == "min" || callee == "max") {
        check_arity(2, 2);
        std::string first;
        for (size_t i = 0; i < expr.children.size(); ++i) {
            const std::string got = infer_expr_ast(scope, expr.children[i], location);
            if (i == 0) {
                first = got;
                continue;
            }
            if (location != nullptr && !can_assign_ast(scope, first, expr.children[i], got)) {
                fail(*location, callee + " argument 2 expects " + first + ", got " + got);
            }
        }
        return first.empty() ? "auto" : first;
    }
    if (callee == "align_up") {
        check_arity(2, 2);
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "usize";
    }
    if (callee == "print") {
        for (const Expr& arg : expr.children) {
            (void)infer_expr_ast(scope, arg, location);
        }
        return "void";
    }
    if (is_deallocation_call(callee)) {
        std::vector<std::string> types;
        for (const Expr& arg : expr.children) {
            types.push_back(infer_expr_ast(scope, arg, location));
        }
        if (location != nullptr) {
            check_deallocation_args(*location, callee, types);
        }
        return "void";
    }
    return {};
}

std::optional<std::string> infer_pointer_cast_call_ast(const FunctionScope& scope, const Expr& expr,
                                                       const std::string& callee,
                                                       const SourceLocation* location) {
    if (!starts_with(callee, "*")) {
        return std::nullopt;
    }
    const TypeRef type_ref = parse_type_text(callee.substr(1), expr.location);
    const std::string type = trim(type_ref.text);
    if (const auto unknown = unknown_type_ref(scope.symbols, type_ref)) {
        if (location != nullptr) {
            const SourceLocation error_location =
                unknown->second.line > 0 ? unknown->second : expr.location;
            fail(error_location, "unknown pointer cast type: " + unknown->first);
        }
        return std::nullopt;
    }
    for (const Expr& arg : expr.children) {
        (void)infer_expr_ast(scope, arg, location);
    }
    return "*" + type;
}

std::vector<Expr> index_arg_exprs(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children;
    }
    return {index_expr};
}

bool missing_expr(const Expr& expr) {
    return expr.text.empty() || (expr.kind == ExprKind::Unknown && trim(expr.text).empty());
}

std::string infer_expr_ast(const FunctionScope& scope, const Expr& expr,
                           const SourceLocation* location) {
    const SourceLocation* use_location =
        location != nullptr
            ? location
            : (expr.range.end.column > expr.range.start.column ? &expr.location : nullptr);
    switch (expr.kind) {
    case ExprKind::Unknown:
        if (trim(expr.text).empty()) {
            return {};
        }
        if (use_location != nullptr) {
            fail(*use_location, "unsupported expression: " + trim(expr.text));
        }
        return {};
    case ExprKind::BoolLiteral:
        return "bool";
    case ExprKind::IntLiteral:
        return "i32";
    case ExprKind::FloatLiteral:
        return "f64";
    case ExprKind::StringLiteral:
        return "str";
    case ExprKind::NoneLiteral:
        return "None";
    case ExprKind::Lambda:
        if (use_location != nullptr && expr.children.size() != 1) {
            fail(*use_location, "lambda expression expects ':' body");
        }
        return "lambda";
    case ExprKind::ListLiteral:
        return "list";
    case ExprKind::DictLiteral:
        return "dict";
    case ExprKind::DictEntry:
        return "auto";
    case ExprKind::NamedArg:
        if (expr.children.size() == 1) {
            return infer_expr_ast(scope, expr.children.front(), use_location);
        }
        return "auto";
    case ExprKind::Slice:
        if (use_location != nullptr) {
            fail(*use_location, "slice expression must be used inside an index");
        }
        for (const Expr& child : expr.children) {
            if (!child.text.empty()) {
                (void)infer_expr_ast(scope, child, use_location);
            }
        }
        return "slice";
    case ExprKind::SetLiteral:
        return "set";
    case ExprKind::TupleLiteral: {
        std::ostringstream out;
        out << "tuple[";
        for (size_t i = 0; i < expr.children.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << infer_expr_ast(scope, expr.children[i], use_location);
        }
        out << "]";
        return out.str();
    }
    case ExprKind::Name:
        if (const auto local = scope.locals.find(expr.name); local != scope.locals.end()) {
            return local->second;
        }
        if (const auto fn = scope.symbols.function_signatures.find(expr.name);
            fn != scope.symbols.function_signatures.end()) {
            return function_type(fn->second);
        }
        if (const auto value = scope.symbols.native_values.find(expr.name);
            value != scope.symbols.native_values.end()) {
            return value->second;
        }
        if (const auto native = scope.symbols.native_function_signatures.find(expr.name);
            native != scope.symbols.native_function_signatures.end() && !native->second.empty()) {
            return function_type(native->second.front());
        }
        if (is_dudu_all_caps(expr.name)) {
            return "i32";
        }
        if (use_location != nullptr) {
            fail(*use_location, "unknown identifier: " + expr.name);
        }
        return {};
    case ExprKind::Unary:
        if (expr.children.empty() || missing_expr(expr.children.front())) {
            if (use_location != nullptr) {
                fail(*use_location, "operator " + expr.op + " expects an operand");
            }
            return {};
        }
        if (expr.op == "not") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            if (use_location != nullptr && !got.empty() && got != "bool" && got != "auto") {
                fail(*use_location, "not expects bool, got " + got);
            }
            return "bool";
        }
        if (expr.op == "-") {
            return infer_expr_ast(scope, expr.children.front(), use_location);
        }
        if (expr.op == "*") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            const std::string type = trim(got);
            if (!type.empty() && type.front() == '*') {
                return trim(type.substr(1));
            }
            if (use_location != nullptr && !type.empty() && type != "auto") {
                fail(*use_location, "cannot dereference non-pointer: " + type);
            }
            return {};
        }
        if (expr.op == "&") {
            const std::string got = infer_expr_ast(scope, expr.children.front(), use_location);
            return got.empty() ? std::string{} : "*" + got;
        }
        if (use_location != nullptr) {
            fail(*use_location, "unsupported unary operator: " + expr.op);
        }
        return {};
    case ExprKind::Binary: {
        if (expr.children.size() != 2 || missing_expr(expr.children[0]) ||
            missing_expr(expr.children[1])) {
            if (use_location != nullptr) {
                fail(*use_location, "operator " + expr.op + " expects left and right operands");
            }
            return {};
        }
        const std::string left = infer_expr_ast(scope, expr.children[0], use_location);
        const std::string right = infer_expr_ast(scope, expr.children[1], use_location);
        if (expr.op == "and" || expr.op == "or") {
            if (use_location != nullptr && !left.empty() && left != "bool") {
                fail(*use_location, expr.op + " expects bool, got " + left);
            }
            if (use_location != nullptr && !right.empty() && right != "bool") {
                fail(*use_location, expr.op + " expects bool, got " + right);
            }
            return "bool";
        }
        if (is_comparison_op(expr.op)) {
            if (const auto signature = dudu_operator_signature(scope.symbols, expr.op, left)) {
                if (use_location != nullptr) {
                    if (signature->params.size() != 1) {
                        fail(*use_location, "operator " + expr.op + " expects 1 argument, got " +
                                                std::to_string(signature->params.size()));
                    } else if (!can_assign_ast(scope, signature->params.front(), expr.children[1],
                                               right)) {
                        fail(*use_location, "operator " + expr.op + " expects " +
                                                signature->params.front() + ", got " + right);
                    }
                    if (signature->return_type != "bool") {
                        fail(*use_location, "comparison operator " + expr.op + " must return bool");
                    }
                }
                return "bool";
            }
            if (use_location != nullptr && !left.empty() && !right.empty() &&
                !comparison_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
                fail(*use_location,
                     "comparison " + expr.op + " expects " + left + ", got " + right);
            }
            return "bool";
        }
        if (const auto signature = dudu_operator_signature(scope.symbols, expr.op, left)) {
            if (use_location != nullptr) {
                check_call_args_ast(scope, expr.op, *signature, std::vector<Expr>{expr.children[1]},
                                    use_location);
            }
            return signature->return_type;
        }
        if (use_location != nullptr && !left.empty() && !right.empty() &&
            !binary_rhs_allowed(scope.symbols, expr.op, left, expr.children[1], right)) {
            fail(*use_location, "operator " + expr.op + " expects " + left + ", got " + right);
        }
        return left.empty() ? right : left;
    }
    case ExprKind::Member:
        if (expr.children.size() == 1 && expr.children.front().kind == ExprKind::Name &&
            scope.locals.contains(expr.children.front().name)) {
            const Expr& receiver = expr.children.front();
            const std::string receiver_type = infer_expr_ast(scope, receiver, use_location);
            if (!receiver_type.empty() &&
                !field_type_for_type(scope.symbols, receiver_type, expr.name)) {
                if (const auto swizzle =
                        swizzle_type_for_type(scope.symbols, receiver_type, expr.name)) {
                    return *swizzle;
                }
            }
        }
        if (const std::optional<std::string> path = member_path_from_expr(expr)) {
            return member_path_type(scope.symbols, scope.locals, use_location,
                                    normalize_current_class_path(scope, *path, use_location), "");
        }
        if (expr.children.size() == 1) {
            const Expr& receiver = expr.children.front();
            const std::string receiver_type = infer_expr_ast(scope, receiver, use_location);
            if (receiver_type.empty()) {
                return {};
            }
            if (const auto field = field_type_for_type(scope.symbols, receiver_type, expr.name)) {
                return *field;
            }
            if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                return "auto";
            }
            if (use_location != nullptr) {
                fail(*use_location, "unknown field: " + receiver_type + "." + expr.name);
            }
            return {};
        }
        if (use_location != nullptr) {
            fail(*use_location, "unsupported member expression: " + expr.text);
        }
        return {};
    case ExprKind::Index:
        if (expr.children.size() == 2) {
            if (missing_expr(expr.children[0]) || missing_expr(expr.children[1])) {
                if (use_location != nullptr) {
                    fail(*use_location, "index expression expects receiver and index");
                }
                return {};
            }
            const SourceLocation& index_location =
                use_location != nullptr ? *use_location : expr.location;
            const Expr& receiver = expr.children[0];
            if (receiver.kind == ExprKind::Name) {
                if (const auto local = scope.locals.find(receiver.name);
                    local != scope.locals.end()) {
                    if (const auto signature =
                            dudu_operator_signature(scope.symbols, "[]", local->second)) {
                        check_call_args_ast(scope, receiver.name + "[]", *signature,
                                            index_arg_exprs(expr.children[1]), use_location);
                    }
                }
                return indexed_value_type(scope.symbols, scope.locals, index_location,
                                          receiver.name, expr.children[1],
                                          "indexed access to unknown local: ");
            }
            if (const std::optional<std::string> receiver_path = member_path_from_expr(receiver)) {
                const std::string receiver_type =
                    member_path_type(scope.symbols, scope.locals, use_location,
                                     normalize_current_class_path(scope, *receiver_path,
                                                                  use_location),
                                     "");
                if (!receiver_type.empty()) {
                    return indexed_type_from_type(scope.symbols, index_location, receiver_type,
                                                  expr.children[1],
                                                  normalize_current_class_path(scope,
                                                                               *receiver_path,
                                                                               use_location));
                }
            }
            const std::string receiver_type = infer_expr_ast(scope, receiver, use_location);
            if (!receiver_type.empty()) {
                return indexed_type_from_type(
                    scope.symbols, index_location, receiver_type, expr.children[1],
                    receiver.text.empty() ? "indexed expression" : receiver.text);
            }
        }
        if (use_location != nullptr) {
            fail(*use_location, "unsupported index expression: " + expr.text);
        }
        return {};
    case ExprKind::Conditional:
        if (expr.children.size() != 3 || missing_expr(expr.children[0]) ||
            missing_expr(expr.children[1]) || missing_expr(expr.children[2])) {
            if (use_location != nullptr) {
                fail(*use_location,
                     "conditional expression expects then, condition, and else values");
            }
            return {};
        }
        {
            const std::string condition = infer_expr_ast(scope, expr.children[1], use_location);
            if (use_location != nullptr && !condition.empty() && condition != "bool") {
                fail(*use_location, "condition must be bool, got " + condition);
            }
            const std::string then_type = infer_expr_ast(scope, expr.children[0], use_location);
            const std::string else_type = infer_expr_ast(scope, expr.children[2], use_location);
            return then_type.empty() ? else_type : then_type;
        }
    case ExprKind::Await:
        return {};
    case ExprKind::Yield:
        return {};
    case ExprKind::Call: {
        const std::string callee =
            normalize_current_class_path(scope, call_callee_text(expr), use_location);
        if (callee.empty()) {
            return {};
        }
        if (is_super_call(callee)) {
            return infer_super_call_ast(scope, expr, callee, use_location);
        }
        if (const auto pointer_cast =
                infer_pointer_cast_call_ast(scope, expr, callee, use_location)) {
            return *pointer_cast;
        }
        if (callee == "Ok" || callee == "Err") {
            if (use_location != nullptr && expr.children.size() != 1) {
                fail(*use_location,
                     callee + " expects 1 argument, got " + std::to_string(expr.children.size()));
            }
            return callee + "[" +
                   (expr.children.size() == 1 ? infer_expr_ast(scope, expr.children.front(),
                                                               use_location)
                                              : "") +
                   "]";
        }
        if (const auto generic_fn = scope.symbols.function_decls.find(callee);
            generic_fn != scope.symbols.function_decls.end() &&
            !generic_fn->second->generic_params.empty()) {
            if (const auto type_args =
                    infer_generic_call_type_args(scope, *generic_fn->second, callee,
                                                 expr.children, use_location)) {
                const FunctionSignature signature =
                    instantiate_generic_signature(*generic_fn->second, *type_args);
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
        }
        if (const auto fn = scope.symbols.function_signatures.find(callee);
            fn != scope.symbols.function_signatures.end()) {
            check_call_args_ast(scope, callee, fn->second, expr.children, use_location);
            return fn->second.return_type;
        }
        if (const auto signature = native_signature_for_call(
                scope, callee, expr.children, use_location, infer_expr_ast,
                [&](const std::string& expected, const Expr& value, const std::string& got) {
                    return can_assign_ast(scope, expected, value, got);
                })) {
            return signature->return_type;
        }
        if (known_template_constructor_type(scope, callee)) {
            return infer_constructor_call_ast(scope, expr, callee, use_location);
        }
        if (const auto local = scope.locals.find(callee); local != scope.locals.end()) {
            FunctionSignature signature;
            if (parse_local_function_type(scope, callee, local->second, signature)) {
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
        }
        if (is_builtin_call(callee)) {
            return infer_builtin_call_ast(scope, expr, callee, use_location);
        }
        if (!expr.callee.empty() && expr.callee.front().kind == ExprKind::Lambda) {
            for (const Expr& arg : expr.children) {
                (void)infer_expr_ast(scope, arg, use_location);
            }
            return "auto";
        }
        if (!expr.callee.empty() && expr.callee.front().kind == ExprKind::Member &&
            expr.callee.front().children.size() == 1) {
            const Expr& member = expr.callee.front();
            const Expr& receiver_expr = member.children.front();
            const bool bare_nonlocal_receiver =
                receiver_expr.kind == ExprKind::Name && !scope.locals.contains(receiver_expr.name);
            if (!bare_nonlocal_receiver) {
                const std::string receiver_type =
                    infer_expr_ast(scope, receiver_expr, use_location);
                if ((receiver_type.empty() || receiver_type == "auto") &&
                    receiver_expr.kind == ExprKind::Name &&
                    scope.locals.contains(receiver_expr.name)) {
                    for (const Expr& arg : expr.children) {
                        (void)infer_expr_ast(scope, arg, use_location);
                    }
                    return "auto";
                }
                if (receiver_type.empty()) {
                    return {};
                }
                const bool foreign_receiver =
                    foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type));
                FunctionSignature signature;
                if (method_signature_for_type(scope.symbols, receiver_type, member.name,
                                              signature,
                                              foreign_receiver ? nullptr : use_location)) {
                    const std::vector<FunctionSignature> signatures =
                        method_signatures_for_type(scope.symbols, receiver_type, member.name);
                    if (const auto match =
                            matching_signature_ast(scope, signatures, expr.children)) {
                        check_call_args_ast(scope, callee, *match, expr.children, use_location);
                        return match->return_type;
                    }
                    check_call_args_ast(scope, callee, signature, expr.children, use_location);
                    return signature.return_type;
                }
                if (foreign_receiver) {
                    for (const Expr& arg : expr.children) {
                        (void)infer_expr_ast(scope, arg, use_location);
                    }
                    return "auto";
                }
            }
        }
        const size_t method_dot = callee.rfind('.');
        if (method_dot != std::string::npos && is_member_path(callee)) {
            const std::string receiver = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            FunctionSignature signature;
            if (!scope.locals.contains(receiver) && scope.symbols.classes.contains(receiver) &&
                static_method_signature_for_type(scope.symbols, receiver, method_name, signature,
                                                 use_location)) {
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, nullptr, receiver, "");
            if (scope.locals.contains(receiver) &&
                foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                for (const Expr& arg : expr.children) {
                    (void)infer_expr_ast(scope, arg, use_location);
                }
                return "auto";
            }
            if (method_signature_for_type(scope.symbols, receiver_type, method_name, signature,
                                          use_location)) {
                const std::vector<FunctionSignature> signatures =
                    method_signatures_for_type(scope.symbols, receiver_type, method_name);
                if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
                    check_call_args_ast(scope, callee, *match, expr.children, use_location);
                    return match->return_type;
                }
                if (foreign_cpp_type_name(resolve_alias(scope.symbols, receiver_type))) {
                    for (const Expr& arg : expr.children) {
                        (void)infer_expr_ast(scope, arg, use_location);
                    }
                    return "auto";
                }
                check_call_args_ast(scope, callee, signature, expr.children, use_location);
                return signature.return_type;
            }
        }
        if (method_dot != std::string::npos) {
            const std::string prefix = trim(callee.substr(0, method_dot));
            const std::string method_name = trim(callee.substr(method_dot + 1));
            if (const auto local = scope.locals.find(prefix); local != scope.locals.end()) {
                FunctionSignature signature;
                if (method_signature_for_type(scope.symbols, local->second, method_name,
                                              signature, nullptr)) {
                    check_call_args_ast(scope, callee, signature, expr.children, use_location);
                    return signature.return_type;
                }
            }
            if (const auto local = scope.locals.find(prefix);
                local != scope.locals.end() &&
                foreign_cpp_type_name(resolve_alias(scope.symbols, local->second))) {
                for (const Expr& arg : expr.children) {
                    (void)infer_expr_ast(scope, arg, use_location);
                }
                return "auto";
            }
            if (scope.symbols.native_import_prefixes.contains(prefix)) {
                for (const Expr& arg : expr.children) {
                    (void)infer_expr_ast(scope, arg, use_location);
                }
                return "auto";
            }
            if (use_location != nullptr) {
                fail(*use_location, "unknown function: " + callee);
            }
        } else if (!expr.callee.empty() && expr.callee.front().kind != ExprKind::Name) {
            if (use_location != nullptr) {
                fail(*use_location, "unsupported call expression: " + callee);
            }
            return {};
        }
        if (use_location != nullptr) {
            fail(*use_location, "unknown function: " + callee);
        }
        return {};
    }
    case ExprKind::TemplateCall:
        return infer_template_call_ast(scope, expr, use_location);
    case ExprKind::CppEscape:
        return infer_cpp_escape_expr(scope, expr.text, use_location);
    }
    return {};
}

void check_type_match(const FunctionScope& scope, const std::string& expected, const Expr& expr,
                      const SourceLocation& location) {
    const std::string got = infer_expr_ast(scope, expr, &location);
    if (can_assign_ast(scope, expected, expr, got)) {
        return;
    }
    fail(location, assignment_error_ast(expected, expr, got));
}

void check_array_literal_elements(const FunctionScope& scope, const std::string& element_type,
                                  const Expr& expr, const SourceLocation& location) {
    if (expr.kind == ExprKind::ListLiteral) {
        for (const Expr& child : expr.children) {
            check_array_literal_elements(scope, element_type, child, location);
        }
        return;
    }
    const std::string got = infer_expr_ast(scope, expr, &expr.location);
    if (!can_assign_ast(scope, element_type, expr, got)) {
        fail(location, "array literal element expects " + element_type + ", got " + got);
    }
}

std::string effective_var_type(const Stmt& stmt) {
    const ArrayShapeInference inferred = infer_array_literal_shape_type(stmt.type, stmt.value_expr);
    return inferred.status == ArrayShapeStatus::Inferred ? inferred.type : stmt.type;
}

std::string shape_text(const std::vector<size_t>& shape) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << shape[i];
    }
    out << "]";
    return out.str();
}

void check_condition_type(const FunctionScope& scope, const Stmt& stmt) {
    const SourceLocation& location = node_location(stmt.location, stmt.condition_expr);
    const std::string got = infer_expr_ast(scope, stmt.condition_expr, &location);
    if (!got.empty() && got != "bool" && got != "auto") {
        if (const auto signature = dudu_operator_signature(scope.symbols, "bool", got);
            signature && signature->params.empty() && signature->return_type == "bool") {
            return;
        }
        fail(location, "condition must be bool, got " + got);
    }
}
std::string assign_target_type(const FunctionScope& scope, const Stmt& stmt,
                               const std::string& lhs) {
    const SourceLocation& target_location = node_location(stmt.location, stmt.target_expr);
    if (stmt.target_expr.kind == ExprKind::Unary && stmt.target_expr.op == "*" &&
        stmt.target_expr.children.size() == 1 &&
        stmt.target_expr.children.front().kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.children.front().name;
        const auto local = scope.locals.find(name);
        if (local == scope.locals.end()) {
            fail(target_location, "assignment through unknown local: " + name);
        }
        std::string type = trim(local->second);
        if (type.empty() || type.front() != '*') {
            fail(target_location, "cannot dereference non-pointer: " + name);
        }
        return trim(type.substr(1));
    }
    if (stmt.target_expr.kind == ExprKind::Index && stmt.target_expr.children.size() == 2 &&
        stmt.target_expr.children[0].kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.children[0].name;
        if (const auto local = scope.locals.find(name); local != scope.locals.end()) {
            if (const auto signature =
                    dudu_operator_signature(scope.symbols, "[]=", local->second)) {
                std::vector<Expr> args = index_arg_exprs(stmt.target_expr.children[1]);
                args.push_back(stmt.value_expr);
                check_call_args_ast(scope, name + "[]=", *signature, args, &target_location);
                return {};
            }
        }
        return indexed_value_type(scope.symbols, scope.locals, target_location, name,
                                  stmt.target_expr.children[1],
                                  "indexed assignment to unknown local: ");
    }
    if (stmt.target_expr.kind == ExprKind::Index && stmt.target_expr.children.size() == 2) {
        const Expr& receiver = stmt.target_expr.children[0];
        if (const std::optional<std::string> receiver_path = member_path_from_expr(receiver)) {
            const std::string normalized_receiver =
                normalize_current_class_path(scope, *receiver_path, &target_location);
            const std::string receiver_type =
                member_path_type(scope.symbols, scope.locals, &target_location, normalized_receiver,
                                 "assignment through unknown local: ");
            if (!receiver_type.empty()) {
                return indexed_type_from_type(scope.symbols, target_location, receiver_type,
                                              stmt.target_expr.children[1], normalized_receiver);
            }
        }
    }
    if (stmt.target_expr.kind == ExprKind::Name) {
        const std::string& name = stmt.target_expr.name;
        if (scope.constants.contains(name)) {
            fail(target_location, "cannot assign to constant: " + name);
        }
        const auto local = scope.locals.find(name);
        if (local == scope.locals.end()) {
            fail(target_location, "assignment to unknown local: " + name);
        }
        return local->second;
    }
    if (stmt.target_expr.kind == ExprKind::Member) {
        if (const std::optional<std::string> path = member_path_from_expr(stmt.target_expr)) {
            return member_path_type(scope.symbols, scope.locals, &target_location,
                                    normalize_current_class_path(scope, *path, &target_location),
                                    "assignment through unknown local: ");
        }
        fail(target_location, "unsupported assignment target: " + stmt.target_expr.text);
    }
    if (stmt.target_expr.kind != ExprKind::Unknown && lhs.empty()) {
        fail(target_location, "unsupported assignment target: " + stmt.target_expr.text);
    }
    if (lhs.size() > 1 && lhs.front() == '*') {
        const std::string name = trim(lhs.substr(1));
        const auto local = scope.locals.find(name);
        if (local == scope.locals.end()) {
            fail(target_location, "assignment through unknown local: " + name);
        }
        std::string type = trim(local->second);
        if (type.empty() || type.front() != '*') {
            fail(target_location, "cannot dereference non-pointer: " + name);
        }
        return trim(type.substr(1));
    }
    if (lhs.find('.') == std::string::npos) {
        const size_t index = lhs.find('[');
        if (index != std::string::npos) {
            const std::string name = trim(lhs.substr(0, index));
            const std::string index_expr = lhs.substr(index + 1, lhs.size() - index - 2);
            return indexed_value_type(scope.symbols, scope.locals, target_location, name,
                                      index_expr, "indexed assignment to unknown local: ");
        }
        if (scope.constants.contains(lhs)) {
            fail(target_location, "cannot assign to constant: " + lhs);
        }
        const auto local = scope.locals.find(lhs);
        if (local == scope.locals.end()) {
            fail(target_location, "assignment to unknown local: " + lhs);
        }
        return local->second;
    }
    return member_path_type(scope.symbols, scope.locals, &target_location, lhs,
                            "assignment through unknown local: ");
}
void check_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                int loop_depth);
void check_block(FunctionScope& scope, const std::vector<Stmt>& body,
                 const std::string& return_type, int loop_depth) {
    const bool allow_super_init_at_start = scope.allow_super_init;
    for (size_t i = 0; i < body.size(); ++i) {
        const Stmt& stmt = body[i];
        scope.allow_super_init = allow_super_init_at_start && i == 0 && loop_depth == 0 &&
                                 is_super_init_stmt(stmt);
        check_stmt(scope, stmt, return_type, loop_depth);
    }
    scope.allow_super_init = false;
}
void check_stmt(FunctionScope& scope, const Stmt& stmt, const std::string& return_type,
                int loop_depth) {
    check_local_address_escape(stmt, scope.locals);
    if (stmt.kind == StmtKind::Return) {
        const SourceLocation& value_location = node_location(stmt.location, stmt.value_expr);
        const std::string got = infer_expr_ast(scope, stmt.value_expr, &value_location);
        if (return_type == "void" && got != "void")
            fail(value_location, "void function cannot return " + got);
        if (return_type != "void" && !can_assign_ast(scope, return_type, stmt.value_expr, got)) {
            fail(value_location, "return type mismatch: expected " + return_type + ", got " + got);
        }
        return;
    }
    if (stmt.kind == StmtKind::Assert || stmt.kind == StmtKind::DebugAssert) {
        const bool debug = stmt.kind == StmtKind::DebugAssert;
        if (!debug && freestanding_like(scope)) {
            fail(stmt.location,
                 "runtime assert is not available in " + scope.target_mode +
                     " target mode; use debug_assert or a target-specific assert handler");
        }
        check_condition_type(scope, stmt);
        if (has_expr(stmt.message_expr))
            (void)infer_expr_ast(scope, stmt.message_expr,
                                 &node_location(stmt.location, stmt.message_expr));
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        if (has_expr(stmt.value_expr)) {
            (void)infer_expr_ast(scope, stmt.value_expr,
                                 &node_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::Match) {
        const SourceLocation& subject_location = node_location(stmt.location, stmt.condition_expr);
        const std::string subject_type =
            infer_expr_ast(scope, stmt.condition_expr, &subject_location);
        const EnumDecl* en = enum_decl_for_type(scope.symbols, subject_type);
        if (en == nullptr) {
            fail(subject_location, "match subject must be an enum, got " + subject_type);
        }
        if (enum_has_payloads(*en)) {
            fail(stmt.location, "payload enum match lowering is not implemented: " + en->name);
        }
        std::set<std::string> covered;
        bool wildcard = false;
        for (const Stmt& child : stmt.children) {
            if (child.kind != StmtKind::Case) {
                fail(child.location, "match body expects case statements");
            }
            if (!child.guard.empty()) {
                fail(child.location, "match guards are not implemented");
            }
            if (wildcard) {
                fail(child.location, "unreachable case after wildcard");
            }
            const std::optional<std::string> variant = enum_case_variant(*en, child);
            if (!variant) {
                fail(child.location, "case pattern must be " + en->name + ".Variant or _");
            }
            if (*variant == "_") {
                wildcard = true;
            } else {
                if (!enum_contains_variant(*en, *variant)) {
                    fail(child.location, "unknown enum variant in pattern: " + en->name + "." +
                                             *variant);
                }
                if (!covered.insert(*variant).second) {
                    fail(child.location, "unreachable duplicate case: " + en->name + "." +
                                             *variant);
                }
            }
            check_block(scope, child.children, return_type, loop_depth);
        }
        if (!wildcard && covered.size() != en->values.size()) {
            fail(stmt.location,
                 "non-exhaustive match on " + en->name + "; missing cases: " +
                     missing_enum_cases(*en, covered));
        }
        return;
    }
    if (stmt.kind == StmtKind::Case) {
        fail(stmt.location, "case outside match");
    }
    if (stmt.kind == StmtKind::Delete) {
        std::vector<std::string> arg_types;
        if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
            for (const Expr& child : stmt.value_expr.children) {
                arg_types.push_back(
                    infer_expr_ast(scope, child, &node_location(stmt.location, child)));
            }
        } else {
            arg_types.push_back(infer_expr_ast(scope, stmt.value_expr,
                                               &node_location(stmt.location, stmt.value_expr)));
        }
        check_deallocation_args(stmt.location, "delete", arg_types);
        return;
    }
    if (stmt.kind == StmtKind::CppEscape || stmt.kind == StmtKind::Pass)
        return;
    if ((stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) && loop_depth == 0) {
        fail(stmt.location, std::string(statement_kind_name(stmt.kind)) + " outside loop");
    }
    if (stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) {
        return;
    }
    if (stmt.kind == StmtKind::If) {
        check_condition_type(scope, stmt);
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        check_condition_type(scope, stmt);
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Else) {
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Try) {
        check_block(scope, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        FunctionScope nested = scope;
        if (!stmt.condition.empty())
            fail(stmt.location, "expected except binding as name: Type");
        if (!stmt.name.empty()) {
            check_local_binding_name(stmt.location, stmt.name);
            check_known_type_ref(scope.symbols, node_location(stmt.location, stmt.type_ref),
                                 stmt.type_ref, "unknown catch type: ");
            bind_local(nested, stmt.name, "&const[" + stmt.type + "]",
                       parse_type_text("&const[" + stmt.type + "]", stmt.location));
        }
        check_block(nested, stmt.children, return_type, loop_depth);
        return;
    }
    if (stmt.kind == StmtKind::While) {
        check_condition_type(scope, stmt);
        check_block(scope, stmt.children, return_type, loop_depth + 1);
        return;
    }
    if (stmt.kind == StmtKind::For) {
        FunctionScope nested = scope;
        if (!stmt.name.empty() && !stmt.type.empty() && !stmt.iterable.empty()) {
            check_local_binding_name(stmt.location, stmt.name);
            check_known_type_ref(scope.symbols, node_location(stmt.location, stmt.type_ref),
                                 stmt.type_ref, "unknown loop binding type: ");
            check_iterable_binding(scope.symbols, scope.locals,
                                   node_location(stmt.location, stmt.iterable_expr), stmt.type,
                                   stmt.iterable);
            bind_local(nested, stmt.name, stmt.type, stmt.type_ref);
        }
        check_block(nested, stmt.children, return_type, loop_depth + 1);
        return;
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        const std::string target_type = assign_target_type(scope, stmt, "");
        if (!target_type.empty()) {
            check_type_match(scope, target_type, stmt.value_expr,
                             node_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        check_local_binding_name(stmt.location, stmt.name);
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(stmt.type, stmt.value_expr);
        if (inferred.status == ArrayShapeStatus::EmptyLiteral) {
            fail(node_location(stmt.location, stmt.value_expr),
                 "array shape cannot be inferred from an empty literal");
        }
        if (inferred.status == ArrayShapeStatus::RaggedLiteral) {
            fail(node_location(stmt.location, stmt.value_expr), "ragged array literal");
        }
        const std::vector<size_t> explicit_shape = explicit_array_shape(stmt.type);
        const std::string explicit_element = explicit_array_element_type(stmt.type);
        if (!explicit_shape.empty() && stmt.value_expr.kind == ExprKind::ListLiteral) {
            const ArrayShapeInference actual =
                infer_array_literal_shape_type("array[" + explicit_element + "]", stmt.value_expr);
            if (actual.status == ArrayShapeStatus::RaggedLiteral) {
                fail(node_location(stmt.location, stmt.value_expr), "ragged array literal");
            }
            if (actual.status == ArrayShapeStatus::EmptyLiteral &&
                explicit_shape != std::vector<size_t>{0}) {
                fail(node_location(stmt.location, stmt.value_expr),
                     "array literal shape mismatch: expected " + shape_text(explicit_shape) +
                         ", got [0]");
            }
            if (actual.status == ArrayShapeStatus::Inferred && actual.shape != explicit_shape) {
                fail(node_location(stmt.location, stmt.value_expr),
                     "array literal shape mismatch: expected " + shape_text(explicit_shape) +
                         ", got " + shape_text(actual.shape));
            }
        }
        const std::string type = effective_var_type(stmt);
        if (stmt.type == type) {
            check_known_type_ref(scope.symbols, node_location(stmt.location, stmt.type_ref),
                                 stmt.type_ref, "unknown local type: ");
        } else if (!known_type(scope.symbols, type)) {
            fail(node_location(stmt.location, stmt.type_ref), "unknown local type: " + type);
        }
        if (has_expr(stmt.value_expr)) {
            if (inferred.status == ArrayShapeStatus::Inferred &&
                is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, inferred.element_type, stmt.value_expr,
                                             node_location(stmt.location, stmt.value_expr));
            } else if (!explicit_element.empty() && is_array_literal(stmt.value_expr)) {
                check_array_literal_elements(scope, explicit_element, stmt.value_expr,
                                             node_location(stmt.location, stmt.value_expr));
            } else {
                check_type_match(scope, type, stmt.value_expr,
                                 node_location(stmt.location, stmt.value_expr));
            }
        }
        bind_local(scope, stmt.name, type,
                   stmt.type == type ? stmt.type_ref : parse_type_text(type));
        if (is_dudu_all_caps(stmt.name)) {
            scope.constants.insert(stmt.name);
        }
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        const std::string lhs = stmt.target;
        if (const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
            !names.empty()) {
            const std::vector<std::string> types = tuple_types(
                scope.symbols, infer_expr_ast(scope, stmt.value_expr,
                                              &node_location(stmt.location, stmt.value_expr)));
            if (names.size() != types.size()) {
                fail(node_location(stmt.location, stmt.value_expr),
                     "tuple destructuring count mismatch");
            }
            check_destructure_bindings(stmt.location, names, scope.locals);
            for (size_t i = 0; i < names.size(); ++i) {
                bind_local(scope, names[i], types[i]);
            }
            return;
        }
        if (stmt.target_expr.kind == ExprKind::TupleLiteral) {
            fail(node_location(stmt.location, stmt.target_expr),
                 "tuple destructuring targets must be names");
        }
        if (stmt.target_expr.kind == ExprKind::Name &&
            !scope.locals.contains(stmt.target_expr.name)) {
            const std::string& name = stmt.target_expr.name;
            check_local_binding_name(node_location(stmt.location, stmt.target_expr), name);
            const std::string inferred = infer_expr_ast(
                scope, stmt.value_expr, &node_location(stmt.location, stmt.value_expr));
            bind_local(scope, name, inferred.empty() ? "auto" : inferred);
            if (is_dudu_all_caps(name)) {
                scope.constants.insert(name);
            }
            return;
        }
        const std::string target_type = assign_target_type(scope, stmt, lhs);
        if (!target_type.empty()) {
            check_type_match(scope, target_type, stmt.value_expr,
                             node_location(stmt.location, stmt.value_expr));
        }
        return;
    }
    (void)infer_expr_ast(scope, stmt.expr, &stmt.location);
}

Symbols with_generic_params(Symbols symbols, const std::vector<std::string>& params) {
    for (const std::string& param : params) {
        symbols.types.insert(param);
    }
    return symbols;
}

void copy_base_scope_state(FunctionScope& dst, const FunctionScope& src) {
    dst.locals = src.locals;
    dst.constants = src.constants;
    dst.target_mode = src.target_mode;
    dst.current_class = src.current_class;
    dst.allow_super_init = src.allow_super_init;
    dst.local_type_refs = src.local_type_refs;
}

void check_bodies(const ModuleAst& module, const Symbols& symbols) {
    FunctionScope base{symbols};
    const auto mode = module.build_values.find("TARGET_MODE");
    if (mode != module.build_values.end()) {
        base.target_mode = trim(mode->second);
        if (base.target_mode.size() >= 2 && base.target_mode.front() == '"' &&
            base.target_mode.back() == '"') {
            base.target_mode = base.target_mode.substr(1, base.target_mode.size() - 2);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        bind_local(base, constant.name, constant.type, constant.type_ref);
        base.constants.insert(constant.name);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (function_has_decorator(method, "abstract")) {
                continue;
            }
            Symbols method_symbols = with_generic_params(symbols, klass.generic_params);
            method_symbols = with_generic_params(method_symbols, method.generic_params);
            FunctionScope scope{method_symbols};
            copy_base_scope_state(scope, base);
            scope.current_class = klass.name;
            scope.allow_super_init = method.name == "init";
            for (const ParamDecl& param : method.params) {
                bind_local(scope, param.name, param.type, param.type_ref);
            }
            check_block(scope, method.statements,
                        method.return_type.empty() ? "void" : method.return_type, 0);
            if (!method.return_type.empty() && method.return_type != "void" &&
                !block_guarantees_return(method.statements)) {
                fail(method.location, "missing return in function: " + method.name);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        Symbols function_symbols = with_generic_params(symbols, fn.generic_params);
        FunctionScope scope{function_symbols};
        copy_base_scope_state(scope, base);
        for (const ParamDecl& param : fn.params) {
            bind_local(scope, param.name, param.type, param.type_ref);
        }
        check_block(scope, fn.statements, fn.return_type.empty() ? "void" : fn.return_type, 0);
        if (!fn.return_type.empty() && fn.return_type != "void" &&
            !block_guarantees_return(fn.statements)) {
            fail(fn.location, "missing return in function: " + fn.name);
        }
    }
}
} // namespace
void analyze_module(const ModuleAst& module, SemanticOptions options) {
    const Symbols symbols = collect_symbols(module);
    check_build_flags(module);
    check_naming(module);
    check_unsupported_python(module);
    check_declarations(module, symbols);
    check_constexpr_uses(module);
    if (options.check_bodies)
        check_bodies(module, symbols);
}
} // namespace dudu
