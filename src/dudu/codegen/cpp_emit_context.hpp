#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>

namespace dudu {

struct CppLocalContext {
    std::set<std::string> names;
    std::string current_class;
    std::string super_class;
    size_t discard_count = 0;

    void bind(std::string name) {
        names.insert(std::move(name));
    }

    bool contains(std::string_view name) const {
        return names.find(std::string(name)) != names.end();
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
