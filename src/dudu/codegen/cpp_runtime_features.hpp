#pragma once

#include "dudu/core/ast.hpp"

namespace dudu {

struct CppRuntimeFeatures {
    bool algorithm = false;
    bool fixed_array = false;
    bool atomic = false;
    bool assertions = false;
    bool cstdlib = false;
    bool function = false;
    bool hosted_print = false;
    bool optional = false;
    bool span = false;
    bool exceptions = false;
    bool string = false;
    bool string_stream = false;
    bool string_view = false;
    bool unordered_map = false;
    bool unordered_set = false;
    bool variant = false;
    bool vector = false;

    bool result = false;
    bool tuples = false;
    bool indexing = false;
    bool array_view = false;
    bool strided_span = false;
    bool min_max = false;
    bool shader = false;
};

CppRuntimeFeatures cpp_runtime_features(const ModuleAst& module);

} // namespace dudu
