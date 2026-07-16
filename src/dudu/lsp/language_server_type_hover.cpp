#include "dudu/lsp/language_server_type_hover.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_markdown.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/lsp/language_server_type_layout.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/sema/sema_context.hpp"

#include <map>
#include <optional>
#include <sstream>
#include <string>

namespace dudu {
namespace {

std::string generic_params_label(const std::vector<std::string>& params) {
    if (params.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << params[i];
    }
    out << "]";
    return out.str();
}

std::string class_header(const ClassDecl& klass, bool native) {
    return std::string(native ? "native class " : "class ") + klass.name +
           generic_params_label(klass.generic_params);
}

std::string class_definition_preview(const ClassDecl& klass, bool native) {
    std::ostringstream out;
    out << class_header(klass, native) << ":";
    constexpr size_t max_fields = 5;
    size_t shown = 0;
    for (const FieldDecl& field : klass.fields) {
        if (shown >= max_fields) {
            break;
        }
        out << "\n    " << field.name << ": " << type_ref_text(field.type_ref);
        ++shown;
    }
    const size_t remaining = klass.fields.size() > shown ? klass.fields.size() - shown : 0;
    if (remaining > 0) {
        out << "\n    # ... " << remaining << " more field" << (remaining == 1 ? "" : "s");
    }
    if (klass.fields.empty()) {
        out << "\n    pass";
    }
    return out.str();
}

} // namespace

std::optional<std::string> primitive_type_hover_json(const std::string& word) {
    static const std::map<std::string, std::pair<std::string, std::string>> primitives = {
        {"bool", {"bool", "Boolean value."}},
        {"i8", {"std::int8_t", "Signed 8-bit integer."}},
        {"i16", {"std::int16_t", "Signed 16-bit integer."}},
        {"i32", {"std::int32_t", "Signed 32-bit integer."}},
        {"i64", {"std::int64_t", "Signed 64-bit integer."}},
        {"isize", {"std::ptrdiff_t", "Signed pointer-sized integer."}},
        {"u8", {"std::uint8_t", "Unsigned 8-bit integer."}},
        {"u16", {"std::uint16_t", "Unsigned 16-bit integer."}},
        {"u32", {"std::uint32_t", "Unsigned 32-bit integer."}},
        {"u64", {"std::uint64_t", "Unsigned 64-bit integer."}},
        {"usize", {"std::size_t", "Unsigned pointer-sized integer."}},
        {"f32", {"float", "32-bit floating-point value."}},
        {"f64", {"double", "64-bit floating-point value."}},
        {"str", {"std::string", "Owned UTF-8 string value."}},
        {"None", {"std::nullptr_t", "Null pointer value."}},
        {"slice", {"dudu::Slice", "Index slice value used by library-defined indexing hooks."}},
        {"ellipsis", {"dudu::Ellipsis", "Index item produced by `...` inside `[]`."}},
        {"new_axis", {"dudu::NewAxis", "Index item produced by `None` inside `[]`."}},
        {"scalar_index",
         {"dudu::ScalarIndex",
          "Index-category type accepted by variadic `@operator(\"[]\")` hooks for scalar "
          "integer indices."}},
        {"basic_index",
         {"dudu::BasicIndex",
          "Index-category type accepted by variadic `@operator(\"[]\")` hooks for scalar "
          "indices, slices, ellipsis, and new-axis items."}},
        {"list", {"std::vector<T>", "Dynamic owning contiguous list. Use `list[T]`."}},
        {"dict", {"std::unordered_map<K, V>", "Dynamic hash map. Use `dict[K, V]`."}},
        {"set", {"std::unordered_set<T>", "Dynamic hash set. Use `set[T]`."}},
        {"tuple", {"dudu::TupleN<T...>", "Fixed-size heterogenous tuple. Use `tuple[T...]`."}},
        {"variant", {"std::variant<T...>", "Tagged union over explicit alternatives."}},
        {"Option", {"std::optional<T>", "Optional value. Use `Option[T]`."}},
        {"Result", {"dudu::Result<T, E>", "Result value with `Ok[T]` or `Err[E]` payload."}},
        {"array",
         {"std::array<T, N> / nested fixed storage",
          "Fixed-shape contiguous array. Use `array[T][shape]`, or `array[T] = literal` "
          "when the shape can be inferred."}},
        {"span", {"std::span<T>", "Non-owning contiguous view. Use `span[T]`."}},
        {"array_view",
         {"dudu::ArrayView<T>",
          "Rank-independent non-owning view produced by fixed-array slicing. Use "
          "`array_view[T]` for helpers that consume `array[T][...]` slices without copying."}},
    };
    const auto found = primitives.find(word);
    if (found == primitives.end()) {
        return std::nullopt;
    }
    return markdown_hover_json(fenced_code("dudu", "type " + word) + "\n\n" + found->second.second +
                               "\n\nC++ lowering: `" + found->second.first + "`");
}

std::optional<std::string> native_alias_hover_json(const std::string& word,
                                                   const ModuleAst& module) {
    if (word.empty()) {
        return std::nullopt;
    }
    const NativeClassDefinitionIndex class_index = native_class_definition_index(module);
    const auto build_hover = [&](const NativeTypeDecl& type) -> std::optional<std::string> {
        const std::optional<NativeClassDefinition> target =
            native_alias_target_class_definition(class_index, type);
        if (!target) {
            return std::nullopt;
        }
        const std::string target_display = native_type_alias_type_text(type);
        std::string markdown = fenced_code("cpp", "native type = " + target_display) +
                               "\n\nresolves to `native class " + target_display + "`";
        if (type.layout) {
            markdown += "\n\nsize = " + std::to_string(type.layout->size) +
                        " bytes, align = " + std::to_string(type.layout->alignment) + " bytes";
        }
        const std::string identity = native_symbol_identity_key(type.identity);
        if (!identity.empty()) {
            if (const std::optional<std::filesystem::path> path =
                    native_identity_source_path(identity)) {
                markdown += "\n\nDeclared in: `" + path->string() + "`";
            }
            markdown += "\n\nNative identity: `" + identity + "`";
        }
        const std::string& doc_comment =
            !type.doc_comment.empty() ? type.doc_comment : target->doc_comment;
        if (!doc_comment.empty()) {
            markdown += "\n\n" + doc_comment;
        }
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
               "\"},\"range\":" + range_json(type.location) + "}";
    };
    for (const NativeTypeDecl& type : module.native_types) {
        if (type.name == word) {
            return build_hover(type);
        }
    }
    for (const NativeTypeDecl& type : module.native_types) {
        if (symbol_matches(type.name, word)) {
            return build_hover(type);
        }
    }
    return std::nullopt;
}

std::string class_hover_json(const ModuleAst& module, const ClassDecl& klass, bool native) {
    std::string markdown =
        fenced_code(native ? "cpp" : "dudu", class_definition_preview(klass, native));
    const Symbols symbols = collect_symbols(module);
    if (const std::optional<TypeLayout> layout = resolved_class_layout(symbols, klass)) {
        markdown += "\n\nsize = " + std::to_string(layout->size) +
                    " bytes, align = " + std::to_string(layout->alignment) + " bytes";
    }
    if (!klass.doc_comment.empty()) {
        markdown += "\n\n" + klass.doc_comment;
    }
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(klass.location) + "}";
}

} // namespace dudu
