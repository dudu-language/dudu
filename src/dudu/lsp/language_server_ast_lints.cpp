#include "dudu/lsp/language_server_ast_lints.hpp"

#include "dudu/lsp/language_server_lint_cpp_escape.hpp"
#include "dudu/lsp/language_server_lint_scope.hpp"
#include "dudu/lsp/language_server_lint_suspicious_cast.hpp"
#include "dudu/lsp/language_server_lint_unreachable.hpp"

namespace dudu {

std::vector<Diagnostic> ast_lint_diagnostics(const ModuleAst& module, const Document& doc) {
    std::vector<Diagnostic> out;
    lint_cpp_escape_module(module, doc, out);
    lint_suspicious_cast_module(module, doc, out);
    lint_unreachable_module(module, doc, out);
    lint_scope_module(module, doc, out);
    return out;
}

} // namespace dudu
