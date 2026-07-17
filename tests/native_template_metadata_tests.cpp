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
#include "dudu/sema/sema_methods.hpp"
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

void test_native_method_templates_do_not_mask_concrete_overloads(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-method-template-overload";
    const std::filesystem::path header = source_dir / "method_template_overload.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "#include <string>\n"
               "struct Holder {\n"
               "    template <typename String>\n"
               "    String text() const { return String{}; }\n"
               "    const std::string& text() const { static std::string value = \"ok\"; return "
               "value; }\n"
               "};\n";
    }

    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import ./method_template_overload.hpp\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    holder = Holder()\n"
                           "    text: str = holder.text()\n"
                           "    if len(text) == 2:\n"
                           "        return 42\n"
                           "    return 1\n",
                           source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    bool saw_template = false;
    bool saw_concrete = false;
    for (const dudu::ClassDecl& klass : module.native_classes) {
        if (klass.name != "Holder") {
            continue;
        }
        for (const dudu::FunctionDecl& method : klass.methods) {
            if (method.name != "text") {
                continue;
            }
            if (!method.generic_params.empty()) {
                saw_template = method.generic_params.front() == "String";
            }
            if (method.generic_params.empty() &&
                dudu::type_assignment_allowed(dudu::parse_type_text("str"),
                                              dudu::function_return_type_ref(method))) {
                saw_concrete = true;
            }
        }
    }
    assert(saw_template);
    assert(saw_concrete);

    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::string text = holder.text();") != std::string::npos);
}

