#include "dudu/native/native_header_cursor_metadata.hpp"

#include "dudu/core/text.hpp"

#include <clang-c/Documentation.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string cx_string(CXString value) {
    const char* text = clang_getCString(value);
    std::string out = text == nullptr ? std::string{} : std::string(text);
    clang_disposeString(value);
    return out;
}

void append_text(std::string& out, std::string text) {
    text = trim_string(std::move(text));
    if (text.empty()) {
        return;
    }
    if (!out.empty() && !std::isspace(static_cast<unsigned char>(out.back()))) {
        out.push_back(' ');
    }
    out += text;
}

std::string comment_text(CXComment comment) {
    std::string out;
    const CXCommentKind kind = clang_Comment_getKind(comment);
    if (kind == CXComment_Text) {
        return trim_string(cx_string(clang_TextComment_getText(comment)));
    }
    if (kind == CXComment_InlineCommand) {
        const unsigned count = clang_InlineCommandComment_getNumArgs(comment);
        for (unsigned index = 0; index < count; ++index) {
            append_text(out, cx_string(clang_InlineCommandComment_getArgText(comment, index)));
        }
        return out;
    }
    if (kind == CXComment_VerbatimBlockLine) {
        return cx_string(clang_VerbatimBlockLineComment_getText(comment));
    }
    if (kind == CXComment_VerbatimLine) {
        return cx_string(clang_VerbatimLineComment_getText(comment));
    }
    const unsigned count = clang_Comment_getNumChildren(comment);
    for (unsigned index = 0; index < count; ++index) {
        append_text(out, comment_text(clang_Comment_getChild(comment, index)));
    }
    return trim_string(std::move(out));
}

NativeParameterMetadata* find_parameter(std::vector<NativeParameterMetadata>& params,
                                        std::string_view name) {
    const auto found = std::ranges::find(params, name, &NativeParameterMetadata::name);
    return found == params.end() ? nullptr : &*found;
}

void collect_comment_metadata(CXComment comment, NativeDeclarationMetadata& metadata) {
    const CXCommentKind kind = clang_Comment_getKind(comment);
    if (kind == CXComment_ParamCommand) {
        const std::string name = cx_string(clang_ParamCommandComment_getParamName(comment));
        NativeParameterMetadata* param = find_parameter(metadata.parameters, name);
        if (param == nullptr && !name.empty()) {
            metadata.parameters.push_back(
                {.name = name, .default_value = {}, .doc_comment = {}});
            param = &metadata.parameters.back();
        }
        if (param != nullptr) {
            param->doc_comment = comment_text(comment);
        }
        return;
    }
    if (kind == CXComment_TParamCommand) {
        const std::string name = cx_string(clang_TParamCommandComment_getParamName(comment));
        NativeParameterMetadata* param = find_parameter(metadata.template_parameters, name);
        if (param == nullptr && !name.empty()) {
            metadata.template_parameters.push_back(
                {.name = name, .default_value = {}, .doc_comment = {}});
            param = &metadata.template_parameters.back();
        }
        if (param != nullptr) {
            param->doc_comment = comment_text(comment);
        }
        return;
    }
    if (kind == CXComment_BlockCommand) {
        const std::string command =
            cx_string(clang_BlockCommandComment_getCommandName(comment));
        const std::string text = comment_text(clang_BlockCommandComment_getParagraph(comment));
        if (command == "return" || command == "returns" || command == "result") {
            metadata.return_doc_comment = text;
        } else if (command == "deprecated") {
            metadata.deprecated_message = text;
        }
        return;
    }
    const unsigned count = clang_Comment_getNumChildren(comment);
    for (unsigned index = 0; index < count; ++index) {
        collect_comment_metadata(clang_Comment_getChild(comment, index), metadata);
    }
}

