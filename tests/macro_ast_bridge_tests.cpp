#include "dudu/core/ast_expr.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/macro/macro_hygiene.hpp"
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
    const dudu::ModuleAst module =
        dudu::parse_source("@packed\n"
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
    const auto declarations =
        dudu::macro::declarations_for_invocations(module, plan.invocations);
    assert(declarations.size() == 1);
    assert(declarations.front().class_decl->name == declaration.class_decl->name);
    assert(declarations.front().class_decl->identity->module == "main");
}

dudu::macro::protocol::GeneratedDeclaration
generated_function(const std::string& name, dudu::macro::protocol::Visibility visibility,
                   const std::string& referenced_name = {},
                   const std::string& parameter_name = {}) {
    namespace p = dudu::macro::protocol;
    p::FunctionDecl function;
    function.name = name;
    function.visibility = visibility;
    if (!parameter_name.empty()) {
        p::Parameter parameter;
        parameter.name = parameter_name;
        parameter.type.kind = p::TypeKind::Named;
        parameter.type.name = "i32";
        function.parameters.push_back(parameter);
    }
    if (!referenced_name.empty()) {
        p::Expression reference;
        reference.kind = p::ExpressionKind::Name;
        reference.name = referenced_name;
        p::Statement statement;
        statement.kind = p::StatementKind::Return;
        statement.value = reference;
        function.body.push_back(statement);
    }
    p::Declaration declaration;
    declaration.kind = p::DeclarationKind::Function;
    declaration.function_decl = function;
    p::GeneratedDeclaration generated;
    generated.declaration = declaration;
    return generated;
}

void test_hygiene_renames_private_helpers_deterministically() {
    namespace p = dudu::macro::protocol;
    p::Expansion first;
    first.members.push_back(generated_function("generated", p::Visibility::Default, "helper"));
    first.siblings.push_back(generated_function("helper", p::Visibility::Private));
    p::SourceRange invocation;
    invocation.file = "main.dd";
    invocation.start.line = 7;
    invocation.start.column = 4;

    p::Expansion same = first;
    dudu::macro::apply_expansion_hygiene(first, "macros.Debug", "main", "Player", invocation);
    dudu::macro::apply_expansion_hygiene(same, "macros.Debug", "main", "Player", invocation);
    const std::string renamed = first.siblings.front().declaration.function_decl->name;
    assert(renamed.starts_with("__dudu_macro_"));
    assert(renamed == same.siblings.front().declaration.function_decl->name);
    assert(first.members.front().declaration.function_decl->body.front().value->name == renamed);

    p::Expansion other;
    other.siblings.push_back(generated_function("helper", p::Visibility::Private));
    ++invocation.start.line;
    dudu::macro::apply_expansion_hygiene(other, "macros.Debug", "main", "Player", invocation);
    assert(other.siblings.front().declaration.function_decl->name != renamed);
}

void test_hygiene_preserves_public_names_and_lexical_shadowing() {
    namespace p = dudu::macro::protocol;
    p::Expansion expansion;
    expansion.members.push_back(
        generated_function("shadow", p::Visibility::Default, "helper", "helper"));
    auto local_shadow = generated_function("local_shadow", p::Visibility::Default, "helper");
    p::Statement local;
    local.kind = p::StatementKind::Variable;
    local.name = "helper";
    p::Expression initial_value;
    initial_value.kind = p::ExpressionKind::IntLiteral;
    initial_value.value = "1";
    local.value = initial_value;
    local_shadow.declaration.function_decl->body.insert(
        local_shadow.declaration.function_decl->body.begin(), local);
    expansion.members.push_back(local_shadow);
    expansion.siblings.push_back(generated_function("helper", p::Visibility::Private));
    expansion.siblings.push_back(generated_function("PublicHelper", p::Visibility::Default));

    dudu::macro::apply_expansion_hygiene(expansion, "macros.Debug", "main", "Player", {});
    const auto& shadow = *expansion.members.front().declaration.function_decl;
    assert(shadow.parameters.front().name == "helper");
    assert(shadow.body.front().value->name == "helper");
    const auto& local_function = *expansion.members.back().declaration.function_decl;
    assert(local_function.body.back().value->name == "helper");
    assert(expansion.siblings.back().declaration.function_decl->name == "PublicHelper");
}

} // namespace

int main() {
    test_class_roundtrip_preserves_structured_bodies();
    test_invocation_target_uses_original_declaration();
    test_hygiene_renames_private_helpers_deterministically();
    test_hygiene_preserves_public_names_and_lexical_shadowing();
    return 0;
}
