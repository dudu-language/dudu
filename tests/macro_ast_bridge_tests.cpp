#include "dudu/core/ast_expr.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/macro/macro_registry.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"

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

void test_class_roundtrip_preserves_structured_bodies() {
    const dudu::ModuleAst module = dudu::parse_source(
        "@packed\n"
        "class Player[T]:\n"
        "    hp: i32 = 10\n"
        "    LIMIT: i32 = 100\n"
        "    count: static[i32] = 0\n"
        "\n"
        "    def heal(self, amount: i32) -> i32:\n"
        "        if amount > 0:\n"
        "            self.hp += amount\n"
        "        return clamp[i32](self.hp, 0, Player.LIMIT)\n",
        "player.dd");
    const dudu::ClassDecl& source = module.classes.front();
    const auto public_value = dudu::macro::to_protocol(source, "player");
    assert(public_value.fields.size() == 1);
    assert(public_value.constants.size() == 1);
    assert(public_value.static_fields.size() == 1);
    assert(public_value.methods.front().body.size() == 2);
    const auto& call = public_value.methods.front().body.back().value;
    assert(call.has_value());
    assert(call->kind == dudu::macro::protocol::ExpressionKind::TemplateCall);
    assert(call->callee.size() == 1);
    assert(call->type_arguments.size() == 1);

    const dudu::ClassDecl decoded =
        dudu::macro::from_protocol(public_value, "generated", source.location);
    assert(decoded.name == "Player");
    assert(decoded.generic_params == std::vector<std::string>{"T"});
    assert(decoded.fields.size() == 1);
    assert(decoded.constants.size() == 1);
    assert(decoded.static_fields.size() == 1);
    assert(decoded.methods.size() == 1);
    assert(decoded.methods.front().statements.front().kind == dudu::StmtKind::If);
    assert(decoded.methods.front().statements.front().children.front().compound_op ==
           dudu::CompoundAssignOp::Add);
    const dudu::Expr& decoded_call = decoded.methods.front().statements.back().value_expr;
    assert(dudu::direct_callee_name(decoded_call) == "clamp");
    assert(dudu::expr_template_type_args(decoded_call).size() == 1);
}

void test_invocation_target_uses_original_declaration() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_ast_bridge_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "macros.dd", "import dudu.ast as ast\n"
                                  "@macro\n"
                                  "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
                                  "    return ast.expansion()\n");
    write_file(dir / "main.dd", "from macros import Debug\n"
                                "@derive(Debug)\n"
                                "class Player:\n"
                                "    hp: i32\n");
    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    const dudu::macro::Plan plan = dudu::macro::build_plan(module);
    const auto declaration =
        dudu::macro::declaration_for_invocation(module, plan.invocations.front());
    assert(declaration.kind == dudu::macro::protocol::DeclarationKind::Class);
    assert(declaration.class_decl->name == "Player");
    assert(declaration.class_decl->fields.front().name == "hp");
    assert(declaration.class_decl->identity->module == "main");
}

} // namespace

int main() {
    test_class_roundtrip_preserves_structured_bodies();
    test_invocation_target_uses_original_declaration();
    return 0;
}
