#include "dudu/native/native_header_usr.hpp"

#include "dudu/native/native_header_cursor_metadata.hpp"
#include "dudu/native/native_header_scan_command.hpp"

#include <charconv>
#include <clang-c/Index.h>
#include <system_error>
#include <utility>
#include <vector>

namespace dudu {
namespace {

std::string cx_string(CXString value) {
    const char* text = clang_getCString(value);
    std::string out = text == nullptr ? std::string{} : std::string(text);
    clang_disposeString(value);
    return out;
}

std::string normalized_source_path(std::string path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? std::filesystem::path(path).lexically_normal().string() : canonical.string();
}

std::string identity_key(NativeCursorKind kind, std::string_view name,
                         const SourceLocation& location) {
    return std::to_string(static_cast<int>(kind)) + "\n" + normalized_source_path(location.file) +
           "\n" + std::to_string(location.line) + "\n" + std::to_string(location.column) + "\n" +
           std::string(name);
}

std::string semantic_identity_key(NativeCursorKind kind, std::string_view path) {
    return "semantic\n" + std::to_string(static_cast<int>(kind)) + "\n" + std::string(path);
}

std::string specialization_arguments_key(std::string_view name, const SourceLocation& location) {
    return "specialization\n" + normalized_source_path(location.file) + "\n" +
           std::to_string(location.line) + "\n" + std::to_string(location.column) + "\n" +
           std::string(name);
}

std::string encode_strings(const std::vector<std::string>& values) {
    std::string out;
    for (const std::string& value : values) {
        out += std::to_string(value.size()) + ":" + value;
    }
    return out;
}

std::vector<std::string> decode_strings(std::string_view text) {
    std::vector<std::string> out;
    size_t cursor = 0;
    while (cursor < text.size()) {
        const size_t colon = text.find(':', cursor);
        if (colon == std::string_view::npos) {
            return {};
        }
        size_t size = 0;
        const auto [end, error] = std::from_chars(text.data() + cursor, text.data() + colon, size);
        if (error != std::errc{} || end != text.data() + colon || size > text.size() - colon - 1) {
            return {};
        }
        cursor = colon + 1;
        out.emplace_back(text.substr(cursor, size));
        cursor += size;
    }
    return out;
}

std::string encode_parameters(const std::vector<NativeParameterMetadata>& parameters) {
    std::vector<std::string> fields;
    fields.reserve(parameters.size() * 3);
    for (const NativeParameterMetadata& parameter : parameters) {
        fields.push_back(parameter.name);
        fields.push_back(parameter.default_value);
        fields.push_back(parameter.doc_comment);
    }
    return encode_strings(fields);
}

std::vector<NativeParameterMetadata> decode_parameters(std::string_view text) {
    const std::vector<std::string> fields = decode_strings(text);
    if (fields.size() % 3 != 0) {
        return {};
    }
    std::vector<NativeParameterMetadata> out;
    out.reserve(fields.size() / 3);
    for (size_t index = 0; index < fields.size(); index += 3) {
        out.push_back({.name = fields[index],
                       .default_value = fields[index + 1],
                       .doc_comment = fields[index + 2]});
    }
    return out;
}

std::string encode_metadata(const NativeDeclarationMetadata& metadata) {
    return encode_strings({metadata.declaration, metadata.summary_doc_comment,
                           metadata.return_doc_comment, metadata.deprecated_message,
                           encode_parameters(metadata.parameters),
                           encode_parameters(metadata.template_parameters)});
}

std::optional<NativeDeclarationMetadata> decode_metadata(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    const std::vector<std::string> fields = decode_strings(text);
    if (fields.size() != 6) {
        return std::nullopt;
    }
    return NativeDeclarationMetadata{
        .declaration = fields[0],
        .summary_doc_comment = fields[1],
        .return_doc_comment = fields[2],
        .deprecated_message = fields[3],
        .parameters = decode_parameters(fields[4]),
        .template_parameters = decode_parameters(fields[5]),
    };
}

std::string cursor_semantic_path(CXCursor cursor) {
    std::vector<std::string> parts;
    for (CXCursor current = cursor; !clang_Cursor_isNull(current);
         current = clang_getCursorSemanticParent(current)) {
        if (clang_getCursorKind(current) == CXCursor_TranslationUnit) {
            break;
        }
        const std::string name = cx_string(clang_getCursorSpelling(current));
        if (!name.empty()) {
            parts.push_back(name);
        }
    }
    std::string out;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
        if (!out.empty()) {
            out += '.';
        }
        out += *part;
    }
    return out;
}

bool class_cursor(CXCursorKind kind) {
    return kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl ||
           kind == CXCursor_ClassDecl || kind == CXCursor_ClassTemplate ||
           kind == CXCursor_ClassTemplatePartialSpecialization;
}

bool class_parent(CXCursor cursor) {
    return class_cursor(clang_getCursorKind(clang_getCursorSemanticParent(cursor)));
}

std::optional<NativeCursorKind> native_cursor_kind(CXCursor cursor) {
    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (class_cursor(kind)) {
        return NativeCursorKind::Class;
    }
    if (kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl ||
        kind == CXCursor_TypeAliasTemplateDecl || kind == CXCursor_EnumDecl) {
        return NativeCursorKind::Type;
    }
    if (kind == CXCursor_FunctionDecl) {
        return NativeCursorKind::Function;
    }
    if (kind == CXCursor_FunctionTemplate) {
        return class_parent(cursor) ? NativeCursorKind::Method : NativeCursorKind::Function;
    }
    if (kind == CXCursor_CXXMethod || kind == CXCursor_ConversionFunction) {
        return NativeCursorKind::Method;
    }
    if (kind == CXCursor_Constructor) {
        return NativeCursorKind::Constructor;
    }
    if (kind == CXCursor_FieldDecl) {
        return NativeCursorKind::Field;
    }
    if (kind == CXCursor_VarDecl || kind == CXCursor_EnumConstantDecl) {
        return NativeCursorKind::Value;
    }
    if (kind == CXCursor_Namespace) {
        return NativeCursorKind::Namespace;
    }
    return std::nullopt;
}

std::optional<TypeLayout> cursor_type_layout(CXCursor cursor, NativeCursorKind kind) {
    if (kind != NativeCursorKind::Type && kind != NativeCursorKind::Class) {
        return std::nullopt;
    }
    const CXType type = clang_getCursorType(cursor);
    const long long size = clang_Type_getSizeOf(type);
    const long long alignment = clang_Type_getAlignOf(type);
    if (size < 0 || alignment <= 0) {
        return std::nullopt;
    }
    return TypeLayout{.size = static_cast<size_t>(size),
                      .alignment = static_cast<size_t>(alignment)};
}

SourceLocation cursor_location(CXCursor cursor) {
    CXFile file = nullptr;
    unsigned line = 0;
    unsigned column = 0;
    unsigned offset = 0;
    clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, &line, &column, &offset);
    (void)offset;
    return {.file = SourceFileName(file == nullptr ? std::string{}
                                                   : cx_string(clang_getFileName(file))),
            .line = static_cast<int>(line),
            .column = static_cast<int>(column)};
}

