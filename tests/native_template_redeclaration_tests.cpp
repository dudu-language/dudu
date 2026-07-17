#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_method_templates.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void test_native_class_partial_specialization_metadata(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build/native-partial-specialization-cache";
    std::filesystem::remove_all(config.build_dir);

    const auto scan_and_check = [&]() {
        dudu::ModuleAst module =
            dudu::parse_source("from cpp.path import native_headers/simple_cpp.hpp\n",
                               source_dir / "native_partial_specialization.dd");
        dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

        const std::string class_name = "dudu_native.AssociatedResult";
        size_t primary_count = 0;
        size_t specialization_count = 0;
        for (const dudu::ClassDecl& klass : module.native_classes) {
            if (klass.name != class_name) {
                continue;
            }
            if (klass.native_specialization_args.empty()) {
                ++primary_count;
                assert((klass.generic_params == std::vector<std::string>{"T", "Enabled"}));
                continue;
            }
            ++specialization_count;
            assert(klass.native_partial_specialization);
            assert((klass.generic_params == std::vector<std::string>{"T"}));
            assert(klass.native_specialization_args.size() == 2);
            assert(dudu::type_ref_text(klass.native_specialization_args[0]) == "T");
            const std::string enabled = dudu::type_ref_text(klass.native_specialization_args[1]);
            if (enabled != "1" && enabled != "true") {
                throw std::runtime_error("unexpected boolean specialization value: " + enabled);
            }
            assert(klass.type_aliases.size() == 1);
            assert(klass.type_aliases.front().name == "type");
            assert(dudu::type_ref_text(klass.type_aliases.front().type_ref) == "T");
        }
        assert(primary_count == 1);
        assert(specialization_count == 1);

        const dudu::Symbols symbols = dudu::collect_symbols(module);
        const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.AssociatedResult[i32, true].type"));
        assert(dudu::type_ref_text(resolved) == "i32");
        const dudu::TypeRef pointer_pattern = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.PatternResult[*i32].type"));
        assert(dudu::type_ref_text(pointer_pattern) == "i32");
        const dudu::TypeRef exact_specialization = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.PatternResult[i32].type"));
        assert(dudu::type_ref_text(exact_specialization) == "f32");
        const dudu::TypeRef ambiguous = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.AmbiguousResult[Condition, i32].type"));
        assert(dudu::type_ref_text(ambiguous) ==
               "dudu_native.AmbiguousResult[Condition, i32].type");
    };

    scan_and_check();
    scan_and_check();
}

void test_native_class_redeclarations_merge_specialization_metadata() {
    dudu::ClassDecl declaration;
    declaration.name = "native.Result";
    declaration.identity.usr = "c:@N@native@Result";
    declaration.generic_params = {"T"};
    declaration.native_specialization_args = {dudu::parse_type_text("*T")};
    declaration.native_partial_specialization = true;

    dudu::ClassDecl definition = declaration;
    dudu::TypeAliasDecl alias;
    alias.name = "type";
    alias.type_ref = dudu::parse_type_text("T");
    definition.type_aliases.push_back(std::move(alias));
    dudu::FieldDecl field;
    field.name = "value";
    field.type_ref = dudu::parse_type_text("T");
    definition.fields.push_back(std::move(field));

    std::vector<dudu::ClassDecl> merged = {declaration};
    dudu::append_unique_native_classes(merged, {definition});
    assert(merged.size() == 1);
    assert(merged.front().native_partial_specialization);
    assert(merged.front().type_aliases.size() == 1);
    assert(merged.front().fields.size() == 1);
}

void test_named_template_definition_replaces_forward_placeholders() {
    dudu::ClassDecl forward;
    forward.name = "sample.Pair";
    forward.generic_params = {"Value", "__dudu_native_type_parameter_1"};
    forward.generic_default_args.resize(2);
    forward.generic_default_args[1] = dudu::parse_type_text("Value");

    dudu::ClassDecl definition;
    definition.name = forward.name;
    definition.generic_params = {"Value", "Entity"};
    dudu::TypeAliasDecl alias;
    alias.name = "value_type";
    alias.type_ref = dudu::parse_type_text("Value");
    definition.type_aliases.push_back(std::move(alias));

    dudu::merge_native_class_declaration(forward, definition);
    assert((forward.generic_params == std::vector<std::string>{"Value", "Entity"}));
    assert(dudu::type_ref_text(forward.generic_default_args[1]) == "Value");
    assert(forward.type_aliases.size() == 1);
    assert(dudu::type_ref_text(forward.type_aliases.front().type_ref) == "Value");
}

