#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_header_cache_deps.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_header_parse.hpp"
#include "dudu/native/native_header_scan_command.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_header_usr.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <algorithm>
#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void test_native_scan_dedupe_allows_opaque_redeclarations() {
    dudu::NativeHeaderScan scan;
    dudu::NativeTypeDecl left;
    left.name = "Thing";
    left.identity.canonical_path = "left.Thing";
    left.location = {.file = dudu::SourceFileName("left.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(left));
    dudu::NativeTypeDecl right;
    right.name = "Thing";
    right.identity.canonical_path = "right.Thing";
    right.location = {.file = dudu::SourceFileName("right.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(right));

    scan = dudu::dedupe_scan(std::move(scan));
    size_t count = 0;
    for (const dudu::NativeTypeDecl& type : scan.types) {
        if (type.name == "Thing") {
            ++count;
        }
    }
    assert(count == 1);
}

void test_native_scan_dedupes_function_redeclarations_by_usr() {
    dudu::NativeHeaderScan scan;
    for (const auto& [template_param, source] :
         {std::pair{"_RAIter", "algorithmfwd.h"},
          std::pair{"_RandomAccessIterator", "stl_algo.h"}}) {
        dudu::NativeFunctionDecl function;
        function.name = "std.sort";
        function.template_params = {template_param};
        function.template_param_is_value = {false};
        function.param_type_refs = {
            dudu::parse_type_text(template_param, {}),
            dudu::parse_type_text(template_param, {}),
        };
        function.return_type_ref = dudu::parse_type_text("void", {});
        function.identity.usr = "c:@N@std@FT@>1#Tsort#t0.0#S0_#v#";
        function.identity.canonical_path = "std.sort";
        function.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 1};
        scan.functions.push_back(std::move(function));
    }

    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.functions, [](const dudu::NativeFunctionDecl& function) {
               return function.name == "std.sort";
           }) == 1);
}

void test_native_scan_dedupe_rejects_alias_identity_collision() {
    dudu::NativeHeaderScan scan;
    dudu::NativeTypeDecl left;
    left.name = "Thing";
    left.native_spelling = "i32";
    left.type_ref = dudu::parse_type_text(
        "i32", {.file = dudu::SourceFileName("left.hpp"), .line = 1, .column = 8});
    left.identity.canonical_path = "left.Thing";
    left.location = {.file = dudu::SourceFileName("left.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(left));
    dudu::NativeTypeDecl right;
    right.name = "Thing";
    right.native_spelling = "f32";
    right.type_ref = dudu::parse_type_text(
        "f32", {.file = dudu::SourceFileName("right.hpp"), .line = 1, .column = 8});
    right.identity.canonical_path = "right.Thing";
    right.location = {.file = dudu::SourceFileName("right.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(right));

    bool failed = false;
    try {
        (void)dudu::dedupe_scan(std::move(scan));
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("native type name collision: Thing") !=
                 std::string::npos;
    }
    assert(failed);
}

void test_native_scan_dedupe_allows_equivalent_alias_redeclarations() {
    dudu::NativeHeaderScan scan;
    for (const std::string source : {"first.hpp", "second.hpp"}) {
        dudu::NativeTypeDecl alias;
        alias.name = "NativeSize";
        alias.native_spelling = "u64";
        alias.type_ref = dudu::parse_type_text(
            "u64", {.file = dudu::SourceFileName(source), .line = 1, .column = 8});
        alias.identity.canonical_path = source + ".NativeSize";
        alias.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 8};
        scan.types.push_back(std::move(alias));
    }

    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.types, [](const dudu::NativeTypeDecl& type) {
               return type.name == "NativeSize";
           }) == 1);
}

void test_native_scan_dedupe_resolves_alias_chains() {
    dudu::NativeHeaderScan scan;
    dudu::NativeTypeDecl bridge;
    bridge.name = "PlatformValue";
    bridge.native_spelling = "BuiltinValue";
    bridge.type_ref = dudu::parse_type_text("BuiltinValue", {});
    scan.types.push_back(std::move(bridge));

    for (const auto& [source, spelling] :
         {std::pair{"first.hpp", "BuiltinValue"}, std::pair{"second.hpp", "PlatformValue"}}) {
        dudu::NativeTypeDecl alias;
        alias.name = "PublicValue";
        alias.native_spelling = spelling;
        alias.type_ref = dudu::parse_type_text(spelling, {});
        alias.identity.canonical_path = std::string(source) + ".PublicValue";
        alias.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 8};
        scan.types.push_back(std::move(alias));
    }

    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.types, [](const dudu::NativeTypeDecl& type) {
               return type.name == "PublicValue";
           }) == 1);
}