bool word_token(CXTokenKind kind) {
    return kind == CXToken_Identifier || kind == CXToken_Keyword || kind == CXToken_Literal;
}

std::string join_tokens(const std::vector<std::pair<std::string, CXTokenKind>>& tokens) {
    std::string out;
    CXTokenKind previous_kind = CXToken_Punctuation;
    for (const auto& [spelling, kind] : tokens) {
        if (!out.empty() && word_token(previous_kind) && word_token(kind)) {
            out.push_back(' ');
        }
        out += spelling;
        previous_kind = kind;
    }
    return out;
}

std::vector<std::string> specialization_arguments(CXTranslationUnit unit, CXCursor cursor,
                                                  std::string_view name) {
    CXToken* raw_tokens = nullptr;
    unsigned token_count = 0;
    clang_tokenize(unit, clang_getCursorExtent(cursor), &raw_tokens, &token_count);
    std::vector<std::pair<std::string, CXTokenKind>> tokens;
    tokens.reserve(token_count);
    for (unsigned i = 0; i < token_count; ++i) {
        const CXTokenKind kind = clang_getTokenKind(raw_tokens[i]);
        if (kind != CXToken_Comment) {
            tokens.emplace_back(cx_string(clang_getTokenSpelling(unit, raw_tokens[i])), kind);
        }
    }
    clang_disposeTokens(unit, raw_tokens, token_count);

    size_t open = tokens.size();
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].first == name && tokens[i + 1].first == "<") {
            open = i + 1;
            break;
        }
    }
    if (open == tokens.size()) {
        return {};
    }

    std::vector<std::string> out;
    std::vector<std::pair<std::string, CXTokenKind>> current;
    int angle_depth = 1;
    for (size_t i = open + 1; i < tokens.size(); ++i) {
        const std::string& token = tokens[i].first;
        if (token == "<") {
            ++angle_depth;
            current.push_back(tokens[i]);
            continue;
        }
        if (token == ">" ||
            (angle_depth > 1 && token.find_first_not_of('>') == std::string::npos)) {
            for (size_t close = 0; close < token.size(); ++close) {
                --angle_depth;
                if (angle_depth == 0) {
                    if (!current.empty()) {
                        out.push_back(join_tokens(current));
                    }
                    return out;
                }
                current.emplace_back(">", tokens[i].second);
            }
            continue;
        }
        if (token == "," && angle_depth == 1) {
            out.push_back(join_tokens(current));
            current.clear();
            continue;
        }
        current.push_back(tokens[i]);
    }
    return {};
}

