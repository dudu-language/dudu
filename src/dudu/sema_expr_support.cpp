#include "dudu/sema_expr_internal.hpp"

namespace dudu {
namespace {

std::vector<std::string> call_args(std::string expr, size_t open) {
    std::string args = trim(expr.substr(open + 1, expr.size() - open - 2));
    return args.empty() ? std::vector<std::string>{} : split_top_level_args(args);
}

} // namespace

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

bool is_builtin_call(const std::string& callee) {
    static const std::set<std::string> builtins = {"delete", "free",  "len",  "max",
                                                   "min",    "print", "range"};
    return builtins.contains(callee);
}
bool is_local_member_call(const FunctionScope& scope, const std::string& callee) {
    const size_t dot = callee.find('.');
    return dot != std::string::npos && scope.locals.contains(trim(callee.substr(0, dot)));
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
    sema_expr_fail(*location, out.str());
}

bool is_comparison_op(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
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

void check_call_args_ast(const FunctionScope& scope, const std::string& callee,
                         const FunctionSignature& signature, const std::vector<Expr>& args,
                         const SourceLocation* location) {
    if (location == nullptr)
        return;
    if ((!signature.variadic && args.size() != signature.params.size()) ||
        (signature.variadic && args.size() < signature.params.size())) {
        sema_expr_fail(*location, "function " + callee + " expects " +
                                      std::to_string(signature.params.size()) + " arguments, got " +
                                      std::to_string(args.size()));
    }
    for (size_t i = 0; i < signature.params.size(); ++i) {
        const std::string& expected = signature.params[i];
        const std::string got = infer_expr_ast(scope, args[i], location);
        if (!can_assign_ast(scope, expected, args[i], got)) {
            sema_expr_fail(*location, "argument " + std::to_string(i + 1) + " for " + callee +
                                          " expects " + expected + ", got " + got);
        }
    }
}

void check_enum_variant_args_ast(const FunctionScope& scope, const EnumDecl& en,
                                 const EnumValueDecl& value, const std::vector<Expr>& args,
                                 const SourceLocation* location) {
    if (location == nullptr) {
        return;
    }
    if (args.size() != value.payload_fields.size()) {
        sema_expr_fail(*location, "enum constructor " + en.name + "." + value.name + " expects " +
                                      std::to_string(value.payload_fields.size()) +
                                      " arguments, got " + std::to_string(args.size()));
    }
    std::set<std::string> seen_named;
    for (size_t i = 0; i < args.size(); ++i) {
        const EnumPayloadField* field = &value.payload_fields[i];
        const Expr* arg = &args[i];
        if (args[i].kind == ExprKind::NamedArg) {
            const auto found = std::find_if(
                value.payload_fields.begin(), value.payload_fields.end(),
                [&](const EnumPayloadField& candidate) { return candidate.name == args[i].name; });
            if (found == value.payload_fields.end()) {
                sema_expr_fail(args[i].location, "unknown enum payload field: " + en.name + "." +
                                                     value.name + "." + args[i].name);
            }
            if (!seen_named.insert(args[i].name).second) {
                sema_expr_fail(args[i].location, "duplicate enum payload field: " + en.name + "." +
                                                     value.name + "." + args[i].name);
            }
            field = &*found;
            arg = &args[i].children.front();
        }
        const std::string got = infer_expr_ast(scope, *arg, location);
        if (!can_assign_ast(scope, field->type, *arg, got)) {
            sema_expr_fail(arg->location, "argument " + std::to_string(i + 1) + " for " + en.name +
                                              "." + value.name + " expects " + field->type +
                                              ", got " + got);
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

bool is_offsetof_field_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Name || expr.kind == ExprKind::StringLiteral) {
        return true;
    }
    if (expr.kind == ExprKind::Member) {
        return member_path_from_expr(expr).has_value();
    }
    return false;
}

} // namespace dudu
