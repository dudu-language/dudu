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

} // namespace

int main() {
    try {
        test_native_scan_dedupe_allows_opaque_redeclarations();
        test_native_scan_dedupes_function_redeclarations_by_usr();
        test_native_scan_dedupe_rejects_alias_identity_collision();
        test_native_scan_dedupe_allows_equivalent_alias_redeclarations();
        test_native_scan_dedupe_resolves_alias_chains();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
