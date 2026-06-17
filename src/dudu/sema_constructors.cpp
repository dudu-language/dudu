#include "dudu/sema_constructors.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"
#include "dudu/type_compat.hpp"

#include <set>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

std::string constructor_param_type_text(const ConstructorParam& param) {
    return has_type_ref(param.type_ref) ? type_ref_text(param.type_ref) : param.type;
}

bool constructor_arg_assignable(
    const ConstructorParam& expected, const Expr& value, const TypeRef& got_ref,
    const std::string& got,
    const std::function<bool(const std::string&, const Expr&, const std::string&)>& can_assign) {
    if (has_type_ref(expected.type_ref) && has_type_ref(got_ref) &&
        type_assignment_allowed(expected.type_ref, got_ref)) {
        return true;
    }
    return can_assign(constructor_param_type_text(expected), value, got);
}

} // namespace

std::vector<ConstructorParam> constructor_params(const ClassDecl& klass) {
    for (const FunctionDecl& method : klass.methods) {
        if (method.name != "init") {
            continue;
        }
        std::vector<ConstructorParam> out;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            out.push_back({.name = method.params[i].name,
                           .type = type_ref_text(method.params[i].type_ref),
                           .type_ref = method.params[i].type_ref});
        }
        return out;
    }

    std::vector<ConstructorParam> out;
    for (const FieldDecl& field : klass.fields) {
        out.push_back({.name = field.name, .type = field.type, .type_ref = field.type_ref});
    }
    return out;
}

std::vector<ConstructorParam> method_constructor_params(const FunctionDecl& method) {
    std::vector<ConstructorParam> out;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    for (size_t i = first_param; i < method.params.size(); ++i)
        out.push_back({.name = method.params[i].name,
                       .type = type_ref_text(method.params[i].type_ref),
                       .type_ref = method.params[i].type_ref});
    return out;
}

bool positional_constructor_matches_ast(
    const FunctionScope& scope, const std::vector<ConstructorParam>& expected,
    const std::vector<Expr>& args,
    const std::function<TypeRef(const FunctionScope&, const Expr&, const SourceLocation*)>&
        infer_expr_type,
    const std::function<bool(const std::string&, const Expr&, const std::string&)>& can_assign,
    const SourceLocation* location) {
    if (args.size() != expected.size())
        return false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].kind == ExprKind::NamedArg)
            return false;
        const TypeRef got_ref = infer_expr_type(scope, args[i], location);
        const std::string got = substitute_type_ref_text(got_ref, {});
        if (!constructor_arg_assignable(expected[i], args[i], got_ref, got, can_assign))
            return false;
    }
    return true;
}

void check_constructor_args_ast(
    const FunctionScope& scope, const ClassDecl& klass, const std::vector<Expr>& args,
    const SourceLocation* location,
    const std::function<TypeRef(const FunctionScope&, const Expr&, const SourceLocation*)>&
        infer_expr_type,
    const std::function<bool(const std::string&, const Expr&, const std::string&)>& can_assign) {
    if (location == nullptr)
        return;
    bool has_named_arg = false;
    for (const Expr& arg : args)
        has_named_arg = has_named_arg || arg.kind == ExprKind::NamedArg;
    if (!has_named_arg) {
        size_t ctor_count = 0;
        for (const FunctionDecl& method : klass.methods) {
            if (method.name != "init")
                continue;
            ++ctor_count;
            if (positional_constructor_matches_ast(scope, method_constructor_params(method), args,
                                                   infer_expr_type, can_assign, location)) {
                return;
            }
        }
        if (scope.symbols.native_classes.contains(base_type(klass.name))) {
            for (const Expr& arg : args) {
                (void)infer_expr_type(scope, arg, location);
            }
            return;
        }
        if (ctor_count > 1) {
            fail(*location, "no constructor overload of " + klass.name + " accepts " +
                                std::to_string(args.size()) + " arguments");
        }
    }
    const std::vector<ConstructorParam> expected = constructor_params(klass);
    std::set<std::string> named_fields;
    size_t positional = 0;
    for (const Expr& arg : args) {
        if (arg.kind != ExprKind::NamedArg) {
            if (!named_fields.empty())
                fail(*location,
                     "positional constructor argument after named fields: " + klass.name);
            if (positional >= expected.size())
                fail(*location, "constructor " + klass.name + " expects at most " +
                                    std::to_string(expected.size()) + " arguments, got " +
                                    std::to_string(args.size()));
            const ConstructorParam& param = expected[positional];
            const TypeRef got_ref = infer_expr_type(scope, arg, location);
            const std::string got = substitute_type_ref_text(got_ref, {});
            if (!constructor_arg_assignable(param, arg, got_ref, got, can_assign))
                fail(*location, "constructor " + klass.name + " argument " +
                                    std::to_string(positional + 1) + " expects " +
                                    constructor_param_type_text(param) + ", got " + got);
            ++positional;
            continue;
        }
        const std::string& name = arg.name;
        if (!named_fields.insert(name).second)
            fail(*location, "duplicate constructor field: " + name);
        const ConstructorParam* param = nullptr;
        for (const ConstructorParam& candidate : expected)
            if (candidate.name == name)
                param = &candidate;
        if (param == nullptr)
            fail(*location, "unknown constructor field: " + klass.name + "." + name);
        const Expr& value = arg.children.empty() ? arg : arg.children.front();
        const TypeRef got_ref = infer_expr_type(scope, value, location);
        const std::string got = substitute_type_ref_text(got_ref, {});
        if (!constructor_arg_assignable(*param, value, got_ref, got, can_assign))
            fail(*location, "constructor field " + klass.name + "." + name + " expects " +
                                constructor_param_type_text(*param) + ", got " + got);
    }
}

} // namespace dudu
