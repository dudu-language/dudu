#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <string>

namespace dudu {

struct Json;

struct LspCallSite {
    std::string name;
    int parameter = 0;
};

LspCallSite lsp_call_site_at(const Document& doc, const Json* params);

} // namespace dudu
