#include "dudu/sema/sema_bindings.hpp"

#include "dudu/core/naming.hpp"

#include <set>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

} // namespace

void check_local_binding_name(const SourceLocation& location, const std::string& name) {
    if (!is_dudu_snake_case(name) && !is_dudu_all_caps(name)) {
        fail(location, "local names must be snake_case or ALL_CAPS: " + name);
    }
}

void check_destructure_bindings(const SourceLocation& location,
                                const std::vector<std::string>& names,
                                const std::map<std::string, TypeRef>& local_type_refs) {
    std::set<std::string> seen;
    for (const std::string& name : names) {
        check_local_binding_name(location, name);
        if (is_discard_binding(name)) {
            continue;
        }
        if (!seen.insert(name).second) {
            fail(location, "duplicate destructuring binding: " + name);
        }
        if (local_type_refs.contains(name)) {
            fail(location, "destructuring binding shadows local: " + name);
        }
    }
}

} // namespace dudu
