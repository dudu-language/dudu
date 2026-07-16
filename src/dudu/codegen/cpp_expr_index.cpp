#include "dudu/codegen/cpp_expr_index.hpp"

#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_expr_index_hooks.hpp"
#include "dudu/codegen/cpp_expr_slices.hpp"
#include "dudu/core/ast_expr.hpp"

#include <optional>

namespace dudu {
namespace {

struct SliceParts {
    const Expr* start = nullptr;
    const Expr* end = nullptr;
    const Expr* step = nullptr;
};

std::optional<SliceParts> slice_parts(const Expr& expr) {
    if (expr.kind != ExprKind::Slice || expr.children.size() != 2) {
        return std::nullopt;
    }
    SliceParts parts{.start = &expr.children[0], .end = &expr.children[1], .step = nullptr};
    if (expr.children[1].kind == ExprKind::Slice && expr.children[1].children.size() == 2) {
        parts.end = &expr.children[1].children[0];
        parts.step = &expr.children[1].children[1];
    }
    return parts;
}

} // namespace

std::string lower_index_expr(const Expr& expr, const std::vector<std::string>& aliases,
                             const CppLocalContext& locals,
                             const std::map<std::string, TypeRef>& local_type_refs,
                             const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.children.size() != 2) {
        return {};
    }

    if (const std::optional<std::string> hook =
            lower_index_read_hook(expr, aliases, locals, local_type_refs, symbols, options)) {
        return *hook;
    }

    std::string out =
        lower_expr(expr.children[0], aliases, locals, local_type_refs, symbols, options);

    if (const auto generic_view =
            lower_generic_array_view_index_expr(expr.children[0], expr.children[1], aliases, locals,
                                                local_type_refs, symbols, options)) {
        return *generic_view;
    }

    // Fixed-array and array_view slicing belongs above in the generic shape/stride path.
    // The remaining span/strided_span lowerer is only an explicit low-level view helper,
    // not the built-in slice architecture.
    if (const std::optional<SliceParts> slice = slice_parts(expr.children[1])) {
        const std::string start =
            expr_missing(*slice->start)
                ? "0"
                : lower_expr(*slice->start, aliases, locals, local_type_refs, symbols, options);
        const std::string end =
            expr_missing(*slice->end)
                ? "(" + out + ").size()"
                : lower_expr(*slice->end, aliases, locals, local_type_refs, symbols, options);
        if (slice->step != nullptr) {
            const std::string step =
                expr_missing(*slice->step)
                    ? "1"
                    : lower_expr(*slice->step, aliases, locals, local_type_refs, symbols, options);
            return "dudu::StridedSpan{&(" + out + ")[" + start + "], ((" + end + ") - (" + start +
                   ") + (" + step + ") - 1) / (" + step + "), " + step + "}";
        }
        return "std::span(&(" + out + ")[" + start + "], (" + end + ") - (" + start + "))";
    }

    if (expr.children[1].kind == ExprKind::TupleLiteral) {
        for (const Expr& index : expr.children[1].children) {
            out +=
                "[" + lower_expr(index, aliases, locals, local_type_refs, symbols, options) + "]";
        }
        return out;
    }

    return out + "[" +
           lower_expr(expr.children[1], aliases, locals, local_type_refs, symbols, options) + "]";
}

} // namespace dudu