struct CursorCollectionContext {
    NativeCursorIdentityIndex& index;
    CXTranslationUnit unit;
};

CXChildVisitResult collect_cursor(CXCursor cursor, CXCursor, CXClientData data) {
    auto& context = *static_cast<CursorCollectionContext*>(data);
    NativeCursorIdentityIndex& index = context.index;
    const std::optional<NativeCursorKind> kind = native_cursor_kind(cursor);
    if (kind) {
        const std::string name = cx_string(clang_getCursorSpelling(cursor));
        const std::string usr = cx_string(clang_getCursorUSR(cursor));
        const SourceLocation location = cursor_location(cursor);
        if (!name.empty() && !usr.empty() && !location.file.empty()) {
            index.insert(*kind, name, location, usr, cursor_type_layout(cursor, *kind),
                         cursor_semantic_path(cursor));
            index.insert_metadata(*kind, name, location, native_cursor_metadata(cursor));
            if (clang_getCursorKind(cursor) == CXCursor_ClassTemplatePartialSpecialization) {
                const std::vector<std::string> arguments =
                    specialization_arguments(context.unit, cursor, name);
                index.insert_specialization_arguments(name, location, arguments);
            }
        }
    }
    return CXChildVisit_Recurse;
}

} // namespace

void NativeCursorIdentityIndex::insert(NativeCursorKind kind, std::string name,
                                       SourceLocation location, std::string usr,
                                       std::optional<TypeLayout> layout,
                                       std::string semantic_path) {
    const std::string key = identity_key(kind, name, location);
    identities_.insert_or_assign(key, usr);
    if (layout) {
        layouts_.insert_or_assign(key, *layout);
    }
    if (!semantic_path.empty()) {
        const std::string semantic_key = semantic_identity_key(kind, semantic_path);
        identities_.insert_or_assign(semantic_key, std::move(usr));
        if (layout) {
            layouts_.insert_or_assign(semantic_key, *layout);
        }
    }
}

std::optional<TypeLayout>
NativeCursorIdentityIndex::find_layout(NativeCursorKind kind, std::string_view name,
                                       const SourceLocation& location) const {
    const auto found = layouts_.find(identity_key(kind, name, location));
    return found == layouts_.end() ? std::nullopt : std::optional<TypeLayout>{found->second};
}

std::optional<std::string> NativeCursorIdentityIndex::find(NativeCursorKind kind,
                                                           std::string_view name,
                                                           const SourceLocation& location) const {
    const auto found = identities_.find(identity_key(kind, name, location));
    return found == identities_.end() ? std::nullopt : std::optional<std::string>{found->second};
}

std::optional<std::string>
NativeCursorIdentityIndex::find_semantic(NativeCursorKind kind,
                                         std::string_view semantic_path) const {
    const auto found = identities_.find(semantic_identity_key(kind, semantic_path));
    return found == identities_.end() ? std::nullopt : std::optional<std::string>{found->second};
}

std::optional<TypeLayout>
NativeCursorIdentityIndex::find_semantic_layout(NativeCursorKind kind,
                                                std::string_view semantic_path) const {
    const auto found = layouts_.find(semantic_identity_key(kind, semantic_path));
    return found == layouts_.end() ? std::nullopt : std::optional<TypeLayout>{found->second};
}

void NativeCursorIdentityIndex::insert_metadata(NativeCursorKind kind, std::string name,
                                                SourceLocation location,
                                                NativeDeclarationMetadata metadata) {
    metadata_.insert_or_assign(identity_key(kind, name, location), std::move(metadata));
}

std::optional<NativeDeclarationMetadata>
NativeCursorIdentityIndex::find_metadata(NativeCursorKind kind, std::string_view name,
                                         const SourceLocation& location) const {
    const auto found = metadata_.find(identity_key(kind, name, location));
    return found == metadata_.end() ? std::nullopt
                                    : std::optional<NativeDeclarationMetadata>{found->second};
}

