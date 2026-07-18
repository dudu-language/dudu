#include "dudu/lsp/language_server_member_identity.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_scope.hpp"

namespace dudu {
namespace {

std::optional<std::string> class_at_line(const ModuleAst& module, int one_based_line) {
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            if (function_contains_source_line(method, one_based_line)) {
                return klass.name;
            }
        }
    }
    return std::nullopt;
}

Symbol method_symbol(const DuduMethodInstantiation& method) {
    const std::string owner = method.owner != nullptr
                                  ? method.owner->name
                                  : (method.enum_owner != nullptr ? method.enum_owner->name : "");
    const std::string detail =
        owner.empty() ? function_detail(*method.method)
                      : owner + "." + method.method->name + ": " + function_detail(*method.method);
    return {.name = method.method->name,
            .detail = detail,
            .location = method.method->location,
            .kind = lsp_symbol_kind::Method,
            .native_identity_key = std::nullopt,
            .doc_comment = method.method->doc_comment};
}

std::optional<Symbol> unique_method_symbol(const Symbols& symbols, const TypeRef& receiver_type,
                                           const std::string& member) {
    const std::vector<DuduMethodInstantiation> methods =
        dudu_method_instantiations_for_type(symbols, receiver_type, member, {});
    std::optional<Symbol> result;
    for (const DuduMethodInstantiation& candidate : methods) {
        if (candidate.method == nullptr) {
            continue;
        }
        const Symbol symbol = method_symbol(candidate);
        if (result && !same_member_declaration(result->location, symbol.location)) {
            return std::nullopt;
        }
        result = symbol;
    }
    return result;
}

std::optional<Symbol> selected_method_symbol(const FunctionScope& scope,
                                             const TypeRef& receiver_type,
                                             const std::string& member, const Expr& call,
                                             bool static_receiver) {
    const std::vector<TypeRef> explicit_args = call.kind == ExprKind::TemplateCall
                                                   ? expr_template_type_args(call)
                                                   : std::vector<TypeRef>{};
    if (!static_receiver && explicit_args.empty()) {
        if (const std::optional<DuduMethodInstantiation> inferred =
                inferred_dudu_method_instantiation_for_type(scope, receiver_type, member,
                                                            call.children, nullptr)) {
            if (inferred->method != nullptr) {
                return method_symbol(*inferred);
            }
        }
    }
    const std::vector<DuduMethodInstantiation> methods =
        static_receiver ? dudu_static_method_instantiations_for_type(scope.symbols, receiver_type,
                                                                     member, explicit_args)
                        : dudu_method_instantiations_for_type(scope.symbols, receiver_type, member,
                                                              explicit_args);
    std::vector<FunctionSignature> signatures;
    signatures.reserve(methods.size());
    for (const DuduMethodInstantiation& method : methods) {
        signatures.push_back(method.signature);
    }
    const std::optional<size_t> selected =
        matching_signature_index_ast(scope, signatures, call.children);
    if (!selected || methods[*selected].method == nullptr) {
        return std::nullopt;
    }
    return method_symbol(methods[*selected]);
}

std::optional<Symbol> member_symbol_for_type(const ModuleAst& module, const Symbols& symbols,
                                             const TypeRef& receiver_type,
                                             const std::string& member) {
    if (const std::optional<Symbol> method = unique_method_symbol(symbols, receiver_type, member)) {
        return method;
    }
    if (!dudu_method_instantiations_for_type(symbols, receiver_type, member, {}).empty()) {
        return std::nullopt;
    }
    std::optional<Symbol> result;
    for (const std::string& owner : member_candidate_types(module, receiver_type)) {
        std::optional<Symbol> candidate = class_member_symbol_for_owner(module, owner, member);
        if (!candidate) {
            continue;
        }
        if (result && !same_member_declaration(result->location, candidate->location)) {
            return std::nullopt;
        }
        candidate->detail = owner + "." + candidate->detail;
        result = candidate;
    }
    return result;
}

} // namespace

bool same_member_declaration(const SourceLocation& left, const SourceLocation& right) {
    if (left.line != right.line || left.column != right.column) {
        return false;
    }
    if (left.file.empty() || right.file.empty()) {
        return left.file.empty() && right.file.empty();
    }
    return same_path(left.file.str(), right.file.str());
}

std::optional<Symbol> dudu_member_symbol_for_expr(const ModuleAst& module, const Expr& member_expr,
                                                  const SourceLocation& use_location,
                                                  const Expr* call_expr) {
    if (member_expr.kind != ExprKind::Member || member_expr.children.size() != 1) {
        return std::nullopt;
    }
    const Expr& receiver_expr = member_expr.children.front();
    if (receiver_expr.kind == ExprKind::Name && receiver_expr.name == "super") {
        return class_member_symbol_for_super(module, use_location.line, member_expr.name);
    }
    try {
        Symbols symbols = collect_symbols(module);
        FunctionScope scope(symbols);
        scope.local_type_refs = local_type_refs_before_location(module, use_location);
        if (const std::optional<std::string> current_class =
                class_at_line(module, use_location.line)) {
            scope.current_class = *current_class;
        }
        const std::optional<TypeRef> static_type =
            static_class_receiver_type_ref(scope, receiver_expr);
        const TypeRef receiver_type =
            static_type.value_or(infer_expr_type_ast(scope, receiver_expr, nullptr));
        if (!has_type_ref(receiver_type)) {
            return std::nullopt;
        }
        if (call_expr != nullptr) {
            return selected_method_symbol(scope, receiver_type, member_expr.name, *call_expr,
                                          static_type.has_value());
        }
        return member_symbol_for_type(module, scope.symbols, receiver_type, member_expr.name);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace dudu
