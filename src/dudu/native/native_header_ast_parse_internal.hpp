#pragma once

#include "dudu/native/native_header_parse.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu::native_ast_parse {

struct TemplateContext {
    enum class Kind {
        Alias,
        Class,
        Function,
    };

    int depth = 0;
    Kind kind = Kind::Function;
    std::vector<std::string> params;
    std::vector<bool> param_is_value;
    std::vector<bool> param_has_default;
    std::vector<TypeRef> param_defaults;
    int last_param_depth = -1;
    SourceLocation location;
};

enum class CommentTargetKind {
    Type,
    Value,
    Function,
    Class,
    Field,
    StaticField,
    Method,
    Namespace,
};

struct CommentTarget {
    int depth = 0;
    CommentTargetKind kind = CommentTargetKind::Function;
    size_t primary = 0;
    size_t secondary = 0;
};

enum class ParamTargetKind {
    Function,
    Method,
};

struct ParamTarget {
    int depth = 0;
    ParamTargetKind kind = ParamTargetKind::Function;
    size_t primary = 0;
    size_t secondary = 0;
    size_t next_param = 0;
};

struct EnumContext {
    int depth = 0;
    std::string type_name;
    std::string value_scope;
};

struct AstParseState {
    AstParseState(NativeHeaderScan& scan, const NativeCursorIdentityIndex& identities,
                  SourceLocation root_location)
        : scan(scan), identities(identities), root_location(std::move(root_location)) {
    }

    NativeHeaderScan& scan;
    const NativeCursorIdentityIndex& identities;
    SourceLocation root_location;
    std::vector<std::pair<int, std::string>> namespaces;
    std::vector<std::pair<int, size_t>> classes;
    std::vector<EnumContext> enums;
    std::vector<TemplateContext> templates;
    std::vector<std::pair<int, size_t>> functions;
    std::vector<ParamTarget> param_targets;
    std::vector<CommentTarget> comment_targets;
    std::string current_file;
};

bool dudu_primitive_name(std::string_view name);
NativeSymbolId scanned_identity(const NativeCursorIdentityIndex& identities, NativeCursorKind kind,
                                std::string_view spelling, const SourceLocation& location,
                                std::string canonical_path, const std::string& current_file);
std::optional<TypeLayout> scanned_layout(const NativeCursorIdentityIndex& identities,
                                         NativeCursorKind kind, std::string_view spelling,
                                         const SourceLocation& location);
TypeRef parse_native_type_text(std::string text, const SourceLocation& location);
void replace_native_type_placeholder(TypeRef& type, std::string_view placeholder,
                                     std::string_view replacement);
std::string native_type_placeholder(std::string_view index);
std::string preserve_native_type_placeholders(std::string text);
void add_base_class(ClassDecl& klass, std::string base, const SourceLocation& location);
void append_doc_text(NativeHeaderScan& scan, const CommentTarget& target, const std::string& text);
size_t append_native_function(NativeHeaderScan& scan,
                              const std::vector<std::pair<int, std::string>>& namespaces,
                              const NativeCursorIdentityIndex& identities, std::string raw_name,
                              const std::string& signature, const SourceLocation& location,
                              const std::string& current_file,
                              const std::vector<std::string>& template_params = {},
                              const std::vector<bool>& template_param_is_value = {},
                              bool deleted = false);

void parse_ast_declaration(AstParseState& state, const std::string& line, int depth,
                           const SourceLocation& location);

} // namespace dudu::native_ast_parse