void test_native_class_templates_preserve_declared_metadata(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    const auto require_class = [](const dudu::ModuleAst& scanned,
                                  std::string_view name) -> const dudu::ClassDecl& {
        for (const dudu::ClassDecl& klass : scanned.native_classes) {
            if (klass.name == name) {
                return klass;
            }
        }
        throw std::runtime_error(std::string(name) + " was not scanned");
    };
    const auto require_type = [](const dudu::ModuleAst& scanned,
                                 std::string_view name) -> const dudu::NativeTypeDecl& {
        for (const dudu::NativeTypeDecl& type : scanned.native_types) {
            if (type.name == name) {
                return type;
            }
        }
        throw std::runtime_error(std::string(name) + " was not scanned");
    };
    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import native_dependent_alias_metadata.hpp\n",
                           source_dir / "native_dependent_alias_metadata_scan.dd");
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    const dudu::ClassDecl& envelope = require_class(module, "depmeta.Envelope");
    assert((envelope.generic_params ==
            std::vector<std::string>{"LeftPayload", "RightPayload", "Extent"}));

    bool saw_selected = false;
    bool saw_wrapped = false;
    for (const dudu::TypeAliasDecl& alias : envelope.type_aliases) {
        if (alias.name == "selected_result") {
            saw_selected = dudu::type_ref_text(alias.type_ref) == "RightPayload";
        }
        if (alias.name == "wrapped_result") {
            saw_wrapped = dudu::type_ref_text(alias.type_ref) == "depmeta.Wrapper[RightPayload]";
        }
    }
    assert(saw_selected);
    assert(saw_wrapped);

    const dudu::TypeRef selected = dudu::substitute_receiver_template_type(
        dudu::parse_type_text("selected_result"), envelope,
        {dudu::parse_type_text("i32"), dudu::parse_type_text("f32"), dudu::parse_type_text("4")});
    assert(dudu::type_ref_text(selected) == "f32");
    const dudu::ClassDecl& defaulted = require_class(module, "depmeta.DefaultedEnvelope");
    assert(defaulted.generic_params.size() == 2);
    assert(defaulted.generic_min_args == 1);
    assert(defaulted.generic_default_args.size() == 2);
    assert(!dudu::has_type_ref(defaulted.generic_default_args[0]));
    assert(dudu::type_ref_text(defaulted.generic_default_args[1]) == "4");
    const dudu::ClassDecl& trait_default = require_class(module, "depmeta.TraitDefaultBase");
    assert(trait_default.generic_default_args.size() == 3);
    assert(trait_default.generic_default_args[2].kind == dudu::TypeKind::Associated);
    assert(trait_default.generic_default_args[2].name == "value");
    assert(trait_default.generic_default_args[2].children.size() == 1);
    assert(trait_default.generic_default_args[2].children.front().kind ==
           dudu::TypeKind::Associated);
    const dudu::NativeTypeDecl& carrier = require_type(module, "depmeta.AliasCarrier");
    assert((carrier.generic_params == std::vector<std::string>{"Selected", "Left"}));
    assert(carrier.generic_min_args == 2);
    const dudu::NativeTypeDecl& defaulted_alias = require_type(module, "depmeta.DefaultedAlias");
    assert((defaulted_alias.generic_params == std::vector<std::string>{"Payload", "Holder"}));
    assert(defaulted_alias.generic_min_args == 1);
    assert(defaulted_alias.generic_default_args.size() == 2);
    assert(dudu::type_ref_text(defaulted_alias.generic_default_args[1]) ==
           "depmeta.Wrapper[Payload]");
    const dudu::Symbols symbols = dudu::collect_symbols(module);
    const dudu::TypeRef nested_result = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("depmeta.NestedAliasOwner[i32, f32].result"));
    assert(dudu::type_ref_text(nested_result) == "depmeta.Wrapper[i32]");
    const dudu::TypeRef specialized_local_alias = dudu::resolve_associated_type_ref(
        symbols,
        dudu::parse_type_text("depmeta.AliasByShape[depmeta.Wrapper[f32], void].result"));
    assert(dudu::type_ref_text(specialized_local_alias) == "depmeta.Envelope[i32, f32, 1]");
    const dudu::TypeRef trait_default_alias = dudu::resolve_associated_type_ref(
        symbols,
        dudu::parse_type_text(
            "depmeta.TraitDefaultOwner[f32, depmeta.MapTraits[true]].mapped_type"));
    assert(dudu::type_ref_text(trait_default_alias) == "f32");
    const std::vector<dudu::FunctionSignature> specialized_methods =
        dudu::method_signatures_for_type(
            symbols,
            dudu::parse_type_text("depmeta.AliasByShape[depmeta.Wrapper[f32], void]"),
            "take", {});
    assert(specialized_methods.size() == 1);
    assert(specialized_methods.front().param_type_refs.size() == 1);
    assert(dudu::type_ref_text(specialized_methods.front().param_type_refs.front()) ==
           "depmeta.Envelope[i32, f32, 1]");
    dudu::TypeRef deeply_nested =
        dudu::parse_type_text("depmeta.NestedAliasOwner[i32, f32].result");
    for (size_t i = 0; i < 32; ++i) {
        dudu::TypeRef wrapper;
        wrapper.kind = dudu::TypeKind::Template;
        wrapper.name = "depmeta.Wrapper";
        wrapper.children.push_back(std::move(deeply_nested));
        deeply_nested = std::move(wrapper);
    }
    const dudu::TypeRef resolved_nested =
        dudu::resolve_associated_type_ref(symbols, std::move(deeply_nested));
    const dudu::TypeRef* nested_leaf = &resolved_nested;
    for (size_t i = 0; i < 33; ++i) {
        assert(nested_leaf->kind == dudu::TypeKind::Template);
        assert(nested_leaf->name == "depmeta.Wrapper");
        assert(nested_leaf->children.size() == 1);
        nested_leaf = &nested_leaf->children.front();
    }
    assert(dudu::type_ref_text(*nested_leaf) == "i32");

    dudu::ModuleAst cached =
        dudu::parse_source("from cpp.path import native_dependent_alias_metadata.hpp\n",
                           source_dir / "native_dependent_alias_metadata_cached.dd");
    dudu::merge_native_headers(cached, {.config = config, .source_dir = source_dir});
    const dudu::ClassDecl& cached_envelope = require_class(cached, "depmeta.Envelope");
    assert(cached_envelope.generic_params == envelope.generic_params);
    assert(cached_envelope.type_aliases.size() == envelope.type_aliases.size());
    const dudu::ClassDecl& cached_defaulted = require_class(cached, "depmeta.DefaultedEnvelope");
    assert(cached_defaulted.generic_min_args == defaulted.generic_min_args);
    assert(cached_defaulted.generic_default_args.size() == defaulted.generic_default_args.size());
    assert(dudu::type_ref_text(cached_defaulted.generic_default_args[1]) == "4");
    const dudu::NativeTypeDecl& cached_carrier = require_type(cached, "depmeta.AliasCarrier");
    assert(cached_carrier.generic_params == carrier.generic_params);
    const dudu::NativeTypeDecl& cached_defaulted_alias =
        require_type(cached, "depmeta.DefaultedAlias");
    assert(cached_defaulted_alias.generic_default_args.size() ==
           defaulted_alias.generic_default_args.size());
    assert(dudu::type_ref_text(cached_defaulted_alias.generic_default_args[1]) ==
           "depmeta.Wrapper[Payload]");
}

