#pragma once

#include <set>
#include <string>
#include <string_view>

namespace dudu {

struct CppLocalContext {
    std::set<std::string> names;
    std::string current_class;
    std::string super_class;

    void bind(std::string name) {
        names.insert(std::move(name));
    }

    bool contains(std::string_view name) const {
        return names.find(std::string(name)) != names.end();
    }
};

} // namespace dudu
