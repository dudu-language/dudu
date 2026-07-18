#include "dudu/lsp/language_server_type_hover.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/lsp/language_server_documentation.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_markdown.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/lsp/language_server_type_layout.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/sema/sema_context.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>

namespace dudu {
namespace {

struct PrimitiveTypeDoc {
    std::string cpp_type;
    std::string description;
};

std::optional<PrimitiveTypeDoc> primitive_type_doc(const std::string& word) {
    static const std::map<std::string, PrimitiveTypeDoc> primitives = {
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
    return found == primitives.end() ? std::nullopt
                                     : std::optional<PrimitiveTypeDoc>{found->second};
}

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
    std::string out = std::string(native ? "native class " : "class ") + klass.name +
                      generic_params_label(klass.generic_params);
    if (!klass.base_class_refs.empty()) {
        out += "(";
        for (size_t index = 0; index < klass.base_class_refs.size(); ++index) {
            if (index > 0) {
                out += ", ";
            }
            out += type_ref_text(klass.base_class_refs[index].type_ref);
        }
        out += ")";
    }
    return out;
}

std::string class_definition_preview(const ClassDecl& klass, bool native) {
    std::ostringstream out;
    out << class_header(klass, native) << ":";
    constexpr size_t max_members = 8;
    size_t shown = 0;
    for (const TypeAliasDecl& alias : klass.type_aliases) {
        if (shown >= max_members) {
            break;
        }
        out << "\n    " << (native ? "using " : "type ") << alias.name << " = "
            << type_ref_text(alias.type_ref) << (native ? ";" : "");
        ++shown;
    }
    for (const EnumDecl& en : klass.enums) {
        if (shown >= max_members) {
            break;
        }
        out << "\n    enum class " << en.name;
        if (!en.values.empty()) {
            out << " { ";
            for (size_t index = 0; index < en.values.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << en.values[index].name;
            }
            out << " }";
        }
        ++shown;
    }
    for (const FieldDecl& field : klass.fields) {
        if (shown >= max_members) {
            break;
        }
        out << "\n    " << field.name << ": " << type_ref_text(field.type_ref);
        ++shown;
    }
    for (const FunctionDecl& method : klass.methods) {
        if (shown >= max_members) {
            break;
        }
        out << "\n    " << function_detail(method);
        ++shown;
    }
    const size_t total =
        klass.type_aliases.size() + klass.enums.size() + klass.fields.size() + klass.methods.size();
    const size_t remaining = total > shown ? total - shown : 0;
    if (remaining > 0) {
        out << "\n    # ... " << remaining << " more field" << (remaining == 1 ? "" : "s");
    }
    if (total == 0) {
        out << "\n    pass";
    }
    return out.str();
}

const ClassDecl* class_for_type_name(const Symbols& symbols, const std::string& name,
                                     bool& native) {
    const auto found = symbols.classes.find(name);
    if (found == symbols.classes.end()) {
        return nullptr;
    }
    native = native_class_decl_for_binding(symbols, name) != nullptr &&
             std::filesystem::path(found->second->location.file.str()).extension() != ".dd";
    return found->second;
}

std::string class_type_markdown(const Symbols& symbols, const ClassDecl& klass, bool native) {
    std::string markdown = fenced_code("dudu", class_definition_preview(klass, native));
    if (native) {
        if (!klass.native_metadata.declaration.empty()) {
            markdown += "\n\n" + fenced_code("cpp", klass.native_metadata.declaration);
        }
        const std::string identity = native_symbol_identity_key(klass.identity);
        if (!identity.empty()) {
            markdown += "\n\nNative identity: `" + identity + "`";
        }
    }
    if (!klass.doc_comment.empty()) {
        markdown += "\n\n" + documentation_markdown(klass.doc_comment);
    }
    if (const std::optional<TypeLayout> layout = resolved_class_layout(symbols, klass)) {
        markdown += "\n\nsize = " + std::to_string(layout->size) +
                    " bytes, align = " + std::to_string(layout->alignment) + " bytes";
    }
    if (native) {
        const std::string identity = native_symbol_identity_key(klass.identity);
        bool has_declared_path = false;
        if (!identity.empty()) {
            if (const std::optional<std::filesystem::path> path =
                    native_identity_source_path(identity)) {
                markdown += "\n\nDeclared in: `" + path->string() + "`";
                has_declared_path = true;
            }
        }
        if (!has_declared_path && !klass.location.file.empty()) {
            markdown += "\n\nDeclared in: `" + klass.location.file.str() + "`";
        }
    }
    return markdown;
}

} // namespace

std::optional<std::string> primitive_type_hover_json(const std::string& word) {
    const std::optional<PrimitiveTypeDoc> doc = primitive_type_doc(word);
    if (!doc) {
        return std::nullopt;
    }
    return markdown_hover_json(fenced_code("dudu", "type " + word) + "\n\n" + doc->description +
                               "\n\nC++ lowering: `" + doc->cpp_type + "`");
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
        for (const ClassDecl& klass : module.native_classes) {
            const bool same_location = klass.location.file == target->location.file.str() &&
                                       klass.location.line == target->location.line &&
                                       klass.location.column == target->location.column;
            if (same_location || klass.name == target->name) {
                return class_hover_json(module, klass, true);
            }
        }
        const std::string target_display = native_type_alias_type_text(type);
        std::string markdown = fenced_code("cpp", "native type = " + target_display) +
                               "\n\nresolves to `native class " + target_display + "`";
        const std::optional<TypeLayout> layout =
            type.layout ? type.layout
                        : (target->declaration == nullptr
                               ? std::nullopt
                               : target->declaration->layout);
        if (layout) {
            markdown += "\n\nsize = " + std::to_string(layout->size) +
                        " bytes, align = " + std::to_string(layout->alignment) + " bytes";
        }
        if (target->declaration != nullptr &&
            !target->declaration->native_metadata.declaration.empty()) {
            markdown += "\n\n" +
                        fenced_code("cpp", target->declaration->native_metadata.declaration);
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
    return std::nullopt;
}

std::string class_hover_json(const ModuleAst& module, const ClassDecl& klass, bool native) {
    const Symbols symbols = collect_symbols(module);
    std::string markdown = class_type_markdown(symbols, klass, native);
    if (native) {
        if (const std::string provenance = native_import_provenance(module, klass.name);
            !provenance.empty()) {
            markdown += "\n\n" + provenance;
        }
    }
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(klass.location) + "}";
}

std::string enum_hover_json(const EnumDecl& en) {
    std::ostringstream preview;
    preview << "enum " << en.name << ":";
    for (const EnumValueDecl& value : en.values) {
        preview << "\n    " << value.name;
        if (!value.payload_fields.empty()) {
            preview << ":";
            for (const EnumPayloadField& field : value.payload_fields) {
                preview << "\n        " << field.name << ": " << type_ref_text(field.type_ref);
            }
        }
    }
    std::string markdown = fenced_code("dudu", preview.str());
    if (!en.doc_comment.empty()) {
        markdown += "\n\n" + documentation_markdown(en.doc_comment);
    }
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(en.location) + "}";
}

std::string type_name_hover_markdown(const Symbols& symbols, const std::string& name) {
    bool native = false;
    if (const ClassDecl* klass = class_for_type_name(symbols, name, native)) {
        return class_type_markdown(symbols, *klass, native);
    }
    if (const NativeTypeDecl* type = native_type_decl_for_binding(symbols, name)) {
        std::string markdown = fenced_code("cpp", type->native_spelling.empty()
                                                      ? std::string("native type ") + type->name
                                                      : "native type " + type->name + " = " +
                                                            native_type_alias_type_text(*type));
        if (type->layout) {
            markdown += "\n\nsize = " + std::to_string(type->layout->size) +
                        " bytes, align = " + std::to_string(type->layout->alignment) + " bytes";
        }
        if (!type->doc_comment.empty()) {
            markdown += "\n\n" + type->doc_comment;
        }
        return markdown;
    }
    const std::optional<PrimitiveTypeDoc> doc = primitive_type_doc(name);
    if (!doc) {
        return {};
    }
    std::string markdown = fenced_code("dudu", "type " + name) + "\n\n" + doc->description +
                           "\n\nC++ lowering: `" + doc->cpp_type + "`";
    if (const std::optional<TypeLayout> layout = primitive_type_layout(name)) {
        markdown += "\n\nsize = " + std::to_string(layout->size) +
                    " bytes, align = " + std::to_string(layout->alignment) + " bytes";
    }
    return markdown;
}

SourceLocation type_name_definition_location(const Symbols& symbols, const std::string& name) {
    bool native = false;
    if (const ClassDecl* klass = class_for_type_name(symbols, name, native)) {
        return klass->location;
    }
    if (const NativeTypeDecl* type = native_type_decl_for_binding(symbols, name)) {
        return type->location;
    }
    return {};
}

} // namespace dudu
