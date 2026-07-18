#include "dudu/core/decorators.hpp"
#include "dudu/macro/macro_registry.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    assert(file);
    file << text;
}

std::filesystem::path make_macro_project(std::string_view main_source) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_registry_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "macros.dd", "import dudu.ast as ast\n"
                                  "\n"
                                  "class JsonOptions:\n"
                                  "    name: Option[str] = None\n"
                                  "    skip: bool = False\n"
                                  "\n"
                                  "@macro(attributes=JsonOptions)\n"
                                  "def Json(item: ast.ClassDecl) -> ast.Expansion:\n"
                                  "    return ast.expansion()\n"
                                  "\n"
                                  "@macro\n"
                                  "def Debug(item: ast.Declaration) -> ast.Expansion:\n"
                                  "    return ast.expansion()\n");
    write_file(dir / "main.dd", std::string(main_source));
    return dir;
}

void test_declaration_and_helper_attributes_parse() {
    const std::string source = "@derive(Debug, Json)\n"
                               "class Player:\n"
                               "    @Json(name=\"identifier\")\n"
                               "    id: u64\n"
                               "\n"
                               "    @reflect\n"
                               "    def label(self) -> str:\n"
                               "        return \"player\"\n"
                               "\n"
                               "    @Json(skip=True)\n"
                               "    CACHE: i32 = 0\n"
                               "\n"
                               "@derive(StringEnum)\n"
                               "enum Token:\n"
                               "    @Json(name=\"end\")\n"
                               "    End\n"
                               "\n"
                               "    @Json(name=\"identifier\")\n"
                               "    Ident:\n"
                               "        @Json(name=\"value\")\n"
                               "        text: str\n"
                               "\n"
                               "@macro(attributes=JsonOptions)\n"
                               "def Json(item: ast.ClassDecl) -> ast.Expansion:\n"
                               "    return ast.expansion()\n";

    const dudu::ModuleAst module = dudu::parse_source(source, "macro_surface.dd");
    assert(module.classes.size() == 1);
    assert(dudu::decorator_call_matches(module.classes[0].decorators[0], "derive"));
    assert(module.classes[0].fields.size() == 1);
    assert(dudu::decorator_call_matches(module.classes[0].fields[0].decorators[0], "Json"));
    assert(module.classes[0].methods.size() == 1);
    assert(dudu::decorator_name(module.classes[0].methods[0].decorators[0]) == "reflect");
    assert(module.classes[0].constants.size() == 1);
    assert(dudu::decorator_call_matches(module.classes[0].constants[0].decorators[0], "Json"));
    assert(module.enums.size() == 1);
    assert(dudu::decorator_call_matches(module.enums[0].decorators[0], "derive"));
    assert(module.enums[0].values.size() == 2);
    assert(dudu::decorator_call_matches(module.enums[0].values[0].decorators[0], "Json"));
    assert(module.enums[0].values[1].payload_fields.size() == 1);
    assert(dudu::decorator_call_matches(module.enums[0].values[1].payload_fields[0].decorators[0],
                                        "Json"));
    assert(module.functions.size() == 1);
    assert(dudu::decorator_call_matches(module.functions[0].decorators[0], "macro"));
}

void test_public_sdk_resolves_from_standard_module_root() {
    const std::filesystem::path root = DUDU_REPO_ROOT;
    const dudu::ModuleAst module =
        dudu::load_source_tree(root / "tests" / "fixtures" / "macro_sdk_import.dd");
    bool found_sdk = false;
    for (const dudu::ModuleAst& unit : module.module_units) {
        if (unit.module_path == "dudu.ast") {
            found_sdk = true;
            assert(!unit.classes.empty());
            assert(!unit.enums.empty());
            break;
        }
    }
    assert(found_sdk);
}

void test_macro_registry_uses_ordinary_import_resolution() {
    const std::filesystem::path dir = make_macro_project("from macros import Json as Encode\n"
                                                         "from macros import Debug\n"
                                                         "\n"
                                                         "@derive(Encode, Debug)\n"
                                                         "class Player:\n"
                                                         "    @Encode(name=\"identifier\")\n"
                                                         "    id: u64\n");
    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    const dudu::macro::Plan plan = dudu::macro::build_plan(module);
    assert(plan.definitions.size() == 2);
    assert(plan.definitions.contains("macros.Json"));
    assert(plan.definitions.contains("macros.Debug"));
    assert(plan.invocations.size() == 2);
    assert(plan.invocations[0].macro->identity == "macros.Json");
    assert(plan.invocations[0].derive);
    assert(plan.invocations[0].helper_attributes.size() == 1);
    assert(plan.invocations[0].helper_attributes[0].target_name == "id");
    assert(plan.invocations[1].macro->identity == "macros.Debug");
}

void test_macro_registry_rejects_bad_helper_attribute() {
    const std::filesystem::path dir = make_macro_project("from macros import Json\n"
                                                         "\n"
                                                         "@derive(Json)\n"
                                                         "class Player:\n"
                                                         "    @Json(unknown=True)\n"
                                                         "    id: u64\n");
    bool failed = false;
    try {
        const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
        (void)dudu::macro::build_plan(module);
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("unknown Json attribute option: unknown") !=
                 std::string::npos;
    }
    assert(failed);
}

void test_container_helper_attribute_does_not_invoke_derive_twice() {
    for (const std::string& decorators : {
             std::string("@derive(Json)\n@Json(name=\"player\")\n"),
             std::string("@Json(name=\"player\")\n@derive(Json)\n"),
         }) {
        const std::filesystem::path dir = make_macro_project("from macros import Json\n"
                                                             "\n" +
                                                             decorators +
                                                             "class Player:\n"
                                                             "    id: u64\n");
        const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
        const dudu::macro::Plan plan = dudu::macro::build_plan(module);
        assert(plan.invocations.size() == 1);
        assert(plan.invocations.front().macro->identity == "macros.Json");
        assert(plan.invocations.front().derive);
    }
}

void test_macro_registry_validates_attached_macro_options() {
    const std::filesystem::path dir = make_macro_project("from macros import Json\n"
                                                         "\n"
                                                         "@Json(unknown=True)\n"
                                                         "class Player:\n"
                                                         "    id: u64\n");
    bool failed = false;
    try {
        const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
        (void)dudu::macro::build_plan(module);
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("unknown Json attribute option: unknown") !=
                 std::string::npos;
    }
    assert(failed);
}

void test_macro_registry_rejects_wrong_target_kind() {
    const std::filesystem::path dir = make_macro_project("from macros import Json\n"
                                                         "\n"
                                                         "@derive(Json)\n"
                                                         "def render() -> i32:\n"
                                                         "    return 0\n");
    bool failed = false;
    try {
        const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
        (void)dudu::macro::build_plan(module);
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("accepts class, not function") != std::string::npos;
    }
    assert(failed);
}

} // namespace

int main() {
    test_declaration_and_helper_attributes_parse();
    test_public_sdk_resolves_from_standard_module_root();
    test_macro_registry_uses_ordinary_import_resolution();
    test_macro_registry_rejects_bad_helper_attribute();
    test_container_helper_attribute_does_not_invoke_derive_twice();
    test_macro_registry_validates_attached_macro_options();
    test_macro_registry_rejects_wrong_target_kind();
    std::cout << "macro syntax tests passed\n";
    return 0;
}
