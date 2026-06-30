#include "dudu/lsp/language_server_hover.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_native_lookup.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_operator.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/project/module_names.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string fenced_code(std::string_view language, const std::string& code) {
    return "```" + std::string(language) + "\n" + code + "\n```";
}

std::optional<std::filesystem::path> native_identity_path(const std::string& identity) {
    if (!starts_with(identity, "usr:")) {
        return std::nullopt;
    }
    const size_t delimiter = identity.find("::", 4);
    if (delimiter == std::string::npos) {
        return std::nullopt;
    }
    std::filesystem::path path = identity.substr(4, delimiter - 4);
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

std::string hover_markdown(const Symbol& symbol) {
    const bool native = symbol.native_identity_key.has_value();
    std::string markdown = fenced_code(native ? "cpp" : "dudu", symbol.detail);
    if (symbol.native_identity_key.has_value()) {
        if (const std::optional<std::filesystem::path> path =
                native_identity_path(*symbol.native_identity_key)) {
            markdown += "\n\nDeclared in: `" + path->string() + "`";
        }
        markdown += "\n\nNative identity: `" + *symbol.native_identity_key + "`";
    }
    if (!symbol.doc_comment.empty()) {
        markdown += "\n\n" + symbol.doc_comment;
    }
    return markdown;
}

std::optional<std::string> primitive_hover_json(const std::string& word) {
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
    };
    const auto found = primitives.find(word);
    if (found == primitives.end()) {
        return std::nullopt;
    }
    const std::string markdown = fenced_code("dudu", "type " + word) + "\n\n" +
                                 found->second.second + "\n\nC++ lowering: `" +
                                 found->second.first + "`";
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) + "\"}}";
}

std::optional<std::string> imported_module_hover_json(const ProjectIndex& index,
                                                      const ModuleAst& current,
                                                      const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::Module || bound_import_name(import) != word) {
            continue;
        }
        const ModuleAst* imported = index.imported_unit(current, import);
        if (imported == nullptr) {
            continue;
        }
        std::string markdown = fenced_code("dudu", "module " + import.module_path);
        if (!imported->doc_comment.empty()) {
            markdown += "\n\n" + imported->doc_comment;
        }
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
               "\"},\"range\":" + range_json(import.location) + "}";
    }
    return std::nullopt;
}

