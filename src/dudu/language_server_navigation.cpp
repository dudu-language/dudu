#include "dudu/language_server_navigation.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/language_server_ast_walk.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>

namespace dudu {

std::string range_json(const SourceLocation& location) {
    const int line = std::max(0, location.line - 1);
    const int column = std::max(0, location.column - 1);
    return range_json(line, column, column + 1);
}

std::string range_json(int line, int start_character, int end_character) {
    return range_json(line, start_character, line, end_character);
}

std::string range_json(int start_line, int start_character, int end_line, int end_character) {
    std::ostringstream out;
    out << "{\"start\":{\"line\":" << start_line << ",\"character\":" << start_character
        << "},\"end\":{\"line\":" << end_line << ",\"character\":" << end_character << "}}";
    return out.str();
}

LspPosition lsp_position(const Json* params) {
    const Json* position = params == nullptr ? nullptr : params->get("position");
    return {.line = required_int_value(position == nullptr ? nullptr : position->get("line"),
                                       "position.line"),
            .character = required_int_value(
                position == nullptr ? nullptr : position->get("character"), "position.character")};
}

std::string location_json(const std::string& uri, const std::string& range) {
    return "{\"uri\":\"" + json_escape(uri) + "\",\"range\":" + range + "}";
}

std::string uri_for_location(const SourceLocation& location, const Document& doc) {
    if (location.file.empty() || std::filesystem::path(location.file) == doc.path) {
        return doc.uri;
    }
    std::filesystem::path path = location.file;
    if (path.is_relative()) {
        path = std::filesystem::absolute(path);
    }
    return "file://" + path.lexically_normal().string();
}

std::string file_uri(const std::filesystem::path& path) {
    std::filesystem::path absolute = path;
    if (absolute.is_relative()) {
        absolute = std::filesystem::absolute(absolute);
    }
    return "file://" + absolute.lexically_normal().string();
}

SourceLocation expr_name_location(const Expr& expr) {
    if (expr.kind == ExprKind::Member && !expr.children.empty() &&
        expr.children.front().range.end.column > 0) {
        SourceLocation location = expr.children.front().range.end;
        location.column += 1;
        return location;
    }
    return expr.location;
}

std::optional<std::string> ast_symbol_at_impl(const Document& doc, const Json* params,
                                              bool prefer_member_path) {
    const LspPosition position = lsp_position(params);
    const int target_line = position.line + 1;
    const int target_column = position.character + 1;
    const auto contains = [&](const SourceLocation& location, const std::string& name) {
        if (name.empty() || location.line != target_line || location.column <= 0) {
            return false;
        }
        const int start = location.column;
        const int end = start + static_cast<int>(name.size());
        return target_column >= start && target_column <= end;
    };
    std::optional<std::string> result;
    const auto set_if_hit = [&](const std::string& name, const SourceLocation& location) {
        if (!result && contains(location, name)) {
            result = name;
        }
    };
    const auto visit_type = [&](const TypeRef& type) {
        if (result) {
            return;
        }
        set_if_hit(type_ref_text(type), type.location);
    };
    const auto visit_type_tree = [&](const TypeRef& type) { visit_type_ref_tree(type, visit_type); };
    const auto visit_expr = [&](const Expr& expr) {
        if (expr.kind == ExprKind::Name || expr.kind == ExprKind::Member) {
            std::string name = expr.name;
            if (prefer_member_path && expr.kind == ExprKind::Member) {
                if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
                    name = render_expr_path(*path);
                }
            }
            set_if_hit(name, expr_name_location(expr));
        }
    };
    const std::function<void(const std::vector<Stmt>&)> visit_stmts =
        [&](const std::vector<Stmt>& statements) {
            for (const Stmt& stmt : statements) {
                visit_stmt_binding_names(stmt, set_if_hit);
                visit_type_tree(stmt.type_ref);
                visit_stmt_expressions(stmt, [&](const Expr& expr) {
                    visit_lsp_expr_tree(expr, visit_expr, visit_type);
                });
                visit_stmts(stmt.children);
            }
        };
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ImportDecl& import : module.imports) {
            set_if_hit(bound_import_name(import), import.location);
            set_if_hit(import.imported_name, import.location);
        }
        for (const TypeAliasDecl& alias : module.aliases) {
            set_if_hit(alias.name, alias.location);
            visit_type_tree(alias.type_ref);
        }
        for (const ConstDecl& constant : module.constants) {
            set_if_hit(constant.name, constant.location);
            visit_type_tree(constant.type_ref);
            visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
        }
        for (const EnumDecl& en : module.enums) {
            set_if_hit(en.name, en.location);
            visit_type_tree(en.underlying_type_ref);
            for (const EnumValueDecl& value : en.values) {
                set_if_hit(value.name, value.location);
                for (const EnumPayloadField& field : value.payload_fields) {
                    set_if_hit(field.name, field.location);
                    visit_type_tree(field.type_ref);
                }
                visit_lsp_expr_tree(value.value_expr, visit_expr, visit_type);
            }
        }
        for (const ClassDecl& klass : module.classes) {
            set_if_hit(klass.name, klass.location);
            for (const BaseClassDecl& base : klass.base_class_refs) {
                visit_type_tree(base.type_ref);
            }
            for (const FieldDecl& field : klass.fields) {
                set_if_hit(field.name, field.location);
                visit_type_tree(field.type_ref);
                visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
            }
            for (const ConstDecl& constant : klass.constants) {
                set_if_hit(constant.name, constant.location);
                visit_type_tree(constant.type_ref);
                visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
            }
            for (const ConstDecl& field : klass.static_fields) {
                set_if_hit(field.name, field.location);
                visit_type_tree(field.type_ref);
                visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
            }
            for (const FunctionDecl& method : klass.methods) {
                set_if_hit(method.name, method.location);
                visit_type_tree(method.receiver_type_ref);
                visit_type_tree(method.return_type_ref);
                for (const ParamDecl& param : method.params) {
                    set_if_hit(param.name, param.location);
                    visit_type_tree(param.type_ref);
                }
                visit_stmts(method.statements);
            }
        }
        for (const FunctionDecl& fn : module.functions) {
            set_if_hit(fn.name, fn.location);
            visit_type_tree(fn.receiver_type_ref);
            visit_type_tree(fn.return_type_ref);
            for (const ParamDecl& param : fn.params) {
                set_if_hit(param.name, param.location);
                visit_type_tree(param.type_ref);
            }
            visit_stmts(fn.statements);
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return result;
}

