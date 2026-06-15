#pragma once

#include "dudu/ast.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_scope.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct GenericInferCallbacks {
    std::function<std::string(const FunctionScope&, const Expr&, const SourceLocation*)> infer_expr;
    std::function<bool(const FunctionScope&, const std::string&, const Expr&, const std::string&)>
        can_assign;
};

std::string template_args_lookup_text(const Expr& expr);
std::vector<TypeRef> template_type_refs(const Expr& expr);
std::optional<std::vector<TypeRef>>
infer_generic_call_type_args(const FunctionScope& scope, const FunctionDecl& fn,
                             const std::string& callee, const std::vector<Expr>& args,
                             const SourceLocation* location,
                             const GenericInferCallbacks& callbacks);
std::optional<std::vector<TypeRef>>
infer_generic_method_type_args(const FunctionScope& scope, const FunctionDecl& method,
                               const std::string& callee, const std::vector<Expr>& args,
                               size_t first_param, const SourceLocation* location,
                               const GenericInferCallbacks& callbacks);
FunctionSignature instantiate_generic_signature(const FunctionDecl& fn,
                                                const std::vector<TypeRef>& args);
ClassDecl instantiate_generic_class(ClassDecl klass, const std::vector<TypeRef>& args,
                                    const std::string& instantiated_name);
std::string join_type_ref_texts(const std::vector<TypeRef>& types);
std::string template_method_name(const Expr& expr, const std::string& callee_base,
                                 size_t method_dot);
bool known_template_constructor_type(const FunctionScope& scope, const std::string& callee);

} // namespace dudu
