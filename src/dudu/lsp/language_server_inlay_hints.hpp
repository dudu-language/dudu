#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <string>

namespace dudu {

struct Json;

struct InlayHintOptions {
    bool inferred_types = true;
    bool loop_binding_types = true;
    bool implicit_self = true;
    bool parameter_names = true;
    bool argument_types = false;
};

std::string inlay_hints_json(const Document& doc, const Json* params,
                             InlayHintOptions options = {});

} // namespace dudu