std::optional<ExprPath> ast_expr_path_at_impl(const Document& doc, const Json* params) {
    const LspPosition position = lsp_position(params);
    const int target_line = position.line + 1;
    const int target_column = position.character + 1;
    const auto contains = [&](const SourceLocation& location, const std::string& name) {
        if (name.empty() || location.line != target_line || location.column <= 0) {
            return false;
        }
        const int start = location.column;
        const int end = start + static_cast<int>(name.size());
        return target_column >= start && target_column <= end;
    };
    std::optional<ExprPath> result;
    const auto visit_type = [](const TypeRef&) {};
    const auto visit_expr = [&](const Expr& expr) {
        if (result) {
            return;
        }
        if ((expr.kind == ExprKind::Name || expr.kind == ExprKind::Member) &&
            contains(expr_name_location(expr), expr.name)) {
            result = expr_path_from_expr(expr);
            return;
        }
    };
    const std::function<void(const std::vector<Stmt>&)> visit_stmts =
        [&](const std::vector<Stmt>& statements) {
            for (const Stmt& stmt : statements) {
                visit_stmt_expressions(stmt, [&](const Expr& expr) {
                    visit_lsp_expr_tree(expr, visit_expr, visit_type);
                });
                visit_stmts(stmt.children);
            }
        };
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ConstDecl& constant : module.constants) {
            visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
        }
        for (const EnumDecl& en : module.enums) {
            for (const EnumValueDecl& value : en.values) {
                visit_lsp_expr_tree(value.value_expr, visit_expr, visit_type);
            }
        }
        for (const ClassDecl& klass : module.classes) {
            for (const FieldDecl& field : klass.fields) {
                visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
            }
            for (const ConstDecl& constant : klass.constants) {
                visit_lsp_expr_tree(constant.value_expr, visit_expr, visit_type);
            }
            for (const ConstDecl& field : klass.static_fields) {
                visit_lsp_expr_tree(field.value_expr, visit_expr, visit_type);
            }
            for (const FunctionDecl& method : klass.methods) {
                visit_stmts(method.statements);
            }
        }
        for (const FunctionDecl& fn : module.functions) {
            visit_stmts(fn.statements);
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> ast_symbol_at(const Document& doc, const Json* params) {
    return ast_symbol_at_impl(doc, params, false);
}

std::optional<std::string> ast_symbol_path_at(const Document& doc, const Json* params) {
    return ast_symbol_at_impl(doc, params, true);
}

std::optional<ExprPath> ast_expr_path_at(const Document& doc, const Json* params) {
    return ast_expr_path_at_impl(doc, params);
}

bool symbol_matches(const std::string& symbol, const std::string& query) {
    if (symbol == query) {
        return true;
    }
    const size_t dot = symbol.rfind('.');
    return dot != std::string::npos && symbol.substr(dot + 1) == query;
}

bool symbol_char(char c) {
    return identifier_char(c) || c == '.';
}

bool identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool valid_identifier(const std::string& value) {
    if (value.empty() ||
        (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_')) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), identifier_char);
}

bool same_path(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    std::error_code error;
    const std::filesystem::path left = std::filesystem::weakly_canonical(lhs, error);
    if (error) {
        error.clear();
        return lhs.lexically_normal() == rhs.lexically_normal();
    }
    const std::filesystem::path right = std::filesystem::weakly_canonical(rhs, error);
    if (error) {
        error.clear();
        return lhs.lexically_normal() == rhs.lexically_normal();
    }
    return left == right;
}

bool skip_workspace_dir(const std::string& name) {
    return name == ".git" || name == "build" || name == ".dudu" || name == "node_modules" ||
           name == "vendor" || name == "third_party";
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace dudu
