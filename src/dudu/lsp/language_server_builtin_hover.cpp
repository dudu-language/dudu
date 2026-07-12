#include "dudu/lsp/language_server_builtin_hover.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

struct BuiltinFunctionDoc {
    std::string signature;
    std::string description;
};

std::string fenced_code(std::string_view language, const std::string& code) {
    return "```" + std::string(language) + "\n" + code + "\n```";
}

std::string hover_json_from_markdown(const std::string& markdown) {
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) + "\"}}";
}

std::optional<BuiltinFunctionDoc> builtin_function_doc(const std::string& name) {
    static const std::map<std::string, BuiltinFunctionDoc> docs = {
        {"delete",
         {"delete(ptr: *T) -> void",
          "Destroys and frees a heap object created with `new[T](...)`."}},
        {"free", {"free(ptr: *T) -> void", "Frees memory acquired from native allocation APIs."}},
        {"len",
         {"len(value) -> usize",
          "Returns the length of a Dudu container, string, span, array, or supported native "
          "container."}},
        {"max", {"max[T](left: T, right: T) -> T", "Returns the larger of two comparable values."}},
        {"min",
         {"min[T](left: T, right: T) -> T", "Returns the smaller of two comparable values."}},
        {"move",
         {"move[T](value: T) -> T",
          "Explicitly transfers a value into its destination using C++ move semantics. The "
          "source remains valid but may be in a moved-from state."}},
        {"print", {"print(value...) -> void", "Writes values to stdout followed by a newline."}},
        {"range",
         {"range(stop: i32) -> range\nrange(start: i32, stop: i32, step: i32 = 1) -> range",
          "Builds an integer range iterable for `for` loops."}},
        {"assume_shape",
         {"assume_shape[T](value) -> T",
          "Narrows runtime-known shaped metadata after user or library code has checked the "
          "value's dimensions. `T` must include shape metadata."}},
    };
    const auto found = docs.find(name);
    if (found == docs.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool is_builtin_function_selection(const AstSelection& selection, const std::string& query,
                                   const std::string& name) {
    if (!selection.call_callee) {
        return query == name;
    }
    if (selection.symbol_path) {
        return *selection.symbol_path == name;
    }
    return selection.symbol == name || query == name;
}

bool builtin_function_is_shadowed(const std::string& name, const ModuleAst& current,
                                  const Document& doc, const Json* params) {
    if (has_type_ref(local_type_ref_before_cursor(current, doc, name, params))) {
        return true;
    }
    return std::ranges::any_of(current.functions,
                               [&](const FunctionDecl& fn) { return fn.name == name; });
}

bool template_head_is(const TypeRef& type, std::initializer_list<std::string_view> names) {
    if (type.kind != TypeKind::Template) {
        return false;
    }
    for (std::string_view name : names) {
        if (type.name == name) {
            return true;
        }
    }
    return false;
}

bool type_head_is(const TypeRef& type, std::initializer_list<std::string_view> names) {
    const std::string head = type_ref_head_name(type);
    for (std::string_view name : names) {
        if (head == name) {
            return true;
        }
    }
    return false;
}

std::string receiver_kind(const TypeRef& type) {
    const std::string head = type_ref_head_name(type);
    if (type_head_is(type, {"str", "string", "string_view", "std.string", "std.string_view",
                            "std::string", "std::string_view"}) ||
        head.find("basic_string") != std::string::npos) {
        return "string";
    }
    if (template_head_is(type, {"list", "std.vector", "std::vector"})) {
        return "list";
    }
    if (template_head_is(
            type, {"set", "std.set", "std::set", "std.unordered_set", "std::unordered_set"})) {
        return "set";
    }
    if (template_head_is(
            type, {"dict", "std.map", "std::map", "std.unordered_map", "std::unordered_map"})) {
        return "dict";
    }
    if (template_head_is(type, {"Option", "std.optional", "std::optional"})) {
        return "Option";
    }
    if (unary_type_child_ref(type, TypeKind::Atomic) ||
        template_head_is(type, {"std.atomic", "std::atomic"})) {
        return "atomic";
    }
    return {};
}

std::string param_name(const std::string& kind, const std::string& method, size_t index) {
    if (method == "append" || method == "push_back" || method == "insert" || method == "store") {
        return "value";
    }
    if (method == "contains" && kind == "dict") {
        return "key";
    }
    if (method == "contains") {
        return "value";
    }
    if (method == "resize" || method == "reserve") {
        return "count";
    }
    return "arg" + std::to_string(index);
}

std::string render_method_signature(const std::string& kind, const std::string& method,
                                    const FunctionSignature& signature) {
    std::ostringstream out;
    out << method << "(";
    const size_t count = signature_param_count(signature);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << param_name(kind, method, i) << ": "
            << type_ref_text(signature_param_type_ref(signature, i));
    }
    out << ") -> " << type_ref_text(signature_return_type_ref(signature));
    return out.str();
}

std::string method_description(const std::string& kind, const std::string& method,
                               const TypeRef& receiver_type) {
    const std::string receiver = type_ref_text(receiver_type);
    if (kind == "list") {
        if (method == "append") {
            return "Appends a value to the end of `" + receiver +
                   "`. Lowers to `std::vector<T>::push_back`.";
        }
        if (method == "push_back") {
            return "Appends a value to the end of `" + receiver + "`.";
        }
        if (method == "resize" || method == "reserve") {
            return "Adjusts storage for `" + receiver + "`.";
        }
        if (method == "front" || method == "back") {
            return "Returns an element from `" + receiver + "`.";
        }
        if (method == "pop_back") {
            return "Removes the last element from `" + receiver + "`.";
        }
    }
    if (kind == "string") {
        return "String method on `" + receiver + "`.";
    }
    if (kind == "set" || kind == "dict") {
        return "Container method on `" + receiver + "`.";
    }
    if (kind == "Option") {
        return "Optional-value method on `" + receiver + "`.";
    }
    if (kind == "atomic") {
        return "Atomic operation on `" + receiver + "`.";
    }
    return "Builtin method on `" + receiver + "`.";
}

} // namespace

