#include "dudu/native/native_header_collision.hpp"

#include <set>
#include <string>

namespace dudu {

bool actionable_native_name_collision(std::string_view name) {
    if (name.starts_with("_")) {
        return false;
    }
    static const std::set<std::string, std::less<>> associated_artifacts = {
        "iterator", "const_iterator", "reference", "const_reference", "value_type",
        "pointer",  "const_pointer",  "size_type", "difference_type", "type"};
    if (associated_artifacts.contains(name)) {
        return false;
    }
    return name.find('.') == std::string_view::npos && name.find("::") == std::string_view::npos;
}

bool non_actionable_native_collision_location(const SourceLocation& location) {
    const std::string& file = location.file.str();
    if (file.empty() || file.ends_with(".dd")) {
        return true;
    }
    return file.rfind("/usr/", 0) == 0 || file.rfind("/opt/", 0) == 0;
}

bool native_decl_collision_is_error(std::string_view name, const SourceLocation& location) {
    return actionable_native_name_collision(name) &&
           !non_actionable_native_collision_location(location);
}

} // namespace dudu