void test_template_definition_adds_default_after_forward_declaration() {
    dudu::ClassDecl forward;
    forward.name = "sample.View";
    forward.generic_params = {"T", "Extent"};
    forward.generic_min_args = 2;
    forward.generic_default_args.resize(2);

    dudu::ClassDecl definition;
    definition.name = forward.name;
    definition.generic_params = {"Value", "Size"};
    definition.generic_min_args = 1;
    definition.generic_default_args = {{}, dudu::parse_type_text("dynamic_extent")};
    dudu::merge_native_class_declaration(forward, definition);
    assert((forward.generic_params == std::vector<std::string>{"T", "Extent"}));
    assert(forward.generic_min_args == 1);
    assert(forward.generic_default_args.size() == 2);
    assert(dudu::type_ref_text(forward.generic_default_args[1]) == "dynamic_extent");

    dudu::NativeTypeDecl forward_type;
    forward_type.name = "sample.View";
    forward_type.generic_params = {"T", "Extent"};
    forward_type.generic_min_args = 2;
    forward_type.generic_default_args.resize(2);
    dudu::NativeTypeDecl definition_type;
    definition_type.name = forward_type.name;
    definition_type.generic_params = {"Value", "Size"};
    definition_type.generic_min_args = 1;
    definition_type.generic_default_args = {{}, dudu::parse_type_text("dynamic_extent")};
    std::vector<dudu::NativeTypeDecl> types = {forward_type};
    dudu::append_unique_native_types(types, {definition_type});
    assert(types.size() == 1);
    assert((types.front().generic_params == std::vector<std::string>{"T", "Extent"}));
    assert(types.front().generic_min_args == 1);
    assert(dudu::type_ref_text(types.front().generic_default_args[1]) == "dynamic_extent");
}

void test_native_template_redeclarations_alpha_rename_definition_metadata() {
    dudu::ClassDecl forward;
    forward.name = "sample.IteratorTraits";
    forward.generic_params = {"__dudu_native_type_parameter_0"};

    dudu::ClassDecl named_redeclaration;
    named_redeclaration.name = forward.name;
    named_redeclaration.generic_params = {"Iterator"};
    dudu::merge_native_class_declaration(forward, named_redeclaration);
    assert((forward.generic_params == std::vector<std::string>{"Iterator"}));

    dudu::ClassDecl definition;
    definition.name = forward.name;
    definition.generic_params = {"Input"};
    dudu::BaseClassDecl base;
    base.type_ref = dudu::parse_type_text("sample.BaseTraits[Input]");
    definition.base_class_refs.push_back(std::move(base));
    dudu::TypeAliasDecl alias;
    alias.name = "reference";
    alias.type_ref = dudu::parse_type_text("Input.reference");
    definition.type_aliases.push_back(std::move(alias));
    dudu::FunctionDecl method;
    method.name = "read";
    method.return_type_ref = dudu::parse_type_text("Input.reference");
    dudu::ParamDecl param;
    param.name = "input";
    param.type_ref = dudu::parse_type_text("&Input");
    method.params.push_back(std::move(param));
    definition.methods.push_back(std::move(method));

    dudu::merge_native_class_declaration(forward, definition);
    assert((forward.generic_params == std::vector<std::string>{"Iterator"}));
    assert(forward.base_class_refs.size() == 1);
    assert(dudu::type_ref_text(forward.base_class_refs.front().type_ref) ==
           "sample.BaseTraits[Iterator]");
    assert(forward.type_aliases.size() == 1);
    assert(dudu::type_ref_text(forward.type_aliases.front().type_ref) == "Iterator.reference");
    assert(forward.methods.size() == 1);
    assert(dudu::type_ref_text(forward.methods.front().return_type_ref) == "Iterator.reference");
    assert(dudu::type_ref_text(forward.methods.front().params.front().type_ref) == "&Iterator");
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_class_partial_specialization_metadata(root);
        test_native_class_redeclarations_merge_specialization_metadata();
        test_named_template_definition_replaces_forward_placeholders();
        test_template_definition_adds_default_after_forward_declaration();
        test_native_template_redeclarations_alpha_rename_definition_metadata();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
