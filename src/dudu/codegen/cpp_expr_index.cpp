#include "dudu/codegen/cpp_expr_index.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_expr_slices.hpp"

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

    std::string out =
        lower_expr(expr.children[0], aliases, locals, local_type_refs, symbols, options);
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
        if (const auto span2_slice =
                lower_strided_span2_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                               local_type_refs, symbols, options)) {
            return *span2_slice;
        }
        if (const auto full_slice =
                lower_full_multidim_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                               local_type_refs, symbols, options)) {
            return *full_slice;
        }
        if (const auto channel_slice =
                lower_channel_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                         local_type_refs, symbols, options)) {
            return *channel_slice;
        }
        if (const auto row_range_slice = lower_leading_range_full_tail_slice_expr(
                expr.children[0], expr.children[1], aliases, locals, local_type_refs, symbols,
                options)) {
            return *row_range_slice;
        }
        if (const auto matrix_patch =
                lower_matrix_patch_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                              local_type_refs, symbols, options)) {
            return *matrix_patch;
        }
        if (const auto column_slice =
                lower_column_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                        local_type_refs, symbols, options)) {
            return *column_slice;
        }
        if (const auto slice =
                lower_trailing_full_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                               local_type_refs, symbols, options)) {
            return *slice;
        }
        if (const auto slice =
                lower_trailing_range_slice_expr(expr.children[0], expr.children[1], aliases, locals,
                                                local_type_refs, symbols, options)) {
            return *slice;
        }
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
