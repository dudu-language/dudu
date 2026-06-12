#include "dudu/sema_bindings.hpp"

#include "dudu/naming.hpp"

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
                                const std::map<std::string, std::string>& locals) {
    std::set<std::string> seen;
    for (const std::string& name : names) {
        check_local_binding_name(location, name);
        if (!seen.insert(name).second) {
            fail(location, "duplicate destructuring binding: " + name);
        }
        if (locals.contains(name)) {
            fail(location, "destructuring binding shadows local: " + name);
        }
    }
}

} // namespace dudu
