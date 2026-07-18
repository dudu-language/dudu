#include "dudu/lsp/language_server_documentation.hpp"

#include "dudu/core/text.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace dudu {
namespace {

enum class Section {
    Body,
    Parameters,
    TemplateParameters,
    Returns,
    Deprecated,
};

std::vector<std::string> lines(std::string_view text) {
    std::vector<std::string> out;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        out.push_back(std::move(line));
    }
    return out;
}

void append_line(std::string& target, std::string_view line) {
    const std::string trimmed = trim_string(std::string(line));
    if (trimmed.empty()) {
        if (!target.empty() && !target.ends_with("\n\n")) {
            target += "\n\n";
        }
        return;
    }
    if (!target.empty() && !target.ends_with('\n')) {
        target.push_back(' ');
    }
    target += trimmed;
}

bool heading(std::string_view line, std::string_view name) {
    return trim_string(std::string(line)) == name;
}

bool named_entry(std::string_view line, std::string& name, std::string& text) {
    const std::string trimmed = trim_string(std::string(line));
    const size_t colon = trimmed.find(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }
    name = trim_string(trimmed.substr(0, colon));
    text = trim_string(trimmed.substr(colon + 1));
    return !name.empty();
}

void append_named_docs(std::string& markdown, std::string_view title,
                       const std::map<std::string, std::string>& docs) {
    if (docs.empty()) {
        return;
    }
    if (!markdown.empty()) {
        markdown += "\n\n";
    }
    markdown += "**" + std::string(title) + "**";
    for (const auto& [name, text] : docs) {
        markdown += "\n\n- `" + name + "`";
        if (!text.empty()) {
            markdown += ": " + text;
        }
    }
}

void append_parameter_docs(std::string& markdown, std::string_view title,
                           const std::vector<Symbol::Parameter>& parameters) {
    const bool has_documentation =
        std::ranges::any_of(parameters, [](const Symbol::Parameter& parameter) {
            return !parameter.documentation.empty();
        });
    if (!has_documentation) {
        return;
    }
    if (!markdown.empty()) {
        markdown += "\n\n";
    }
    markdown += "**" + std::string(title) + "**";
    for (const Symbol::Parameter& parameter : parameters) {
        if (parameter.documentation.empty()) {
            continue;
        }
        markdown += "\n\n- `" + parameter.label + "`: " + parameter.documentation;
    }
}

} // namespace

StructuredDocumentation parse_documentation(std::string_view text) {
    StructuredDocumentation out;
    Section section = Section::Body;
    std::string current_name;
    for (const std::string& line : lines(text)) {
        if (heading(line, "Args:") || heading(line, "Parameters:")) {
            section = Section::Parameters;
            current_name.clear();
            continue;
        }
        if (heading(line, "Type Args:") || heading(line, "Type Parameters:")) {
            section = Section::TemplateParameters;
            current_name.clear();
            continue;
        }
        if (heading(line, "Returns:") || heading(line, "Return:")) {
            section = Section::Returns;
            current_name.clear();
            continue;
        }
        if (heading(line, "Deprecated:")) {
            section = Section::Deprecated;
            current_name.clear();
            continue;
        }
        if (section == Section::Body) {
            append_line(out.body, line);
            continue;
        }
        if (section == Section::Returns) {
            append_line(out.returns, line);
            continue;
        }
        if (section == Section::Deprecated) {
            append_line(out.deprecated, line);
            continue;
        }
        auto& docs = section == Section::Parameters ? out.parameters : out.template_parameters;
        std::string name;
        std::string value;
        if (named_entry(line, name, value)) {
            current_name = std::move(name);
            docs[current_name] = std::move(value);
        } else if (!current_name.empty()) {
            append_line(docs[current_name], line);
        }
    }
    out.body = trim_string(std::move(out.body));
    out.returns = trim_string(std::move(out.returns));
    out.deprecated = trim_string(std::move(out.deprecated));
    return out;
}

std::string documentation_markdown(std::string_view text) {
    const StructuredDocumentation docs = parse_documentation(text);
    std::string markdown = docs.body;
    append_named_docs(markdown, "Parameters", docs.parameters);
    append_named_docs(markdown, "Type parameters", docs.template_parameters);
    if (!docs.returns.empty()) {
        if (!markdown.empty()) {
            markdown += "\n\n";
        }
        markdown += "**Returns**\n\n" + docs.returns;
    }
    if (!docs.deprecated.empty()) {
        if (!markdown.empty()) {
            markdown += "\n\n";
        }
        markdown += "**Deprecated:** " + docs.deprecated;
    }
    return markdown;
}

std::string parameter_documentation(std::string_view text, std::string_view parameter) {
    const StructuredDocumentation docs = parse_documentation(text);
    const auto found = docs.parameters.find(std::string(parameter));
    return found == docs.parameters.end() ? std::string{} : found->second;
}

std::string symbol_documentation_markdown(const Symbol& symbol) {
    std::string markdown = documentation_markdown(symbol.doc_comment);
    if (symbol.native_identity_key.has_value()) {
        append_parameter_docs(markdown, "Parameters", symbol.parameters);
        append_parameter_docs(markdown, "Type parameters", symbol.template_parameters);
        if (!symbol.return_documentation.empty()) {
            if (!markdown.empty()) {
                markdown += "\n\n";
            }
            markdown += "**Returns**\n\n" + symbol.return_documentation;
        }
        if (!symbol.deprecated_message.empty()) {
            if (!markdown.empty()) {
                markdown += "\n\n";
            }
            markdown += "**Deprecated:** " + symbol.deprecated_message;
        }
    }
    return markdown;
}

} // namespace dudu
