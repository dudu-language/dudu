#include "dudu/sema_methods.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_methods_internal.hpp"

#include <optional>
#include <set>
#include <string_view>

namespace dudu {
namespace {

std::optional<std::string_view> swizzle_component_set(const std::string& swizzle) {
    if (swizzle.size() < 2 || swizzle.size() > 4) {
        return std::nullopt;
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
            return set;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> swizzle_type_for_type(const Symbols& symbols,
                                                 const std::string& receiver_type,
                                                 const std::string& swizzle) {
    const auto component_set = swizzle_component_set(swizzle);
    if (!component_set) {
        return std::nullopt;
    }
    const std::string class_name = unwrap_receiver_type(symbols, receiver_type);
    const auto klass = symbols.classes.find(class_name);
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    const TypeRef receiver_type_ref = parse_type_text(receiver_type);
    size_t component_count = 0;
    for (const char ch : *component_set) {
        if (field_type_ref_for_class(symbols, *klass->second, receiver_type_ref,
                                     std::string(1, ch))) {
            ++component_count;
        }
    }
    for (const char ch : swizzle) {
        if (!field_type_ref_for_class(symbols, *klass->second, receiver_type_ref,
                                      std::string(1, ch))) {
            return std::nullopt;
        }
    }
    if (component_count == swizzle.size()) {
        return class_name;
    }
    const std::string_view result_components = component_set->substr(0, swizzle.size());
    for (const auto& [candidate_name, candidate] : symbols.classes) {
        if (candidate_name == class_name || candidate->fields.size() != swizzle.size()) {
            continue;
        }
        bool matches = true;
        for (size_t i = 0; i < result_components.size(); ++i) {
            const std::string result_field(1, result_components[i]);
            const std::string source_field(1, swizzle[i]);
            const auto result_type =
                field_type_ref_for_class(symbols, *candidate, parse_type_text(candidate_name),
                                         result_field);
            const auto source_type =
                field_type_ref_for_class(symbols, *klass->second, receiver_type_ref, source_field);
            if (!result_type || !source_type ||
                substitute_type_ref_text(*result_type, {}) !=
                    substitute_type_ref_text(*source_type, {})) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return candidate_name;
        }
    }
    return std::nullopt;
}

std::optional<std::string> swizzle_assignment_type_for_type(const Symbols& symbols,
                                                            const SourceLocation& location,
                                                            const std::string& receiver_type,
                                                            const std::string& swizzle) {
    const auto component_set = swizzle_component_set(swizzle);
    if (!component_set) {
        return std::nullopt;
    }
    std::set<char> seen;
    for (const char ch : swizzle) {
        if (!seen.insert(ch).second) {
            sema_fail(location, "swizzle assignment cannot repeat component: " + swizzle);
        }
    }
    return swizzle_type_for_type(symbols, receiver_type, swizzle);
}

std::optional<TypeRef> swizzle_assignment_type_ref_for_type(const Symbols& symbols,
                                                            const SourceLocation& location,
                                                            const TypeRef& receiver_type,
                                                            const std::string& swizzle) {
    const auto type =
        swizzle_assignment_type_for_type(symbols, location,
                                         substitute_type_ref_text(receiver_type, {}), swizzle);
    if (!type) {
        return std::nullopt;
    }
    TypeRef result;
    result.kind = TypeKind::Named;
    result.name = *type;
    result.text = *type;
    result.location = location;
    return result;
}

} // namespace dudu