void NativeCursorIdentityIndex::insert_specialization_arguments(
    std::string name, SourceLocation location, std::vector<std::string> arguments) {
    if (arguments.empty()) {
        return;
    }
    identities_.insert_or_assign(specialization_arguments_key(name, location),
                                 encode_strings(arguments));
}

std::vector<std::string>
NativeCursorIdentityIndex::find_specialization_arguments(std::string_view name,
                                                         const SourceLocation& location) const {
    const auto found = identities_.find(specialization_arguments_key(name, location));
    return found == identities_.end() ? std::vector<std::string>{} : decode_strings(found->second);
}

bool NativeCursorIdentityIndex::empty() const {
    return identities_.empty();
}

std::string NativeCursorIdentityIndex::serialize() const {
    std::string out;
    for (const auto& [key, usr] : identities_) {
        const auto layout = layouts_.find(key);
        const std::string size =
            layout == layouts_.end() ? "" : std::to_string(layout->second.size);
        const std::string alignment =
            layout == layouts_.end() ? "" : std::to_string(layout->second.alignment);
        const auto metadata = metadata_.find(key);
        const std::string encoded_metadata =
            metadata == metadata_.end() ? "" : encode_metadata(metadata->second);
        out += std::to_string(key.size()) + ":" + key + std::to_string(usr.size()) + ":" + usr +
               std::to_string(size.size()) + ":" + size + std::to_string(alignment.size()) + ":" +
               alignment + std::to_string(encoded_metadata.size()) + ":" + encoded_metadata + "\n";
    }
    return out;
}

NativeCursorIdentityIndex NativeCursorIdentityIndex::deserialize(std::string_view text) {
    NativeCursorIdentityIndex out;
    size_t cursor = 0;
    auto read_field = [&](std::string& field) -> bool {
        const size_t colon = text.find(':', cursor);
        if (colon == std::string_view::npos) {
            return false;
        }
        size_t size = 0;
        const auto [end, error] = std::from_chars(text.data() + cursor, text.data() + colon, size);
        if (error != std::errc{} || end != text.data() + colon || size > text.size() - colon - 1) {
            return false;
        }
        cursor = colon + 1;
        field.assign(text.substr(cursor, size));
        cursor += size;
        return true;
    };
    while (cursor < text.size()) {
        std::string key;
        std::string usr;
        std::string size;
        std::string alignment;
        std::string metadata;
        if (!read_field(key) || !read_field(usr) || !read_field(size) || !read_field(alignment) ||
            !read_field(metadata)) {
            return {};
        }
        out.identities_.insert_or_assign(key, std::move(usr));
        if (!size.empty() && !alignment.empty()) {
            out.layouts_.insert_or_assign(
                key, TypeLayout{.size = static_cast<size_t>(std::stoull(size)),
                                .alignment = static_cast<size_t>(std::stoull(alignment))});
        }
        if (const std::optional<NativeDeclarationMetadata> decoded = decode_metadata(metadata)) {
            out.metadata_.insert_or_assign(key, *decoded);
        }
        if (cursor < text.size() && text[cursor] == '\n') {
            ++cursor;
        }
    }
    return out;
}

NativeCursorIdentityIndex scan_native_cursor_identities(const std::filesystem::path& source,
                                                        const NativeHeaderOptions& options,
                                                        bool include_source_dir) {
    std::vector<std::string> args = {"-std=" + options.config.cpp_std, "-x", "c++"};
    std::vector<std::string> scanner_args =
        native_header_scanner_arguments(options, include_source_dir);
    args.insert(args.end(), std::make_move_iterator(scanner_args.begin()),
                std::make_move_iterator(scanner_args.end()));
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }

    CXIndex clang_index = clang_createIndex(1, 0);
    CXTranslationUnit unit = nullptr;
    const CXErrorCode error = clang_parseTranslationUnit2(
        clang_index, source.string().c_str(), argv.data(), static_cast<int>(argv.size()), nullptr,
        0, CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_KeepGoing, &unit);
    NativeCursorIdentityIndex out;
    if (error == CXError_Success && unit != nullptr) {
        CursorCollectionContext context{.index = out, .unit = unit};
        clang_visitChildren(clang_getTranslationUnitCursor(unit), collect_cursor, &context);
    }
    if (unit != nullptr) {
        clang_disposeTranslationUnit(unit);
    }
    clang_disposeIndex(clang_index);
    if (error != CXError_Success) {
        throw CompileError({}, "libclang could not index native scanner source " + source.string());
    }
    return out;
}

} // namespace dudu
