#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_types.hpp"
#include "dudu/sema/sema_context.hpp"

#include <string>
#include <vector>

namespace dudu {

struct InlayLabelPart {
    std::string value;
    std::string tooltip;
    SourceLocation location;
};

struct InlayTypeDetail {
    std::string label;
    std::vector<InlayLabelPart> label_parts;
    std::string tooltip_markdown;
};

InlayTypeDetail inlay_type_detail(const Document& doc, const Symbols& symbols, const TypeRef& type,
                                  std::string prefix);

std::string inlay_label_json(const InlayTypeDetail& detail, const Document& doc);
std::string inlay_tooltip_json(const InlayTypeDetail& detail);

} // namespace dudu
