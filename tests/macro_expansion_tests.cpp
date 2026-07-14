#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    assert(output);
    output << source;
}

const dudu::ClassDecl& player_class(const dudu::ModuleAst& module) {
    for (const dudu::ModuleAst& unit : module.module_units) {
        for (const dudu::ClassDecl& klass : unit.classes) {
            if (klass.name == "Player")
                return klass;
        }
    }
    assert(false && "Player class is missing");
    return module.classes.front();
}

bool has_player_method(const dudu::ModuleAst& module, const std::string& name) {
    const dudu::ClassDecl& player = player_class(module);
    return std::any_of(player.methods.begin(), player.methods.end(),
                       [&](const auto& method) { return method.name == name; });
}

const dudu::EnumDecl& color_enum(const dudu::ModuleAst& module) {
    for (const dudu::ModuleAst& unit : module.module_units) {
        for (const dudu::EnumDecl& en : unit.enums) {
            if (en.name == "Color")
                return en;
        }
    }
    assert(false && "Color enum is missing");
    return module.enums.front();
}

void test_dudu_macro_expands_before_semantics_and_caches() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_expansion_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_expansion_fixture\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(dir / "src/macros.dd",
               "import dudu.ast as ast\n"
               "\n"
               "@macro\n"
               "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
               "    return_stmt = ast.Statement(\n"
               "        kind=ast.StatementKind.Return,\n"
               "        value=ast.Expression(kind=ast.ExpressionKind.IntLiteral, value=\"7\"),\n"
               "    )\n"
               "    method = ast.FunctionDecl(\n"
               "        name=\"debug_score\",\n"
               "        return_type=ast.named_type(\"i32\"),\n"
               "        body=[return_stmt],\n"
               "    )\n"
               "    declaration = ast.Declaration(\n"
               "        kind=ast.DeclarationKind.Function,\n"
               "        function_decl=method,\n"
               "    )\n"
               "    generated = ast.GeneratedDeclaration(declaration=declaration)\n"
               "    return ast.Expansion(members=[generated])\n");
    write_file(dir / "src/main.dd", "from macros import Debug\n"
                                    "\n"
                                    "@derive(Debug)\n"
                                    "class Player:\n"
                                    "    hp: i32\n"
                                    "\n"
                                    "def score(player: Player) -> i32:\n"
                                    "    return player.debug_score()\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    const dudu::ProjectIndex first = dudu::ProjectIndex::load(options);
    assert(first.macro_report().invocations == 1);
    const dudu::ClassDecl& player = player_class(first.merged_module());
    assert(player.methods.size() == 1);
    assert(player.methods.front().name == "debug_score");
    const std::vector<dudu::CppModuleArtifact> artifacts =
        dudu::emit_cpp_module_artifacts(first.merged_module());
    assert(artifacts.size() == 3);
    assert(std::none_of(artifacts.begin(), artifacts.end(), [](const auto& artifact) {
        return artifact.module_path == "macros" || artifact.module_path == "dudu.ast";
    }));
    assert(std::any_of(artifacts.begin(), artifacts.end(), [](const auto& artifact) {
        return artifact.content.find("debug_score") != std::string::npos;
    }));

    const dudu::ProjectIndex second = dudu::ProjectIndex::load(options);
    assert(second.macro_report().invocations == 1);
    assert(second.macro_report().worker_cache_hits == 1);
    assert(second.macro_report().expansion_cache_hits == 1);
    assert(player_class(second.merged_module()).methods.size() == 1);
}

void test_capability_input_invalidates_expansion_cache() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_capability_expansion_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_capability_fixture\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n"
                                  "\n"
                                  "[macro.capabilities]\n"
                                  "fs.read = [\"generated_name.txt\"]\n");
    write_file(dir / "generated_name.txt", "first_score");
    write_file(dir / "src/macros.dd",
               "import dudu.ast as ast\n"
               "import dudu.macro as host\n"
               "\n"
               "@macro\n"
               "def FileNamed(item: ast.ClassDecl) -> ast.Expansion:\n"
               "    return_stmt = ast.Statement(\n"
               "        kind=ast.StatementKind.Return,\n"
               "        value=ast.Expression(kind=ast.ExpressionKind.IntLiteral, value=\"1\"),\n"
               "    )\n"
               "    method = ast.FunctionDecl(\n"
               "        name=host.read_text(\"generated_name.txt\"),\n"
               "        return_type=ast.named_type(\"i32\"),\n"
               "        body=[return_stmt],\n"
               "    )\n"
               "    declaration = ast.Declaration(\n"
               "        kind=ast.DeclarationKind.Function, function_decl=method,\n"
               "    )\n"
               "    return ast.Expansion(\n"
               "        members=[ast.GeneratedDeclaration(declaration=declaration)],\n"
               "    )\n");
    write_file(dir / "src/main.dd", "from macros import FileNamed\n"
                                    "\n"
                                    "@derive(FileNamed)\n"
                                    "class Player:\n"
                                    "    hp: i32\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;

    const dudu::ProjectIndex first = dudu::ProjectIndex::load(options);
    assert(first.macro_report().expansion_cache_hits == 0);
    assert(has_player_method(first.merged_module(), "first_score"));

    const dudu::ProjectIndex second = dudu::ProjectIndex::load(options);
    assert(second.macro_report().expansion_cache_hits == 1);
    assert(has_player_method(second.merged_module(), "first_score"));

    write_file(dir / "generated_name.txt", "second_score");
    const dudu::ProjectIndex changed = dudu::ProjectIndex::load(options);
    assert(changed.macro_report().expansion_cache_hits == 0);
    assert(has_player_method(changed.merged_module(), "second_score"));
}

