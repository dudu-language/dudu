#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"

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
            if (klass.name == "Player") return klass;
        }
    }
    assert(false && "Player class is missing");
    return module.classes.front();
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
    write_file(dir / "src/main.dd",
               "from macros import Debug\n"
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

    const dudu::ProjectIndex second = dudu::ProjectIndex::load(options);
    assert(second.macro_report().invocations == 1);
    assert(second.macro_report().worker_cache_hits == 1);
    assert(second.macro_report().expansion_cache_hits == 1);
    assert(player_class(second.merged_module()).methods.size() == 1);
}

} // namespace

int main() {
    test_dudu_macro_expands_before_semantics_and_caches();
    return 0;
}
