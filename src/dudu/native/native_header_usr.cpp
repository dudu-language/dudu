#include "dudu/native/native_header_usr.hpp"

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

CXChildVisitResult collect_cursor(CXCursor cursor, CXCursor, CXClientData data) {
    auto& index = *static_cast<NativeCursorIdentityIndex*>(data);
    const std::optional<NativeCursorKind> kind = native_cursor_kind(cursor);
    if (kind) {
        const std::string name = cx_string(clang_getCursorSpelling(cursor));
        const std::string usr = cx_string(clang_getCursorUSR(cursor));
        const SourceLocation location = cursor_location(cursor);
        if (!name.empty() && !usr.empty() && !location.file.empty()) {
            index.insert(*kind, name, location, usr, cursor_type_layout(cursor, *kind));
        }
    }
    return CXChildVisit_Recurse;
}

} // namespace

void NativeCursorIdentityIndex::insert(NativeCursorKind kind, std::string name,
                                       SourceLocation location, std::string usr,
                                       std::optional<TypeLayout> layout) {
    const std::string key = identity_key(kind, name, location);
    identities_.insert_or_assign(key, std::move(usr));
    if (layout) {
        layouts_.insert_or_assign(key, *layout);
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
        out += std::to_string(key.size()) + ":" + key + std::to_string(usr.size()) + ":" + usr +
               std::to_string(size.size()) + ":" + size + std::to_string(alignment.size()) + ":" +
               alignment + "\n";
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
        if (!read_field(key) || !read_field(usr) || !read_field(size) || !read_field(alignment)) {
            return {};
        }
        out.identities_.insert_or_assign(key, std::move(usr));
        if (!size.empty() && !alignment.empty()) {
            out.layouts_.insert_or_assign(
                key, TypeLayout{.size = static_cast<size_t>(std::stoull(size)),
                                .alignment = static_cast<size_t>(std::stoull(alignment))});
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
        clang_visitChildren(clang_getTranslationUnitCursor(unit), collect_cursor, &out);
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