void test_undeclared_macro_capability_is_rejected() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_capability_denied_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_capability_denied\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(dir / "secret.txt", "forbidden_score");
    write_file(dir / "src/macros.dd", "import dudu.ast as ast\n"
                                      "import dudu.macro as host\n"
                                      "\n"
                                      "@macro\n"
                                      "def Denied(item: ast.ClassDecl) -> ast.Expansion:\n"
                                      "    host.read_text(\"secret.txt\")\n"
                                      "    return ast.expansion()\n");
    write_file(dir / "src/main.dd",
               "from macros import Denied\n\n@derive(Denied)\nclass Player:\n    hp: i32\n");
    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    bool rejected = false;
    try {
        (void)dudu::ProjectIndex::load(options);
    } catch (const std::runtime_error& error) {
        rejected =
            std::string(error.what()).find("undeclared capability 'fs.read'") != std::string::npos;
    }
    assert(rejected);
}

void test_nondeterministic_macro_requires_explicit_approval() {
    const auto write_fixture = [](const std::filesystem::path& dir, bool approved) {
        std::filesystem::remove_all(dir);
        write_file(
            dir / "dudu.toml",
            "name = \"macro_clock_fixture\"\n"
            "entry = \"src/main.dd\"\n"
            "build_dir = \"build\"\n\n" +
                std::string(approved ? "[macro]\nallow_non_cacheable = [\"Clocked\"]\n\n" : "") +
                "[macro.capabilities]\nclock = true\n");
        write_file(dir / "src/macros.dd", "import dudu.ast as ast\n"
                                          "import dudu.macro as host\n\n"
                                          "@macro\n"
                                          "def Clocked(item: ast.ClassDecl) -> ast.Expansion:\n"
                                          "    host.clock_ns()\n"
                                          "    return ast.expansion()\n");
        write_file(dir / "src/main.dd",
                   "from macros import Clocked\n\n@derive(Clocked)\nclass Player:\n    hp: i32\n");
    };
    const auto load = [](const std::filesystem::path& dir) {
        const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
        dudu::ProjectIndexOptions options;
        options.entry_path = dir / "src/main.dd";
        options.config = config;
        options.source_dir = dir / "src";
        options.force_module_tree = true;
        return dudu::ProjectIndex::load(options);
    };

    const std::filesystem::path denied =
        std::filesystem::temp_directory_path() / "dudu_macro_clock_denied_test";
    write_fixture(denied, false);
    bool rejected = false;
    try {
        (void)load(denied);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("allow_non_cacheable") != std::string::npos;
    }
    assert(rejected);

    const std::filesystem::path allowed =
        std::filesystem::temp_directory_path() / "dudu_macro_clock_allowed_test";
    write_fixture(allowed, true);
    const dudu::ProjectIndex first = load(allowed);
    const dudu::ProjectIndex second = load(allowed);
    assert(first.macro_report().expansion_cache_hits == 0);
    assert(second.macro_report().expansion_cache_hits == 0);
}

void test_enum_derive_generates_a_callable_method() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_enum_derive_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_enum_derive_fixture\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(
        dir / "src/macros.dd",
        "import dudu.ast as ast\n"
        "\n"
        "@macro\n"
        "def StringEnum(item: ast.EnumDecl) -> ast.Expansion:\n"
        "    self_type = ast.TypeRef(\n"
        "        kind=ast.TypeKind.Reference,\n"
        "        children=[ast.named_type(\"Self\")],\n"
        "    )\n"
        "    self_param = ast.Parameter(name=\"self\", type=self_type)\n"
        "    value = ast.Expression(\n"
        "        kind=ast.ExpressionKind.StringLiteral, value=item.name,\n"
        "    )\n"
        "    body = ast.Statement(kind=ast.StatementKind.Return, value=value)\n"
        "    method = ast.FunctionDecl(\n"
        "        name=\"enum_name\",\n"
        "        parameters=[self_param],\n"
        "        return_type=ast.named_type(\"str\"),\n"
        "        body=[body],\n"
        "    )\n"
        "    return ast.Expansion(\n"
        "        members=[ast.generated(ast.function_declaration(method), ast.SourceOrigin())],\n"
        "    )\n");
    write_file(dir / "src/main.dd", "from macros import StringEnum\n"
                                    "\n"
                                    "@derive(StringEnum)\n"
                                    "enum Color:\n"
                                    "    Red\n"
                                    "    Green\n"
                                    "\n"
                                    "def color_name(color: Color) -> str:\n"
                                    "    return color.enum_name()\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.macro_report().invocations == 1);
    const dudu::EnumDecl& color = color_enum(index.merged_module());
    assert(color.methods.size() == 1);
    assert(color.methods.front().name == "enum_name");
    const std::vector<dudu::CppModuleArtifact> artifacts =
        dudu::emit_cpp_module_artifacts(index.merged_module());
    assert(std::any_of(artifacts.begin(), artifacts.end(), [](const auto& artifact) {
        return artifact.content.find("dudu_main_Color_enum_name") != std::string::npos &&
               artifact.content.find("dudu_main_Color_enum_name(color)") != std::string::npos;
    }));
}

} // namespace

int main() {
    test_dudu_macro_expands_before_semantics_and_caches();
    test_capability_input_invalidates_expansion_cache();
    test_undeclared_macro_capability_is_rejected();
    test_nondeterministic_macro_requires_explicit_approval();
    test_enum_derive_generates_a_callable_method();
    return 0;
}
