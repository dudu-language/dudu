#include "dudu/sema_inheritance.hpp"

#include <set>

namespace dudu {
namespace {

std::string unwrap_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
            type = trim(type.substr(1));
        }
        bool changed = false;
        for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
            const std::string prefix = std::string(wrapper) + "[";
            if (type.rfind(prefix, 0) == 0 && type.back() == ']') {
                type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
                changed = true;
                break;
            }
        }
        if (!changed) return base_type(type);
    }
}

bool ref_like(std::string type) {
    type = trim(std::move(type));
    return !type.empty() && (type.front() == '*' || type.front() == '&');
}

bool derives_from_impl(const Symbols& symbols, const std::string& derived,
                       const std::string& base, std::set<std::string>& seen) {
    if (derived == base) return true;
    if (!seen.insert(derived).second) return false;
    const auto klass = symbols.classes.find(derived);
    if (klass == symbols.classes.end()) return false;
    for (const std::string& parent : klass->second->base_classes) {
        if (derives_from_impl(symbols, unwrap_type(symbols, parent), base, seen)) return true;
    }
    return false;
}

} // namespace

bool type_derives_from(const Symbols& symbols, const std::string& derived,
                       const std::string& base) {
    std::set<std::string> seen;
    return derives_from_impl(symbols, unwrap_type(symbols, derived), unwrap_type(symbols, base),
                             seen);
}

bool native_base_assignable(const Symbols& symbols, const std::string& expected,
                            const std::string& got) {
    if (!ref_like(expected) && !ref_like(got)) return false;
    const std::string base = unwrap_type(symbols, expected);
    const std::string derived = unwrap_type(symbols, got);
    return base != derived && type_derives_from(symbols, derived, base);
}

} // namespace dudu
