#include "dudu/core/ast_type.hpp"
#include "dudu/parser/ast_type_token_parser.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_method_templates.hpp"

#include <cassert>
#include <string>
#include <utility>

namespace {

dudu::ClassDecl branch(std::string owner, std::string condition, std::string result) {
    dudu::ClassDecl specialization;
    specialization.name = std::move(owner);
    specialization.native_declaration = true;
    specialization.native_partial_specialization = true;
    specialization.generic_params = {"T"};
    specialization.native_specialization_args = {dudu::parse_type_text("T"),
                                                 dudu::parse_type_text(std::move(condition))};
    dudu::TypeAliasDecl alias;
    alias.name = "result";
    alias.type_ref = dudu::parse_type_text(std::move(result));
    specialization.type_aliases.push_back(std::move(alias));
    return specialization;
}

dudu::TypeRef associated_result(std::string owner) {
    dudu::TypeRef query;
    query.kind = dudu::TypeKind::Associated;
    query.name = "result";
    query.children.push_back(dudu::parse_type_text(std::move(owner)));
    return query;
}

void test_opaque_value_specializations_resolve_only_consensus_aliases() {
    dudu::ClassDecl primary;
    primary.name = "meta.Branch";
    primary.native_declaration = true;
    primary.generic_params = {"T", "Enabled"};

    dudu::Symbols agreeing;
    agreeing.classes.emplace(primary.name, &primary);
    agreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, "false", "T"));
    agreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, "true", "T"));

    const dudu::TypeRef query = associated_result("meta.Branch[i32, !bool(meta.Trait.value)]");
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(agreeing, query)) == "i32");

    dudu::Symbols disagreeing;
    disagreeing.classes.emplace(primary.name, &primary);
    disagreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, "false", "T"));
    disagreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, "true", "bool"));
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(disagreeing, query)) != "i32");
}

void test_unresolved_constraint_specializations_require_consensus() {
    dudu::ClassDecl primary;
    primary.name = "meta.Constrained";
    primary.native_declaration = true;
    primary.generic_params = {"T", "Enable"};
    primary.generic_min_args = 1;
    primary.generic_default_args = {{}, dudu::parse_type_text("void")};

    const std::string first_constraint = "meta.Enable[meta.First[T]].type";
    const std::string second_constraint = "meta.Enable[meta.Second[T]].type";
    const dudu::TypeRef query = associated_result("meta.Constrained[i32]");

    dudu::Symbols agreeing;
    agreeing.classes.emplace(primary.name, &primary);
    agreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, first_constraint, "T"));
    agreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, second_constraint, "T"));
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(agreeing, query)) == "i32");

    dudu::Symbols disagreeing;
    disagreeing.classes.emplace(primary.name, &primary);
    disagreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, first_constraint, "T"));
    disagreeing.native_class_specializations[primary.name].push_back(
        branch(primary.name, second_constraint, "bool"));
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(disagreeing, query)) != "i32");

    dudu::Symbols single;
    single.classes.emplace(primary.name, &primary);
    single.native_class_specializations[primary.name].push_back(
        branch(primary.name, first_constraint, "T"));
    assert(dudu::type_ref_text(dudu::resolve_associated_type_ref(single, query)) != "i32");
}

} // namespace

int main() {
    test_opaque_value_specializations_resolve_only_consensus_aliases();
    test_unresolved_constraint_specializations_require_consensus();
    return 0;
}