void test_internal_native_template_aliases_resolve_public_returns(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build";
    const dudu::ModuleAst import_only =
        dudu::parse_source("from cpp.path import native_internal_alias_factory.hpp\n",
                           source_dir / "native_internal_alias_factory_import.dd");
    const dudu::NativeHeaderScan scan =
        dudu::scan_native_headers(import_only, {.config = config, .source_dir = source_dir});
    const auto select_pack = std::ranges::find_if(scan.classes, [](const dudu::ClassDecl& klass) {
        return klass.name == "internal_alias.SelectPack" &&
               !klass.native_specialization_args.empty();
    });
    assert(select_pack != scan.classes.end());
    assert(select_pack->native_specialization_args.size() == 3);
    assert(dudu::type_ref_text(select_pack->native_specialization_args[0]) == "0");
    assert(dudu::type_ref_text(select_pack->native_specialization_args[1]) == "T0");
    assert(dudu::type_ref_text(select_pack->native_specialization_args[2]) == "Rest...");
    assert(select_pack->type_aliases.size() == 1);
    assert(dudu::type_ref_text(select_pack->type_aliases[0].type_ref) == "T0");

    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import native_internal_alias_factory.hpp\n"
                           "\n"
                           "class Node:\n"
                           "    value: i32\n"
                           "\n"
                           "def probe():\n"
                           "    holder: internal_alias.Holder[Node] = "
                           "internal_alias.make_holder[Node]()\n"
                           "    pack_result: i32 = "
                           "internal_alias.make_pack_element[1, i32, f32]()\n"
                           "    selected_pack: i32 = "
                           "internal_alias.select_pack[0, i32, f32]()\n"
                           "    pointer_result: i32 = internal_alias.select_result[*i32]()\n"
                           "    value_result: f32 = internal_alias.select_result[i32]()\n",
                           source_dir / "native_internal_alias_factory_scan.dd");
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    const auto internal =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.__factory_result";
        });
    assert(internal != module.native_classes.end());
    assert(std::ranges::any_of(internal->type_aliases, [](const dudu::TypeAliasDecl& alias) {
        return alias.name == "type" &&
               dudu::type_ref_text(alias.type_ref) == "internal_alias.Holder[T]";
    }));
    assert(std::ranges::none_of(module.native_types, [](const dudu::NativeTypeDecl& type) {
        return type.name == "internal_alias.__factory_result";
    }));
    const auto defaulted =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.__defaulted_result";
        });
    assert(defaulted != module.native_classes.end());
    assert(defaulted->generic_params.size() == 3);
    assert(defaulted->generic_min_args == 2);
    assert(defaulted->generic_default_args.size() == 3);
    assert(dudu::type_ref_text(defaulted->generic_default_args[2]) == "void");
    const auto scoped =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.__scope.__rebind";
        });
    assert(scoped != module.native_classes.end());
    assert(std::ranges::none_of(module.native_classes, [](const dudu::ClassDecl& klass) {
        return klass.name == "internal_alias.__rebind";
    }));
    const auto detected_specialization =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.DetectedValue" &&
                   !klass.native_specialization_args.empty();
        });
    assert(detected_specialization != module.native_classes.end());
    assert(detected_specialization->native_specialization_requirements.empty());
    assert(detected_specialization->native_specialization_args.size() == 2);
    assert(dudu::type_ref_text(detected_specialization->native_specialization_args[1]) ==
           "internal_alias.Void[T.value_type]");

    const dudu::Symbols symbols = dudu::collect_symbols(module);
    const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.__factory_result[Node].type"));
    if (dudu::type_ref_text(resolved) != "internal_alias.Holder[Node]") {
        throw std::runtime_error("internal associated alias resolved as " +
                                 dudu::type_ref_text(resolved));
    }
    const dudu::TypeRef scoped_holder = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.ScopedHolder[i32, Node]"));
    if (dudu::type_ref_text(scoped_holder) != "internal_alias.Holder[Node]") {
        throw std::runtime_error("nested internal alias resolved as " +
                                 dudu::type_ref_text(scoped_holder));
    }
    const dudu::TypeRef detected = dudu::resolve_associated_type_ref(
        symbols,
        dudu::parse_type_text("internal_alias.DetectedValue[internal_alias.WithValue].type"));
    assert(dudu::type_ref_text(detected) == "i32");
    const dudu::TypeRef fallback = dudu::resolve_associated_type_ref(
        symbols,
        dudu::parse_type_text("internal_alias.DetectedValue[internal_alias.WithoutValue].type"));
    if (dudu::type_ref_text(fallback) != "f32") {
        throw std::runtime_error("fallback detection resolved as " +
                                 dudu::type_ref_text(fallback));
    }
    const auto signatures = symbols.native_function_signatures.find("internal_alias.make_holder");
    assert(signatures != symbols.native_function_signatures.end());
    assert(signatures->second.size() == 1);
    const dudu::FunctionSignature signature = dudu::substitute_explicit_template_signature(
        symbols, signatures->second.front(), {dudu::parse_type_text("Node")});
    const std::string return_type = dudu::type_ref_text(dudu::signature_return_type_ref(signature));
    if (return_type != "internal_alias.Holder[Node]") {
        throw std::runtime_error("internal factory return resolved as " + return_type);
    }
    const auto constant =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.Constant";
        });
    assert(constant != module.native_classes.end());
    const auto value =
        std::ranges::find_if(constant->static_fields,
                             [](const dudu::ConstDecl& field) { return field.name == "value"; });
    assert(value != constant->static_fields.end());
    assert(value->value_expr.kind == dudu::ExprKind::Name);
    assert(value->value_expr.name == "Value");
    const auto select =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.Select" &&
                   klass.native_specialization_args.empty();
        });
    assert(select != module.native_classes.end());
    const auto select_type =
        std::ranges::find_if(select->type_aliases,
                             [](const dudu::TypeAliasDecl& alias) { return alias.name == "type"; });
    assert(select_type != select->type_aliases.end());
    assert(select_type->generic_params.size() == 2);
    const std::string pointer_value = dudu::type_ref_text(dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.IsPointer[*i32].value")));
    const std::string plain_value = dudu::type_ref_text(dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.IsPointer[i32].value")));
    if (pointer_value != "true" || plain_value != "false") {
        throw std::runtime_error("native constexpr values resolved as " + pointer_value + " and " +
                                 plain_value);
    }
    const auto selected_signatures =
        symbols.native_function_signatures.find("internal_alias.select_result");
    assert(selected_signatures != symbols.native_function_signatures.end());
    const dudu::FunctionSignature pointer_signature = dudu::substitute_explicit_template_signature(
        symbols, selected_signatures->second.front(), {dudu::parse_type_text("*i32")});
    const dudu::FunctionSignature value_signature = dudu::substitute_explicit_template_signature(
        symbols, selected_signatures->second.front(), {dudu::parse_type_text("i32")});
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(pointer_signature)) == "i32");
    if (const std::string selected =
            dudu::type_ref_text(dudu::signature_return_type_ref(value_signature));
        selected != "f32") {
        throw std::runtime_error("false native constexpr selection resolved as " + selected);
    }

    dudu::analyze_module(module, {.check_bodies = true});
}

