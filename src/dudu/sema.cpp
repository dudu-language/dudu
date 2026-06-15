#include "dudu/sema.hpp"

#include "dudu/build_flags.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_constexpr.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_expr.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/unsupported.hpp"

namespace dudu {

void analyze_module(const ModuleAst& module, SemanticOptions options) {
    const Symbols symbols = collect_symbols(module);
    check_build_flags(module);
    check_naming(module);
    check_unsupported_python(module);
    check_declarations(module, symbols);
    check_constexpr_uses(module);
    if (options.check_bodies) {
        check_bodies(module, symbols, expression_body_check_callbacks());
    }
}

} // namespace dudu
