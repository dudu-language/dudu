#include "dudu/lsp/language_server_decorators.hpp"

#include "dudu/core/decorators.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

bool contains_location_text(const SourceLocation& location, std::string_view text,
                            const LspPosition& position) {
    if (text.empty() || location.line != position.line + 1 || location.column <= 0) {
        return false;
    }
    const int start = location.column - 1;
    const int end = start + static_cast<int>(text.size());
    return position.character >= start && position.character <= end;
}

SourceRange decorator_name_range(const Decorator& decorator, std::string_view name) {
    const SourceLocation start = decorator.location;
    SourceLocation end = start;
    end.column += static_cast<int>(1 + name.size());
    return {.start = start, .end = end};
}

std::optional<ExprPath> decorator_path(const Decorator& decorator) {
    const Expr& expr = decorator.expr;
    if ((expr.kind == ExprKind::Call || expr.kind == ExprKind::TemplateCall) &&
        has_expr_callee(expr)) {
        return expr_path_from_expr(expr_callee(expr).front());
    }
    return expr_path_from_expr(expr);
}

std::optional<DecoratorSelection> selection_for_decorator(const Decorator& decorator,
                                                          const LspPosition& position) {
    const std::optional<ExprPath> path = decorator_path(decorator);
    if (!path) {
        return std::nullopt;
    }
    const std::string name = render_expr_path(*path);
    if (!contains_location_text(decorator.location, "@" + name, position)) {
        return std::nullopt;
    }
    SourceLocation name_location = decorator.location;
    name_location.column += 1;
    return DecoratorSelection{.name = name,
                              .detail = "@" + name,
                              .name_location = name_location,
                              .range = decorator_name_range(decorator, name),
                              .path = path};
}

std::optional<DecoratorSelection> selection_for_decorators(
    const std::vector<Decorator>& decorators, const LspPosition& position) {
    for (const Decorator& decorator : decorators) {
        if (const std::optional<DecoratorSelection> selection =
                selection_for_decorator(decorator, position)) {
            return selection;
        }
    }
    return std::nullopt;
}

std::optional<std::string> builtin_decorator_doc(const std::string& name) {
    static const std::map<std::string, std::string> docs = {
        {"abstract", "Marks a method as abstract; subclasses must implement it before instantiation."},
        {"align", "Requests explicit native alignment for a class or value."},
        {"cuda.global", "Marks a function as a CUDA global kernel entry."},
        {"extern_c", "Exports a function with C ABI naming."},
        {"operator", "Marks a method as the implementation of a Dudu operator overload."},
        {"override", "Requires a method to override an inherited virtual or abstract method."},
        {"packed", "Requests packed native layout for a class."},
        {"section", "Places an emitted function or object into a named native section."},
        {"shader.compute", "Marks a function as a compute shader entry point."},
        {"test", "Marks a function as a Dudu test case."},
        {"test.ignore", "Marks a Dudu test case as ignored unless explicitly selected."},
        {"test.should_panic", "Marks a Dudu test case as passing only when it panics."},
        {"virtual", "Marks a method for virtual dispatch."},
        {"workgroup_size", "Sets compute shader workgroup dimensions."},
    };
    const auto found = docs.find(name);
    if (found == docs.end()) {
        return std::nullopt;
    }
    return found->second;
}

} // namespace

std::optional<DecoratorSelection> decorator_selection_at(const ModuleAst& module,
                                                         const Json* params) {
    if (params == nullptr) {
        return std::nullopt;
    }
    const LspPosition position = lsp_position(params);
    for (const ClassDecl& klass : module.classes) {
        if (const std::optional<DecoratorSelection> selection =
                selection_for_decorators(klass.decorators, position)) {
            return selection;
        }
        for (const FunctionDecl& method : klass.methods) {
            if (const std::optional<DecoratorSelection> selection =
                    selection_for_decorators(method.decorators, position)) {
                return selection;
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        if (const std::optional<DecoratorSelection> selection =
                selection_for_decorators(fn.decorators, position)) {
            return selection;
        }
    }
    return std::nullopt;
}

std::optional<Symbol> builtin_decorator_symbol(const DecoratorSelection& selection) {
    const std::optional<std::string> doc = builtin_decorator_doc(selection.name);
    if (!doc) {
        return std::nullopt;
    }
    return Symbol{.name = "@" + selection.name,
                  .detail = selection.detail,
                  .location = selection.name_location,
                  .kind = lsp_symbol_kind::Function,
                  .native_identity_key = std::nullopt,
                  .doc_comment = *doc};
}

} // namespace dudu