void test_equivalent_dependent_native_spellings_compare() {
    const dudu::TypeRef parsed =
        dudu::parse_type_text("std.allocator_traits._Size[_Alloc, i64].type");
    dudu::TypeRef alternate = parsed;
    alternate.kind = dudu::TypeKind::Qualified;
    alternate.name = dudu::type_ref_text(parsed);
    alternate.children.clear();
    assert(!dudu::type_ref_equivalent(parsed, alternate));
    assert(dudu::comparison_rhs_allowed({}, "<", parsed, {}, alternate));
}

void test_standard_string_size_type_resolves_numeric(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    dudu::ModuleAst module =
        dudu::parse_source("from cpp import string\n"
                           "\n"
                           "def prefix(value: std.string, count: usize) -> std.string:\n"
                           "    return std.string(value.substr(0, count))\n",
                           source_dir / "native_string_size_type_scan.dd");
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    const dudu::Symbols symbols = dudu::collect_symbols(module);
    if (!symbols.classes.contains("std.allocator_traits._Size")) {
        return;
    }
    const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("std.allocator_traits._Size[std.allocator[i32], i64].type"));
    if (!dudu::native_numeric_operator_operand(resolved)) {
        throw std::runtime_error("std string size type resolved as " +
                                 dudu::type_ref_text(resolved));
    }
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_native_fixed_array_typedef_alias(const std::filesystem::path& root) {
    assert(dudu::dudu_type("unsigned char[16]") == "array[u8][16]");
    assert(dudu::dudu_type("int[2][3]") == "array[i32][2, 3]");
    assert(dudu::dudu_type("enum NativeMode") == "NativeMode");
    assert(dudu::dudu_type("const enum NativeMode") == "const[NativeMode]");

    const std::filesystem::path source_dir = root / "build" / "native-fixed-array-typedef";
    const std::filesystem::path header = source_dir / "fixed_array_alias.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "typedef unsigned char DuduFixedBytes[16];\n"
               "inline int read_fixed(const DuduFixedBytes value) { return value[0]; }\n"
               "inline void copy_fixed(DuduFixedBytes dst, const DuduFixedBytes src) { "
               "dst[0] = src[0]; }\n";
    }

    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import ./fixed_array_alias.hpp as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    value: native.DuduFixedBytes\n"
                           "    copy: native.DuduFixedBytes\n"
                           "    native.copy_fixed(copy, value)\n"
                           "    if native.read_fixed(value) == 0:\n"
                           "        return 42\n"
                           "    return 1\n",
                           source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    bool saw_alias = false;
    for (const dudu::NativeTypeDecl& type : module.native_types) {
        if (type.name == "native.DuduFixedBytes") {
            assert(dudu::type_ref_text(type.type_ref) == "array[u8][16]");
            saw_alias = true;
        }
    }
    assert(saw_alias);

    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("DuduFixedBytes value{};") != std::string::npos);
    assert(cpp.find("copy_fixed(copy, value);") != std::string::npos);
    assert(cpp.find("read_fixed(value)") != std::string::npos);
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_method_templates_do_not_mask_concrete_overloads(root);
        test_native_class_templates_preserve_declared_metadata(root);
        test_internal_native_template_aliases_resolve_public_returns(root);
        test_equivalent_dependent_native_spellings_compare();
        test_standard_string_size_type_resolves_numeric(root);
        test_native_fixed_array_typedef_alias(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