std::optional<std::string> imported_symbol_hover_json(const ProjectIndex& index,
                                                      const ModuleAst& current,
                                                      const std::string& word) {
    if (word.empty()) {
        return std::nullopt;
    }
    for (const ImportDecl& import : current.imports) {
        std::string target;
        if (import.kind == ImportKind::Module) {
            const std::string bound = bound_import_name(import);
            const std::vector<std::string> prefixes =
                import.alias.empty() ? std::vector<std::string>{import.module_path, bound}
                                     : std::vector<std::string>{bound};
            for (const std::string& prefix : prefixes) {
                if (word.rfind(prefix + ".", 0) == 0) {
                    target = word.substr(prefix.size() + 1);
                    break;
                }
            }
            if (target.empty()) {
                continue;
            }
        } else if (import.kind == ImportKind::From) {
            const std::string bound = bound_import_name(import);
            if (bound == word) {
                target = import.imported_name;
            } else if (word.rfind(bound + ".", 0) == 0) {
                target = import.imported_name + word.substr(bound.size());
            } else {
                continue;
            }
        } else {
            continue;
        }
        const ModuleAst* imported = index.imported_unit(current, import);
        if (imported == nullptr) {
            continue;
        }
        for (const Symbol& symbol : symbols_for_module(*imported, false)) {
            if (symbol.name != target) {
                continue;
            }
            return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" +
                   json_escape(hover_markdown(symbol)) + "\"}}";
        }
    }
    return std::nullopt;
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
        std::string markdown =
            fenced_code("cpp", "native type = " + native_type_alias_type_text(type)) +
            "\n\nresolves to `" + "native class " + target->name + "`";
        const std::string identity = native_symbol_identity_key(type.identity);
        if (!identity.empty()) {
            if (const std::optional<std::filesystem::path> path = native_identity_path(identity)) {
                markdown += "\n\nDeclared in: `" + path->string() + "`";
            }
            markdown += "\n\nNative identity: `" + identity + "`";
        }
        const std::string doc_comment =
            !type.doc_comment.empty() ? type.doc_comment : target->doc_comment;
        const std::string doc_text = doc_comment.empty() ? std::string{} : "\n\n" + doc_comment;
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
               json_escape(doc_text) + "\"},\"range\":" + range_json(type.location) + "}";
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

std::string symbol_hover_json(const Symbol& symbol) {
    const std::string markdown = hover_markdown(symbol);
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(symbol.location) + "}";
}

std::optional<std::string> constructor_hover_json(const ModuleAst& module,
                                                  const AstSelection& selection) {
    if (!selection.call_callee) {
        return std::nullopt;
    }
    std::vector<std::string> candidates;
    if (selection.symbol) {
        candidates.push_back(*selection.symbol);
    }
    if (selection.expr_path && !selection.expr_path->segments.empty()) {
        candidates.push_back(selection.expr_path->segments.back().text);
    }
    if (selection.symbol_path) {
        candidates.push_back(*selection.symbol_path);
    }
    std::ranges::sort(candidates);
    candidates.erase(std::ranges::unique(candidates).begin(), candidates.end());
    const auto find_constructor =
        [&](const std::vector<ClassDecl>& classes) -> std::optional<std::string> {
        for (const ClassDecl& klass : classes) {
            if (std::ranges::find(candidates, klass.name) == candidates.end()) {
                continue;
            }
            SourceLocation location = klass.location;
            std::optional<std::string> native_identity;
            for (const FunctionDecl& method : klass.methods) {
                if (!is_constructor_method_name(method.name)) {
                    continue;
                }
                location = method.location;
                const std::string identity = native_symbol_identity_key(method.native_identity);
                if (!identity.empty()) {
                    native_identity = identity;
                }
                break;
            }
            if (!native_identity) {
                const std::string identity = native_symbol_identity_key(klass.identity);
                if (!identity.empty()) {
                    native_identity = identity;
                }
            }
            return symbol_hover_json({.name = klass.name,
                                      .detail = constructor_detail(klass),
                                      .location = location,
                                      .kind = lsp_symbol_kind::Constructor,
                                      .native_identity_key = native_identity,
                                      .doc_comment = constructor_doc_comment(klass)});
        }
        return std::nullopt;
    };
    if (const std::optional<std::string> native = find_constructor(module.native_classes)) {
        return native;
    }
    return find_constructor(module.classes);
}

std::optional<std::string> member_hover_json(const ExprPath& path, const Json* params,
                                             const ModuleAst& current, const ModuleAst& module) {
    if (params == nullptr || path.segments.size() < 2 ||
        path.segments.front().kind != ExprPathSegmentKind::Name ||
        path.segments.back().kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const std::string& receiver = path.segments.front().text;
    const std::string& member = path.segments.back().text;
    const TypeRef type_ref = local_type_ref_before_cursor(current, receiver, params);
    if (!has_type_ref(type_ref)) {
        return std::nullopt;
    }
    const std::set<std::string> candidate_types = member_candidate_types(module, type_ref);
    const auto find_member = [&](const std::vector<ClassDecl>& classes,
                                 bool native) -> std::optional<std::string> {
        for (const ClassDecl& klass : classes) {
            if (!candidate_types.contains(klass.name)) {
                continue;
            }
            for (const FieldDecl& field : klass.fields) {
                if (field.name == member) {
                    return symbol_hover_json(
                        {.name = field.name,
                         .detail = field.name + ": " + type_ref_text(field.type_ref),
                         .location = field.location,
                         .kind = lsp_symbol_kind::Field,
                         .native_identity_key =
                             native ? native_class_member_identity_key(klass, field.name)
                                    : std::nullopt,
                         .doc_comment = field.doc_comment});
                }
            }
            for (const ConstDecl& constant : klass.constants) {
                if (constant.name == member) {
                    return symbol_hover_json(
                        {.name = constant.name,
                         .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                         .location = constant.location,
                         .kind = lsp_symbol_kind::Constant,
                         .native_identity_key =
                             native ? native_class_member_identity_key(klass, constant.name)
                                    : std::nullopt,
                         .doc_comment = constant.doc_comment});
                }
            }
            for (const ConstDecl& field : klass.static_fields) {
                if (field.name == member) {
                    return symbol_hover_json(
                        {.name = field.name,
                         .detail = field.name + ": " + type_ref_text(field.type_ref),
                         .location = field.location,
                         .kind = lsp_symbol_kind::Field,
                         .native_identity_key =
                             native ? native_class_member_identity_key(klass, field.name)
                                    : std::nullopt,
                         .doc_comment = field.doc_comment});
                }
            }
            for (const FunctionDecl& method : klass.methods) {
                if (method.name == member) {
                    return symbol_hover_json(
                        {.name = method.name,
                         .detail = function_detail(method),
                         .location = method.location,
                         .kind = is_constructor_method_name(method.name)
                                     ? lsp_symbol_kind::Constructor
                                     : lsp_symbol_kind::Method,
                         .native_identity_key =
                             native ? native_identity_key(method.native_identity) : std::nullopt,
                         .doc_comment = method.doc_comment});
                }
            }
        }
        return std::nullopt;
    };
    if (const std::optional<std::string> native = find_member(module.native_classes, true)) {
        return native;
    }
    return find_member(module.classes, false);
}

} // namespace

std::string hover_json(const Document& doc, const std::string& word, const Json* params,
                       std::optional<ExprPath> selected_path) {
    const ProjectIndex* index = nullptr;
    try {
        index = &project_index_for_document(doc, false);
    } catch (const std::exception&) {
        return "null";
    }
    const ModuleAst& current = index->visible_unit_for_path(doc.path);
    std::string query = word;
    AstSelection selection;
    bool has_selection = false;
    if ((query.empty() || !selected_path.has_value()) && params != nullptr) {
        selection = ast_selection_at(current, params);
        has_selection = true;
        if (query.empty()) {
            query = selection.symbol_path.value_or("");
        }
        if (!selected_path.has_value()) {
            selected_path = selection.expr_path;
        }
    }
    if (const std::optional<std::string> primitive = primitive_hover_json(query)) {
        return *primitive;
    }
    if (has_selection) {
        if (selection.operator_expr) {
            if (const std::optional<Symbol> op = dudu_operator_symbol_for_expr(
                    current, *selection.operator_expr, lsp_position(params).line + 1)) {
                return symbol_hover_json(*op);
            }
        }
        if (const std::optional<std::string> constructor =
                constructor_hover_json(current, selection)) {
            return *constructor;
        }
    }
    const std::vector<Symbol> symbols = symbols_for_module(current, false);
    if (const std::optional<Symbol> exact = exact_symbol_match(symbols, query)) {
        return symbol_hover_json(*exact);
    }
    if (const std::optional<std::string> module_hover =
            imported_module_hover_json(*index, current, query)) {
        return *module_hover;
    }
    if (const std::optional<std::string> imported =
            imported_symbol_hover_json(*index, current, query)) {
        return *imported;
    }
    const ProjectIndex* native_index = nullptr;
    const auto load_native_index = [&]() -> const ProjectIndex* {
        if (native_index == nullptr) {
            native_index = &project_index_for_document(doc, true);
        }
        return native_index;
    };
    if (selected_path.has_value()) {
        if (const std::optional<Symbol> class_member =
                class_member_symbol_for_path(current, *selected_path)) {
            return symbol_hover_json(*class_member);
        }
    }
    try {
        const ProjectIndex* native = load_native_index();
        const ModuleAst& native_visible = native->visible_unit_for_path(doc.path);
        if (has_selection) {
            if (const std::optional<std::string> constructor =
                    constructor_hover_json(native_visible, selection)) {
                return *constructor;
            }
        }
        if (selected_path.has_value()) {
            if (const std::optional<Symbol> class_member =
                    class_member_symbol_for_path(native_visible, *selected_path)) {
                return symbol_hover_json(*class_member);
            }
        }
        if (const std::optional<std::string> native_alias =
                native_alias_hover_json(query, native_visible)) {
            return *native_alias;
        }
        const std::vector<Symbol> native_symbols = symbols_for_module(native_visible, true);
        if (has_selection) {
            if (const std::optional<Symbol> native_namespace =
                    native_namespace_segment_symbol(native_symbols, selection.symbol, query)) {
                return symbol_hover_json(*native_namespace);
            }
        }
        if (const std::optional<Symbol> exact = exact_symbol_match(native_symbols, query)) {
            return symbol_hover_json(*exact);
        }
        if (const std::optional<Symbol> suffix =
                unambiguous_suffix_symbol_match(native_symbols, query)) {
            return symbol_hover_json(*suffix);
        }
    } catch (const std::exception&) {
    }
    if (selected_path.has_value()) {
        try {
            const ProjectIndex* native = load_native_index();
            const ModuleAst& native_visible = native->visible_unit_for_path(doc.path);
            if (const std::optional<std::string> member_hover =
                    member_hover_json(*selected_path, params, current, native_visible)) {
                return *member_hover;
            }
        } catch (const std::exception&) {
        }
    }
    if (const std::optional<Symbol> suffix = unambiguous_suffix_symbol_match(symbols, query)) {
        return symbol_hover_json(*suffix);
    }
    std::string local_type =
        !query.empty() && params != nullptr
            ? substitute_type_ref_text(local_type_ref_before_cursor(current, query, params), {})
            : std::string{};
    if (local_type.empty() && !query.empty() && params != nullptr) {
        try {
            const ProjectIndex* native = load_native_index();
            local_type = substitute_type_ref_text(
                local_type_ref_before_cursor(native->visible_unit_for_path(doc.path), query,
                                             params),
                {});
        } catch (const std::exception&) {
        }
    }
    if (!local_type.empty()) {
        const std::string markdown = fenced_code("dudu", query + ": " + local_type);
        return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) + "\"}}";
    }
    return "null";
}

} // namespace dudu
