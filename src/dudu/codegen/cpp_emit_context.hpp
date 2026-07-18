#pragma once

#include "dudu/codegen/cpp_emit_options.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

namespace dudu {

struct CppLocalContext {
    std::set<std::string> names;
    std::set<std::string> type_names;
    std::string current_class;
    std::string super_class;
    size_t discard_count = 0;

    std::string bind(std::string name) {
        const std::string emitted = emitted_local_name(name);
        names.insert(std::move(name));
        return emitted;
    }

    bool contains(std::string_view name) const {
        return names.find(std::string(name)) != names.end();
    }

    std::string emitted(std::string_view name) const {
        return contains(name) ? emitted_local_name(name) : std::string(name);
    }

    void bind_type(std::string name) {
        type_names.insert(std::move(name));
    }

    bool contains_type(std::string_view name) const {
        return type_names.find(std::string(name)) != type_names.end();
    }

    std::string bind_discard() {
        std::string name;
        do {
            name = "dudu_internal_discard_" + std::to_string(discard_count++);
        } while (names.contains(name));
        names.insert(name);
        return name;
    }
};

} // namespace dudu