std::optional<std::string> builtin_function_hover_json(const AstSelection& selection,
                                                       const std::string& query,
                                                       const ModuleAst& current,
                                                       const Document& doc, const Json* params) {
    static const std::vector<std::string> names = {"delete", "free", "len",   "max",
                                                   "min",    "move", "print", "range"};
    for (const std::string& name : names) {
        if (!is_builtin_function_selection(selection, query, name)) {
            continue;
        }
        if (builtin_function_is_shadowed(name, current, doc, params)) {
            return std::nullopt;
        }
        const std::optional<BuiltinFunctionDoc> doc = builtin_function_doc(name);
        if (!doc) {
            return std::nullopt;
        }
        return hover_json_from_markdown(fenced_code("dudu", doc->signature) + "\n\n" +
                                        doc->description);
    }
    return std::nullopt;
}

std::optional<std::string> builtin_member_hover_json(const ExprPath& path, const Json* params,
                                                     const ModuleAst& current) {
    if (params == nullptr || path.segments.size() < 2 ||
        path.segments.front().kind != ExprPathSegmentKind::Name ||
        path.segments.back().kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const std::string& receiver = path.segments.front().text;
    const std::string& method = path.segments.back().text;
    const TypeRef receiver_type = local_type_ref_before_cursor(current, receiver, params);
    if (!has_type_ref(receiver_type)) {
        return std::nullopt;
    }
    const Symbols symbols = collect_symbols(current);
    FunctionSignature signature;
    if (!builtin_cpp_method_signature(symbols, receiver_type, method, signature)) {
        return std::nullopt;
    }
    const TypeRef normalized_receiver = receiver_template_type_ref(symbols, receiver_type);
    const std::string kind = receiver_kind(normalized_receiver);
    if (kind.empty()) {
        return std::nullopt;
    }
    const std::string markdown =
        fenced_code("dudu", render_method_signature(kind, method, signature)) + "\n\n" +
        method_description(kind, method, normalized_receiver);
    return hover_json_from_markdown(markdown);
}

} // namespace dudu