void collect_summary_text(CXComment comment, std::string& summary) {
    const CXCommentKind kind = clang_Comment_getKind(comment);
    if (kind == CXComment_ParamCommand || kind == CXComment_TParamCommand) {
        return;
    }
    if (kind == CXComment_BlockCommand) {
        const std::string command =
            cx_string(clang_BlockCommandComment_getCommandName(comment));
        if (command == "return" || command == "returns" || command == "result" ||
            command == "deprecated") {
            return;
        }
        append_text(summary,
                    comment_text(clang_BlockCommandComment_getParagraph(comment)));
        return;
    }
    if (kind == CXComment_Paragraph || kind == CXComment_VerbatimBlockLine ||
        kind == CXComment_VerbatimLine) {
        append_text(summary, comment_text(comment));
        return;
    }
    const unsigned count = clang_Comment_getNumChildren(comment);
    for (unsigned index = 0; index < count; ++index) {
        collect_summary_text(clang_Comment_getChild(comment, index), summary);
    }
}

std::string pretty_declaration(CXCursor cursor) {
    CXPrintingPolicy policy = clang_getCursorPrintingPolicy(cursor);
    if (policy == nullptr) {
        return {};
    }
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_TerseOutput, 1);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_PolishForDeclaration, 1);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_IncludeNewlines, 1);
    clang_PrintingPolicy_setProperty(policy, CXPrintingPolicy_SuppressInitializers, 0);
    std::string out = trim_string(cx_string(clang_getCursorPrettyPrinted(cursor, policy)));
    clang_PrintingPolicy_dispose(policy);
    return out;
}

std::string default_value(std::string declaration) {
    const size_t equals = declaration.find('=');
    if (equals == std::string::npos) {
        return {};
    }
    return trim_string(declaration.substr(equals + 1));
}

std::string unquote_string_literal(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.erase(value.begin());
        value.pop_back();
    }
    return value;
}

std::string deprecated_attribute_message(CXCursor cursor) {
    CXTranslationUnit unit = clang_Cursor_getTranslationUnit(cursor);
    if (unit == nullptr) {
        return {};
    }
    CXToken* tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, clang_getCursorExtent(cursor), &tokens, &count);
    bool saw_deprecated = false;
    std::string out;
    for (unsigned index = 0; index < count; ++index) {
        const std::string spelling = cx_string(clang_getTokenSpelling(unit, tokens[index]));
        if (spelling == "deprecated") {
            saw_deprecated = true;
            continue;
        }
        if (saw_deprecated && clang_getTokenKind(tokens[index]) == CXToken_Literal) {
            out = unquote_string_literal(spelling);
            break;
        }
    }
    clang_disposeTokens(unit, tokens, count);
    return out;
}

struct ChildContext {
    NativeDeclarationMetadata& metadata;
};

CXChildVisitResult collect_parameter(CXCursor child, CXCursor, CXClientData data) {
    auto& metadata = static_cast<ChildContext*>(data)->metadata;
    const CXCursorKind kind = clang_getCursorKind(child);
    if (kind == CXCursor_ParmDecl) {
        metadata.parameters.push_back(
            {.name = cx_string(clang_getCursorSpelling(child)),
             .default_value = default_value(pretty_declaration(child)),
             .doc_comment = {}});
    } else if (kind == CXCursor_TemplateTypeParameter ||
               kind == CXCursor_NonTypeTemplateParameter ||
               kind == CXCursor_TemplateTemplateParameter) {
        metadata.template_parameters.push_back(
            {.name = cx_string(clang_getCursorSpelling(child)),
             .default_value = default_value(pretty_declaration(child)),
             .doc_comment = {}});
    }
    return CXChildVisit_Continue;
}

} // namespace

NativeDeclarationMetadata native_cursor_metadata(CXCursor cursor) {
    NativeDeclarationMetadata metadata;
    metadata.declaration = pretty_declaration(cursor);
    ChildContext context{.metadata = metadata};
    clang_visitChildren(cursor, collect_parameter, &context);
    const CXComment comment = clang_Cursor_getParsedComment(cursor);
    collect_summary_text(comment, metadata.summary_doc_comment);
    collect_comment_metadata(comment, metadata);
    if (const std::string attribute_message = deprecated_attribute_message(cursor);
        !attribute_message.empty()) {
        metadata.deprecated_message = attribute_message;
    }
    if (clang_getCursorAvailability(cursor) == CXAvailability_Deprecated &&
        metadata.deprecated_message.empty()) {
        metadata.deprecated_message = "Deprecated native declaration.";
    }
    return metadata;
}

} // namespace dudu
