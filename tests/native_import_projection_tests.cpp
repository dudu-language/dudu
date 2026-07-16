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

void test_native_scan_retries_with_c_prelude_for_context_headers(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-context-header-scan";
    const std::filesystem::path header = source_dir / "needs_c_context.h";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "struct DuduNeedsContext {\n"
               "    size_t count;\n"
               "    int state;\n"
               "};\n";
    }

    dudu::ModuleAst module = dudu::parse_source("from c.path import ./needs_c_context.h as native\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    value: native.DuduNeedsContext\n"
                                                "    value.count = 7\n"
                                                "    value.state = 35\n"
                                                "    return i32(value.count) + value.state\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("value.count = 7;") != std::string::npos);
    assert(cpp.find("value.state = 35;") != std::string::npos);
}

void test_aliased_c_import_prefixes_visible_transitive_functions(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-transitive-c-filter";
    const std::filesystem::path header = source_dir / "wrap_stdio.h";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "#include <stdio.h>\n"
               "static inline int dudu_wrap_answer(void) { return 42; }\n";
    }

    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::ModuleAst positive = dudu::parse_source("from c.path import ./wrap_stdio.h as wrap\n"
                                                  "\n"
                                                  "def main() -> i32:\n"
                                                  "    return wrap.dudu_wrap_answer()\n",
                                                  source_dir / "positive.dd");
    dudu::merge_native_headers(positive, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(positive, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(positive);
    assert(cpp.find("dudu_wrap_answer()") != std::string::npos);

    dudu::ModuleAst transitive = dudu::parse_source("from c.path import ./wrap_stdio.h as wrap\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    return wrap.remove(\"x\")\n",
                                                    source_dir / "transitive.dd");
    dudu::merge_native_headers(transitive, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(transitive, {.check_bodies = true});
    const std::string transitive_cpp = dudu::emit_cpp_source(transitive);
    assert(transitive_cpp.find("remove(\"x\")") != std::string::npos);
}

void test_native_scan_ignores_anonymous_record_definitions() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "|-CXXRecordDecl 0x1 <test.hpp:10:5, line:13:5> line:10:5 union "
        "definition\n"
        "|-CXXRecordDecl 0x2 <test.hpp:20:5, line:23:5> line:20:5 struct "
        "definition\n"
        "|-CXXRecordDecl 0x3 <test.hpp:30:1, line:32:1> line:30:8 struct "
        "NamedThing definition\n",
        {.file = dudu::SourceFileName("native_anonymous.dd"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));
    bool saw_named = false;
    for (const dudu::NativeTypeDecl& type : scan.types) {
        assert(type.name != "definition");
        saw_named = saw_named || type.name == "NamedThing";
    }
    assert(saw_named);
}

void test_native_scan_preserves_scoped_enum_owners() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(scan,
                         "`-NamespaceDecl 0x1 <test.hpp:1:1, line:8:1> line:1:11 ns\n"
                         "  |-EnumDecl 0x2 <line:2:1, col:27> col:12 class First 'int'\n"
                         "  | |-EnumConstantDecl 0x3 <col:20> col:20 Same 'ns::First'\n"
                         "  | `-EnumConstantDecl 0x4 <col:26> col:26 FirstOnly 'ns::First'\n"
                         "  |-EnumDecl 0x5 <line:3:1, col:28> col:12 class Second 'int'\n"
                         "  | |-EnumConstantDecl 0x6 <col:21> col:21 Same 'ns::Second'\n"
                         "  | `-EnumConstantDecl 0x7 <col:27> col:27 SecondOnly 'ns::Second'\n"
                         "  `-EnumDecl 0x8 <line:4:1, col:24> col:8 Plain\n"
                         "    `-EnumConstantDecl 0x9 <col:16> col:16 PlainValue 'ns::Plain'\n",
                         {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto has_type = [&](const std::string& name) {
        return std::ranges::any_of(
            scan.types, [&](const dudu::NativeTypeDecl& type) { return type.name == name; });
    };
    const auto value_type = [&](const std::string& name) -> std::string {
        for (const dudu::NativeValueDecl& value : scan.values) {
            if (value.name == name) {
                return dudu::type_ref_text(value.type_ref);
            }
        }
        return {};
    };

    assert(has_type("ns.First"));
    assert(has_type("ns.Second"));
    assert(has_type("ns.Plain"));
    assert(value_type("ns.First.Same") == "ns.First");
    assert(value_type("ns.Second.Same") == "ns.Second");
    assert(value_type("ns.First.FirstOnly") == "ns.First");
    assert(value_type("ns.Second.SecondOnly") == "ns.Second");
    assert(value_type("ns.PlainValue") == "ns.Plain");
}

void test_native_scan_preserves_namespace_value_owners() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(scan,
                         "|-NamespaceDecl 0x1 <test.hpp:1:1, line:3:1> line:1:11 first\n"
                         "| `-VarDecl 0x2 <line:2:1, col:18> col:12 value 'const int'\n"
                         "`-NamespaceDecl 0x3 <line:4:1, line:6:1> line:4:11 second\n"
                         "  `-VarDecl 0x4 <line:5:1, col:18> col:12 value 'const int'\n",
                         {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto has_value = [&](const std::string& name) {
        return std::ranges::any_of(
            scan.values, [&](const dudu::NativeValueDecl& value) { return value.name == name; });
    };
    assert(has_value("first.value"));
    assert(has_value("second.value"));
}

void test_native_scan_imports_using_shadow_function_overloads() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "`-NamespaceDecl 0x1 <cmath:1:1, line:5:1> line:1:11 std\n"
        "  |-UsingDecl 0x2 <line:2:1, col:9> col:9 ::sqrt\n"
        "  |-UsingShadowDecl 0x3 <col:9> col:9 implicit Function 0x10 'sqrt' 'double "
        "(double) noexcept(true)'\n"
        "  |-UsingShadowDecl 0x4 <col:9> col:9 implicit Function 0x11 'sqrt' 'float "
        "(float) noexcept'\n"
        "  `-UsingShadowDecl 0x5 <col:9> col:9 implicit FunctionTemplate 0x12 'sqrt'\n",
        {.file = dudu::SourceFileName("cmath"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto overload_count =
        std::ranges::count_if(scan.functions, [](const dudu::NativeFunctionDecl& function) {
            return function.name == "std.sqrt";
        });
    assert(overload_count == 2);
    assert(std::ranges::any_of(scan.functions, [](const dudu::NativeFunctionDecl& function) {
        return function.name == "std.sqrt" &&
               dudu::type_ref_text(function.return_type_ref) == "f64" &&
               function.param_type_refs.size() == 1 &&
               dudu::type_ref_text(function.param_type_refs.front()) == "f64";
    }));
    assert(std::ranges::any_of(scan.functions, [](const dudu::NativeFunctionDecl& function) {
        return function.name == "std.sqrt" &&
               dudu::type_ref_text(function.return_type_ref) == "f32" &&
               function.param_type_refs.size() == 1 &&
               dudu::type_ref_text(function.param_type_refs.front()) == "f32";
    }));
}

void test_native_scan_preserves_callable_and_static_methods() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "`-CXXRecordDecl 0x1 <test.hpp:1:1, line:5:1> line:1:8 struct Callable definition\n"
        "  |-CXXMethodDecl 0x2 <line:2:5, col:40> col:9 operator() 'int (int) const'\n"
        "  | `-ParmVarDecl 0x3 <col:20, col:24> col:24 value 'int'\n"
        "  |-CXXMethodDecl 0x4 <line:3:5, col:29> col:16 now 'int ()' static\n"
        "  `-CXXMethodDecl 0x5 <line:4:5, col:31> col:14 clone 'Callable () const'\n",
        {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto klass = std::ranges::find_if(
        scan.classes, [](const dudu::ClassDecl& item) { return item.name == "Callable"; });
    assert(klass != scan.classes.end());
    const auto call = std::ranges::find_if(klass->methods, [](const dudu::FunctionDecl& method) {
        return method.name == "operator()";
    });
    assert(call != klass->methods.end());
    assert(dudu::type_ref_text(call->receiver_type_ref) == "const[Self]");
    assert(call->params.size() == 1);
    assert(dudu::type_ref_text(call->params.front().type_ref) == "i32");

    const auto now = std::ranges::find_if(
        klass->methods, [](const dudu::FunctionDecl& method) { return method.name == "now"; });
    assert(now != klass->methods.end());
    assert(!dudu::has_type_ref(now->receiver_type_ref));

    const auto clone = std::ranges::find_if(
        klass->methods, [](const dudu::FunctionDecl& method) { return method.name == "clone"; });
    assert(clone != klass->methods.end());
    assert(dudu::type_ref_text(clone->return_type_ref) == "Callable");
}

void test_native_scan_merges_reopened_namespaces() {
    dudu::NativeHeaderScan scan;
    for (const std::string source : {"first.hpp", "second.hpp"}) {
        dudu::NativeNamespaceDecl space;
        space.name = "shared";
        space.identity.usr = source + "::shared";
        space.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 11};
        scan.namespaces.push_back(std::move(space));
    }
    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.namespaces, [](const dudu::NativeNamespaceDecl& space) {
               return space.name == "shared";
           }) == 1);
}

void test_native_scan_keeps_internal_generic_class_aliases_scoped() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "`-NamespaceDecl 0x1 <test.hpp:1:1, line:9:1> line:1:11 internal\n"
        "  `-ClassTemplateDecl 0x2 <line:2:1, line:8:3> line:3:10 __traits\n"
        "    |-TemplateTypeParmDecl 0x3 <line:2:10, col:19> col:19 referenced typename depth 0 "
        "index 0 T\n"
        "    |-TemplateTypeParmDecl 0x4 <col:27, col:48> col:36 typename depth 0 index 1\n"
        "    | `-TemplateArgument type 'typename T::value_type'\n"
        "    `-CXXRecordDecl 0x5 <line:3:3, line:8:3> line:3:10 struct __traits definition\n"
        "      |-TypedefDecl 0x6 <line:4:5, col:40> col:40 value_type 'typename T::value_type'\n"
        "      |-TypedefDecl 0x7 <line:5:5, col:26> col:26 reference 'value_type &'\n"
        "      `-TypedefDecl 0x8 <line:6:5, col:38> col:38 const_reference 'const value_type &'\n",
        {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto klass = std::ranges::find_if(scan.classes, [](const dudu::ClassDecl& item) {
        return item.name == "internal.__traits" && item.native_specialization_args.empty();
    });
    assert(klass != scan.classes.end());
    assert(klass->generic_params.size() == 2);
    assert(std::ranges::any_of(klass->type_aliases, [](const dudu::TypeAliasDecl& alias) {
        return alias.name == "reference" && alias.type_ref.kind == dudu::TypeKind::Reference &&
               alias.type_ref.children.size() == 1 &&
               dudu::type_ref_text(alias.type_ref.children.front()) ==
                   "internal.__traits.value_type";
    }));
    assert(std::ranges::none_of(scan.types, [](const dudu::NativeTypeDecl& type) {
        return type.name == "internal.reference";
    }));
}

void test_completed_native_scan_qualifies_forward_visible_types() {
    const dudu::SourceLocation location{
        .file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1};
    dudu::NativeHeaderScan scan;
    scan.types.push_back({
        .name = "std.LateAlias",
        .native_spelling = "i32",
        .type_ref = dudu::named_type_ref("i32", location),
        .location = location,
    });
    scan.functions.push_back({
        .name = "std.make_late",
        .template_params = {"T"},
        .template_param_is_value = {false},
        .param_names = {"value"},
        .param_native_spellings = {"T"},
        .param_type_refs = {dudu::named_type_ref("T", location)},
        .return_native_spelling = "LateAlias",
        .return_type_ref = dudu::named_type_ref("LateAlias", location),
        .location = location,
    });

    dudu::qualify_completed_native_scan(scan);

    assert(dudu::type_ref_text(scan.functions.front().return_type_ref) == "std.LateAlias");
    assert(dudu::type_ref_text(scan.functions.front().param_type_refs.front()) == "T");
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_scan_retries_with_c_prelude_for_context_headers(root);
        test_aliased_c_import_prefixes_visible_transitive_functions(root);
        test_native_scan_ignores_anonymous_record_definitions();
        test_native_scan_preserves_scoped_enum_owners();
        test_native_scan_preserves_namespace_value_owners();
        test_native_scan_imports_using_shadow_function_overloads();
        test_native_scan_preserves_callable_and_static_methods();
        test_native_scan_merges_reopened_namespaces();
        test_native_scan_keeps_internal_generic_class_aliases_scoped();
        test_completed_native_scan_qualifies_forward_visible_types();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
