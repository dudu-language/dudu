#include "dudu/ast_type.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_methods.hpp"
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

std::optional<TypeRef> swizzle_type_ref_for_type(const Symbols& symbols,
                                                 const TypeRef& receiver_type,
                                                 const std::string& swizzle) {
    const auto component_set = swizzle_component_set(swizzle);
    if (!component_set) {
        return std::nullopt;
    }
    const std::string class_name = receiver_class_name(symbols, receiver_type);
    const ClassDecl* klass = class_for_receiver_type(symbols, receiver_type);
    if (klass == nullptr) {
        return std::nullopt;
    }
    size_t component_count = 0;
    for (const char ch : *component_set) {
        if (field_type_ref_for_class(symbols, *klass, receiver_type, std::string(1, ch))) {
            ++component_count;
        }
    }
    for (const char ch : swizzle) {
        if (!field_type_ref_for_class(symbols, *klass, receiver_type, std::string(1, ch))) {
            return std::nullopt;
        }
    }
    if (component_count == swizzle.size()) {
        return named_type_ref(class_name, receiver_type.location);
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
            const auto result_type = field_type_ref_for_class(
                symbols, *candidate, named_type_ref(candidate_name, receiver_type.location),
                result_field);
            const auto source_type =
                field_type_ref_for_class(symbols, *klass, receiver_type, source_field);
            if (!result_type || !source_type || !type_ref_equivalent(*result_type, *source_type)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return named_type_ref(candidate_name, receiver_type.location);
        }
    }
    return std::nullopt;
}

std::optional<TypeRef> swizzle_assignment_type_ref_for_type(const Symbols& symbols,
                                                            const SourceLocation& location,
                                                            const TypeRef& receiver_type,
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
    return swizzle_type_ref_for_type(symbols, receiver_type, swizzle);
}

} // namespace dudu