dudu::NativeDeclarationMetadata rich_metadata() {
    return {
        .declaration = "template<class T> int inspect(T value = T{})",
        .summary_doc_comment = "Inspect a native value.",
        .return_doc_comment = "The inspection score.",
        .deprecated_message = "Use inspect_v2.",
        .parameters = {{.name = "value",
                        .default_value = "T{}",
                        .doc_comment = "Value to inspect."}},
        .template_parameters = {{.name = "T",
                                 .default_value = "int",
                                 .doc_comment = "Inspected value type."}},
    };
}

void test_native_redeclaration_merge_preserves_rich_metadata() {
    dudu::NativeFunctionDecl forward;
    forward.name = "inspect";
    forward.identity.usr = "c:@FT@>1#Tinspect#t0.0#I#";
    forward.return_type_ref = dudu::parse_type_text("i32", {});
    forward.param_type_refs = {dudu::parse_type_text("i32", {})};
    forward.param_names.resize(1);

    dudu::NativeFunctionDecl definition = forward;
    definition.doc_comment = "Inspect a value.";
    definition.param_names = {"value"};
    definition.native_metadata = rich_metadata();

    std::vector<dudu::NativeFunctionDecl> functions = {forward};
    dudu::append_unique_native_functions(functions, {definition});
    assert(functions.size() == 1);
    assert(functions[0].doc_comment == "Inspect a value.");
    assert(functions[0].param_names == std::vector<std::string>{"value"});
    assert(functions[0].native_metadata.declaration ==
           "template<class T> int inspect(T value = T{})");
    assert(functions[0].native_metadata.summary_doc_comment == "Inspect a native value.");
    assert(functions[0].native_metadata.parameters[0].default_value == "T{}");
    assert(functions[0].native_metadata.template_parameters[0].doc_comment ==
           "Inspected value type.");

    dudu::NativeTypeDecl type_forward;
    type_forward.name = "Widget";
    type_forward.identity.usr = "c:@S@Widget";
    dudu::NativeTypeDecl type_definition = type_forward;
    type_definition.doc_comment = "A native widget.";
    type_definition.native_metadata = rich_metadata();
    std::vector<dudu::NativeTypeDecl> types = {type_forward};
    dudu::append_unique_native_types(types, {type_definition});
    assert(types.size() == 1);
    assert(types[0].doc_comment == "A native widget.");
    assert(types[0].native_metadata.deprecated_message == "Use inspect_v2.");

    dudu::NativeValueDecl value_forward;
    value_forward.name = "WIDGET_LIMIT";
    value_forward.identity.usr = "c:@WIDGET_LIMIT";
    dudu::NativeValueDecl value_definition = value_forward;
    value_definition.doc_comment = "Maximum widget count.";
    value_definition.native_metadata = rich_metadata();
    std::vector<dudu::NativeValueDecl> values = {value_forward};
    dudu::append_unique_native_values(values, {value_definition});
    assert(values.size() == 1);
    assert(values[0].doc_comment == "Maximum widget count.");
    assert(values[0].native_metadata.return_doc_comment == "The inspection score.");

    dudu::NativeMacroDecl macro_forward;
    macro_forward.name = "WIDGET_AT";
    macro_forward.identity.usr = "macro:widget.hpp:WIDGET_AT";
    dudu::NativeMacroDecl macro_definition = macro_forward;
    macro_definition.arity = 2;
    macro_definition.function_like = true;
    macro_definition.param_names = {"items", "index"};
    macro_definition.doc_comment = "Access one widget.";
    std::vector<dudu::NativeMacroDecl> macros = {macro_forward};
    dudu::append_unique_native_macros(macros, {macro_definition});
    assert(macros.size() == 1);
    assert(macros[0].param_names == (std::vector<std::string>{"items", "index"}));
    assert(macros[0].doc_comment == "Access one widget.");

    dudu::NativeNamespaceDecl namespace_forward;
    namespace_forward.name = "widgets";
    namespace_forward.identity.usr = "c:@N@widgets";
    dudu::NativeNamespaceDecl namespace_definition = namespace_forward;
    namespace_definition.doc_comment = "Widget operations.";
    std::vector<dudu::NativeNamespaceDecl> namespaces = {namespace_forward};
    dudu::append_unique_native_namespaces(namespaces, {namespace_definition});
    assert(namespaces.size() == 1);
    assert(namespaces[0].doc_comment == "Widget operations.");
}

