#include "dudu/cpp_expr_swizzles.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_scope.hpp"

#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

bool is_supported_swizzle(const std::string& swizzle) {
    if (swizzle.size() < 2 || swizzle.size() > 4) {
        return false;
    }
    for (const std::string_view set :
         {std::string_view("xyzw"), std::string_view("rgba"), std::string_view("stpq")}) {
        bool matches = true;
        for (const char ch : swizzle) {
            if (set.find(ch) == std::string_view::npos) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

std::optional<std::string_view> swizzle_component_order(const std::string& swizzle) {
    if (!is_supported_swizzle(swizzle)) {
        return std::nullopt;
    }
    for (const std::string_view set :
         {std::string_view("xyzw"), std::string_view("rgba"), std::string_view("stpq")}) {
        if (set.find(swizzle.front()) != std::string_view::npos) {
            return set.substr(0, swizzle.size());
        }
    }
    return std::nullopt;
}

bool looks_like_local_dudu_class_type(const std::string& type) {
    const std::string trimmed = trim_copy(type);
    return !trimmed.empty() && trimmed.find('.') == std::string::npos &&
           trimmed.find("::") == std::string::npos &&
           std::isupper(static_cast<unsigned char>(trimmed.front())) != 0;
}

std::optional<std::string>
lower_local_swizzle_expr(const Expr& expr, const std::vector<std::string>& aliases,
                         const std::map<std::string, std::string>& locals,
                         const std::map<std::string, TypeRef>& local_type_refs,
                         const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.kind != ExprKind::Member || expr.children.size() != 1 ||
        expr.children.front().kind != ExprKind::Name || !is_supported_swizzle(expr.name)) {
        return std::nullopt;
    }
    const std::string& receiver = expr.children.front().name;
    (void)locals;
    if (!local_type_refs.contains(receiver)) {
        return std::nullopt;
    }
    if (symbols == nullptr) {
        return std::nullopt;
    }
    const TypeRef receiver_type = local_type_ref(local_type_refs, receiver, expr.location);
    if (!looks_like_local_dudu_class_type(type_ref_text(receiver_type))) {
        return std::nullopt;
    }
    const auto result_type = swizzle_type_ref_for_type(*symbols, receiver_type, expr.name);
    if (!result_type) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << lower_cpp_type(*result_type, aliases, options) << "{";
    for (size_t i = 0; i < expr.name.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << receiver << "." << expr.name[i];
    }
    out << "}";
    return out.str();
}

} // namespace

std::optional<std::string> lower_swizzle_expr(const Expr& expr,
                                              const std::vector<std::string>& aliases,
                                              const std::map<std::string, std::string>& locals,
                                              const std::map<std::string, TypeRef>& local_type_refs,
                                              const Symbols* symbols,
                                              const CppEmitOptions& options) {
    if (const auto local =
            lower_local_swizzle_expr(expr, aliases, locals, local_type_refs, symbols, options)) {
        return local;
    }
    if (expr.kind != ExprKind::Member || expr.children.size() != 1 ||
        !is_supported_swizzle(expr.name)) {
        return std::nullopt;
    }
    std::optional<TypeRef> result_type;
    if (symbols != nullptr) {
        const TypeRef receiver_type =
            member_expr_type_ref(*symbols, local_type_refs, nullptr, expr.children.front());
        if (has_type_ref(receiver_type)) {
            result_type = swizzle_type_ref_for_type(*symbols, receiver_type, expr.name);
        }
    }
    if (expr.children.front().kind == ExprKind::Name &&
        local_type_refs.contains(expr.children.front().name) && !result_type) {
        return std::nullopt;
    }
    const std::string receiver =
        lower_expr(expr.children.front(), aliases, locals, local_type_refs, symbols, options);
    if (receiver.empty()) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "([&]() { auto&& __dudu_swizzle_value = " << receiver << "; return "
        << (result_type ? lower_cpp_type(*result_type, aliases, options)
                        : "std::remove_cvref_t<decltype(__dudu_swizzle_value)>")
        << "{";
    for (size_t i = 0; i < expr.name.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "__dudu_swizzle_value." << expr.name[i];
    }
    out << "}; }())";
    return out.str();
}

std::optional<std::string>
lower_swizzle_assignment(const Stmt& stmt, const std::vector<std::string>& aliases,
                         const std::map<std::string, std::string>& locals,
                         const std::map<std::string, TypeRef>& local_type_refs,
                         const Symbols* symbols, const CppEmitOptions& options) {
    if (stmt.target_expr.kind != ExprKind::Member || stmt.target_expr.children.size() != 1) {
        return std::nullopt;
    }
    const Expr& receiver_expr = stmt.target_expr.children.front();
    if (!is_supported_swizzle(stmt.target_expr.name)) {
        return std::nullopt;
    }
    const auto rhs_order = swizzle_component_order(stmt.target_expr.name);
    if (!rhs_order) {
        return std::nullopt;
    }
    std::string receiver =
        lower_expr(receiver_expr, aliases, locals, local_type_refs, symbols, options);
    if (receiver.empty()) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "([&]() { auto&& __dudu_swizzle_rhs = "
        << lower_expr(stmt.value_expr, aliases, locals, local_type_refs, symbols, options) << "; ";
    for (size_t i = 0; i < stmt.target_expr.name.size(); ++i) {
        out << receiver << "." << stmt.target_expr.name[i] << " = __dudu_swizzle_rhs."
            << (*rhs_order)[i] << "; ";
    }
    out << "}())";
    return out.str();
}

} // namespace dudu
