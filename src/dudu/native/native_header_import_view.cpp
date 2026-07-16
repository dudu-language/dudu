#include "dudu/native/native_header_import_view.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/native/native_header_scan_command.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <vector>

namespace dudu {
namespace {

bool public_direct_macro_name(const std::string& name) {
    return !name.empty() &&
           (std::isupper(static_cast<unsigned char>(name.front())) != 0 || name.front() == '_');
}

bool builtin_native_type_name(const std::string& name) {
    static const std::set<std::string> builtins = {
        "bool",  "char",  "i8",  "i16", "i32",  "i64", "u8",   "u16",  "u32",  "u64",
        "isize", "usize", "f32", "f64", "void", "str", "cstr", "auto", "None",
    };
    return builtins.contains(name);
}

std::string macro_raw_name(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string trim_macro_doc_line(std::string text) {
    text = trim_copy(std::move(text));
    if (text.starts_with("///")) {
        return trim_copy(text.substr(3));
    }
    if (text.starts_with("//")) {
        return trim_copy(text.substr(2));
    }
    if (text.starts_with("/**")) {
        text = trim_copy(text.substr(3));
    } else if (text.starts_with("/*")) {
        text = trim_copy(text.substr(2));
    }
    if (text.ends_with("*/")) {
        text = trim_copy(text.substr(0, text.size() - 2));
    }
    if (text.starts_with("*")) {
        text = trim_copy(text.substr(1));
    }
    return text;
}

std::string macro_doc_before_line(const std::vector<std::string>& lines, size_t define_index) {
    std::vector<std::string> pending;
    bool in_block = false;
    for (size_t cursor = define_index; cursor > 0;) {
        --cursor;
        std::string line = trim_copy(lines[cursor]);
        if (line.empty()) {
            break;
        }
        if (in_block) {
            pending.push_back(trim_macro_doc_line(line));
            if (line.find("/*") != std::string::npos) {
                break;
            }
            continue;
        }
        if (line.starts_with("//")) {
            pending.push_back(trim_macro_doc_line(line));
            continue;
        }
        if (line.find("*/") != std::string::npos) {
            pending.push_back(trim_macro_doc_line(line));
            if (line.find("/*") != std::string::npos) {
                break;
            }
            in_block = true;
            continue;
        }
        break;
    }
    std::reverse(pending.begin(), pending.end());
    std::string doc;
    for (const std::string& line : pending) {
        if (line.empty()) {
            continue;
        }
        if (!doc.empty()) {
            doc += '\n';
        }
        doc += line;
    }
    return doc;
}

std::optional<SourceLocation> macro_definition_location(const std::filesystem::path& header,
                                                        const std::vector<std::string>& lines,
                                                        const std::string& name) {
    const std::regex define_pattern("^\\s*#\\s*define\\s+" + name + "(\\b|\\()");
    for (size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        if (!std::regex_search(line, define_pattern)) {
            continue;
        }
        const size_t column = line.find(name);
        return SourceLocation{
            .file = SourceFileName(header.string()),
            .line = static_cast<int>(index + 1),
            .column = column == std::string::npos ? 1 : static_cast<int>(column + 1),
        };
    }
    return std::nullopt;
}

std::set<std::string> native_type_names(const NativeHeaderScan& scan) {
    std::set<std::string> names;
    for (const NativeTypeDecl& type : scan.types) {
        names.insert(type.name);
    }
    for (const ClassDecl& klass : scan.classes) {
        names.insert(klass.name);
    }
    return names;
}

void prefix_type_ref(TypeRef& type, const std::set<std::string>& names, const std::string& prefix) {
    if (names.contains(type.name.str()) && !builtin_native_type_name(type.name.str()) &&
        !type.name.starts_with(prefix + ".")) {
        type.name = prefix + "." + type.name.str();
    }
    for (TypeRef& child : type.children) {
        prefix_type_ref(child, names, prefix);
    }
}

void prefix_native_type_refs(NativeValueDecl& value, const std::set<std::string>& names,
                             const std::string& prefix) {
    prefix_type_ref(value.type_ref, names, prefix);
}

void prefix_native_type_refs(NativeTypeDecl& type, const std::set<std::string>& names,
                             const std::string& prefix) {
    prefix_type_ref(type.type_ref, names, prefix);
}

void prefix_native_type_refs(NativeFunctionDecl& function, const std::set<std::string>& names,
                             const std::string& prefix) {
    for (TypeRef& param : function.param_type_refs) {
        prefix_type_ref(param, names, prefix);
    }
    prefix_type_ref(function.return_type_ref, names, prefix);
}

void prefix_native_type_refs(ClassDecl& klass, const std::set<std::string>& names,
                             const std::string& prefix) {
    for (BaseClassDecl& base : klass.base_class_refs) {
        prefix_type_ref(base.type_ref, names, prefix);
    }
    for (FieldDecl& field : klass.fields) {
        prefix_type_ref(field.type_ref, names, prefix);
    }
    for (ConstDecl& constant : klass.constants) {
        prefix_type_ref(constant.type_ref, names, prefix);
    }
    for (ConstDecl& field : klass.static_fields) {
        prefix_type_ref(field.type_ref, names, prefix);
    }
    for (FunctionDecl& method : klass.methods) {
        prefix_type_ref(method.receiver_type_ref, names, prefix);
        for (ParamDecl& param : method.params) {
            prefix_type_ref(param.type_ref, names, prefix);
        }
        prefix_type_ref(method.return_type_ref, names, prefix);
    }
}

template <typename T>
std::vector<T> prefixed_type_refs(std::vector<T> items, const std::set<std::string>& names,
                                  const std::string& prefix) {
    for (T& item : items) {
        prefix_native_type_refs(item, names, prefix);
    }
    return items;
}

template <typename T>
std::vector<T> prefixed_names(const std::vector<T>& source, const std::string& prefix) {
    std::vector<T> out;
    out.reserve(source.size());
    for (T item : source) {
        if (!starts_with(item.name, prefix + ".")) {
            item.name = prefix + "." + item.name;
        }
        out.push_back(std::move(item));
    }
    return out;
}

std::vector<NativeTypeDecl> prefixed_type_names(const std::vector<NativeTypeDecl>& source,
                                                const std::string& prefix,
                                                const std::set<std::string>& c_record_names) {
    std::vector<NativeTypeDecl> out;
    out.reserve(source.size());
    for (NativeTypeDecl item : source) {
        const std::string original = item.name;
        if (!starts_with(item.name, prefix + ".")) {
            item.name = prefix + "." + item.name;
        }
        if (item.native_spelling.empty()) {
            item.native_spelling =
                c_record_names.contains(original) ? "struct " + original : original;
            item.type_ref = named_type_ref(original, item.location);
        }
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace

void attach_native_macro_definition_locations(NativeHeaderScan& scan, const ImportDecl& import,
                                              const NativeHeaderOptions& options) {
    const std::optional<std::filesystem::path> header =
        resolve_existing_native_header_path(import, options);
    if (!header.has_value()) {
        return;
    }
    const std::vector<std::string> lines = read_lines(*header);
    std::map<std::string, SourceLocation> locations;
    std::map<std::string, std::string> docs;
    for (NativeMacroDecl& macro : scan.macros) {
        const std::string raw_name = macro_raw_name(macro.name);
        if (const auto found = locations.find(raw_name); found != locations.end()) {
            macro.location = found->second;
            if (const auto doc = docs.find(raw_name); doc != docs.end()) {
                macro.doc_comment = doc->second;
            }
            continue;
        }
        const std::optional<SourceLocation> location =
            macro_definition_location(*header, lines, raw_name);
        if (!location.has_value()) {
            continue;
        }
        locations.emplace(raw_name, *location);
        macro.location = *location;
        const std::string doc =
            macro_doc_before_line(lines, static_cast<size_t>(location->line - 1));
        docs.emplace(raw_name, doc);
        macro.doc_comment = doc;
    }
    for (NativeFunctionDecl& function : scan.functions) {
        const std::string raw_name = macro_raw_name(function.name);
        const auto doc = docs.find(raw_name);
        if (doc == docs.end() || doc->second.empty()) {
            continue;
        }
        const bool macro_stub =
            function.identity.canonical_path == raw_name &&
            function.return_native_spelling == "auto" &&
            std::ranges::all_of(function.param_native_spellings,
                                [](const std::string& param) { return param == "auto"; });
        if (macro_stub) {
            function.doc_comment = doc->second;
        }
    }
}

NativeHeaderScan direct_native_import_view(const NativeHeaderScan& scan) {
    NativeHeaderScan out = scan;
    out.macros.clear();
    for (const NativeMacroDecl& macro : scan.macros) {
        if (public_direct_macro_name(macro.name)) {
            out.macros.push_back(macro);
        }
    }
    return out;
}

NativeHeaderScan aliased_native_import_view(const NativeHeaderScan& scan,
                                            const ImportDecl& import) {
    const std::set<std::string> type_names = native_type_names(scan);
    std::set<std::string> c_record_names;
    if (import.kind == ImportKind::ForeignC) {
        for (const ClassDecl& klass : scan.classes) {
            c_record_names.insert(klass.name);
        }
    }

    NativeHeaderScan out;
    out.types = prefixed_type_refs(prefixed_type_names(scan.types, import.alias, c_record_names),
                                   type_names, import.alias);
    out.classes =
        prefixed_type_refs(prefixed_names(scan.classes, import.alias), type_names, import.alias);
    out.values =
        prefixed_type_refs(prefixed_names(scan.values, import.alias), type_names, import.alias);
    out.functions =
        prefixed_type_refs(prefixed_names(scan.functions, import.alias), type_names, import.alias);
    out.macros = prefixed_names(scan.macros, import.alias);
    out.namespaces = prefixed_names(scan.namespaces, import.alias);
    return out;
}

} // namespace dudu