void test_native_class_redeclaration_merge_preserves_members_and_docs() {
    dudu::ClassDecl forward;
    forward.name = "Widget";
    forward.identity.usr = "c:@S@Widget";
    dudu::FieldDecl value_field;
    value_field.name = "value";
    value_field.type_ref = dudu::parse_type_text("i32", {});
    forward.fields.push_back(value_field);
    dudu::ConstDecl width_constant;
    width_constant.name = "WIDTH";
    width_constant.type_ref = dudu::parse_type_text("i32", {});
    forward.constants.push_back(width_constant);
    dudu::TypeAliasDecl value_alias;
    value_alias.name = "value_type";
    forward.type_aliases.push_back(value_alias);
    dudu::FunctionDecl forward_method;
    forward_method.name = "inspect";
    forward_method.receiver_type_ref = dudu::parse_type_text("&Widget", {});
    dudu::ParamDecl scale_param;
    scale_param.type_ref = dudu::parse_type_text("i32", {});
    forward_method.params.push_back(scale_param);
    forward_method.return_type_ref = dudu::parse_type_text("i32", {});
    forward.methods.push_back(forward_method);
    dudu::EnumDecl forward_enum;
    forward_enum.name = "Mode";
    dudu::EnumValueDecl fast_value;
    fast_value.name = "fast";
    forward_enum.values.push_back(fast_value);
    forward.enums.push_back(forward_enum);

    dudu::ClassDecl definition = forward;
    definition.doc_comment = "A fully defined widget.";
    definition.native_metadata = rich_metadata();
    definition.fields[0].doc_comment = "Stored widget value.";
    definition.constants[0].doc_comment = "Widget width.";
    definition.type_aliases[0].type_ref = dudu::parse_type_text("i32", {});
    definition.type_aliases[0].doc_comment = "Stored value type.";
    definition.methods[0].params[0].name = "scale";
    definition.methods[0].doc_comment = "Inspect this widget.";
    definition.methods[0].native_metadata = rich_metadata();
    definition.enums[0].doc_comment = "Widget processing mode.";
    definition.enums[0].values[0].doc_comment = "Fast mode.";
    dudu::EnumValueDecl safe_value;
    safe_value.name = "safe";
    safe_value.doc_comment = "Safe mode.";
    definition.enums[0].values.push_back(safe_value);

    std::vector<dudu::ClassDecl> classes = {forward};
    dudu::append_unique_native_classes(classes, {definition});
    assert(classes.size() == 1);
    const dudu::ClassDecl& merged = classes[0];
    assert(merged.doc_comment == "A fully defined widget.");
    assert(merged.native_metadata.declaration == "template<class T> int inspect(T value = T{})");
    assert(merged.fields[0].doc_comment == "Stored widget value.");
    assert(merged.constants[0].doc_comment == "Widget width.");
    assert(dudu::type_ref_text(merged.type_aliases[0].type_ref) == "i32");
    assert(merged.type_aliases[0].doc_comment == "Stored value type.");
    assert(merged.methods.size() == 1);
    assert(merged.methods[0].params[0].name == "scale");
    assert(merged.methods[0].doc_comment == "Inspect this widget.");
    assert(merged.methods[0].native_metadata.parameters[0].doc_comment == "Value to inspect.");
    assert(merged.enums.size() == 1);
    assert(merged.enums[0].doc_comment == "Widget processing mode.");
    assert(merged.enums[0].values.size() == 2);
    assert(merged.enums[0].values[0].doc_comment == "Fast mode.");
    assert(merged.enums[0].values[1].name == "safe");
}

} // namespace

int main() {
    try {
        test_native_scan_dedupe_allows_opaque_redeclarations();
        test_native_scan_dedupes_function_redeclarations_by_usr();
        test_native_scan_dedupe_rejects_alias_identity_collision();
        test_native_scan_dedupe_allows_equivalent_alias_redeclarations();
        test_native_scan_dedupe_resolves_alias_chains();
        test_native_redeclaration_merge_preserves_rich_metadata();
        test_native_class_redeclaration_merge_preserves_members_and_docs();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
