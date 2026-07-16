#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_semantic_index.hpp"
#include "dudu/lsp/language_server_semantic_token_wire.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace dudu {

void collect_semantic_type_tokens(const TypeRef& type, std::vector<SemanticToken>& tokens,
                                  const DuduSemanticIndex& dudu_index,
                                  const NativeSemanticIndex* native_index);
void collect_semantic_expr_tokens(const Expr& expr, std::vector<SemanticToken>& tokens,
                                  const DuduSemanticIndex& dudu_index,
                                  const NativeSemanticIndex* native_index,
                                  const std::set<std::string>* local_bindings = nullptr,
                                  const std::map<std::string, TypeRef>* local_types = nullptr);
void collect_semantic_stmt_tokens(const std::vector<Stmt>& statements,
                                  std::vector<SemanticToken>& tokens,
                                  const DuduSemanticIndex& dudu_index,
                                  const NativeSemanticIndex* native_index,
                                  std::set<std::string>& local_bindings,
                                  std::map<std::string, TypeRef>& local_types);

} // namespace dudu
