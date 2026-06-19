#include "dudu/language_server_navigation.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
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

template <typename Add> void visit_stmt_binding_names(const Stmt& stmt, Add add) {
    if ((stmt.kind == StmtKind::VarDecl || stmt.kind == StmtKind::For ||
         stmt.kind == StmtKind::Except) &&
        !stmt.name.empty()) {
        add(stmt.name, stmt.location);
        return;
    }
    if (stmt.kind != StmtKind::Assign) {
        return;
    }
    if (stmt.target_expr.kind == ExprKind::Name && !stmt.target_expr.name.empty()) {
        add(stmt.target_expr.name, stmt.target_expr.location);
        return;
    }
    if (stmt.target_expr.kind == ExprKind::TupleLiteral) {
        for (const Expr& child : stmt.target_expr.children) {
            if (child.kind == ExprKind::Name && !child.name.empty()) {
                add(child.name, child.location);
            }
        }
    }
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
    const std::function<void(const Expr&)> visit_expr = [&](const Expr& expr) {
        if (expr.kind == ExprKind::Name || expr.kind == ExprKind::Member) {
            std::string name = expr.name;
            if (prefer_member_path && expr.kind == ExprKind::Member) {
                if (const std::optional<ExprPath> path = expr_path_from_expr(expr)) {
                    name = render_expr_path(*path);
                }
            }
            set_if_hit(name, expr_name_location(expr));
        }
        for (const Expr& callee : expr.callee) {
            visit_expr(callee);
        }
        for (const Expr& param : expr.params) {
            visit_expr(param);
        }
        for (const Expr& arg : expr.template_args) {
            visit_expr(arg);
        }
        for (const Expr& child : expr.children) {
            visit_expr(child);
        }
    };
    const std::function<void(const std::vector<Stmt>&)> visit_stmts =
        [&](const std::vector<Stmt>& statements) {
            for (const Stmt& stmt : statements) {
                visit_stmt_binding_names(stmt, set_if_hit);
                visit_stmt_expressions(stmt, visit_expr);
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
        }
        for (const ConstDecl& constant : module.constants) {
            set_if_hit(constant.name, constant.location);
            visit_expr(constant.value_expr);
        }
        for (const EnumDecl& en : module.enums) {
            set_if_hit(en.name, en.location);
            for (const EnumValueDecl& value : en.values) {
                set_if_hit(value.name, value.location);
                visit_expr(value.value_expr);
            }
        }
        for (const ClassDecl& klass : module.classes) {
            set_if_hit(klass.name, klass.location);
            for (const FieldDecl& field : klass.fields) {
                set_if_hit(field.name, field.location);
                visit_expr(field.value_expr);
            }
            for (const ConstDecl& constant : klass.constants) {
                set_if_hit(constant.name, constant.location);
                visit_expr(constant.value_expr);
            }
            for (const ConstDecl& field : klass.static_fields) {
                set_if_hit(field.name, field.location);
                visit_expr(field.value_expr);
            }
            for (const FunctionDecl& method : klass.methods) {
                set_if_hit(method.name, method.location);
                for (const ParamDecl& param : method.params) {
                    set_if_hit(param.name, param.location);
                }
                visit_stmts(method.statements);
            }
        }
        for (const FunctionDecl& fn : module.functions) {
            set_if_hit(fn.name, fn.location);
            for (const ParamDecl& param : fn.params) {
                set_if_hit(param.name, param.location);
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
    const std::function<void(const Expr&)> visit_expr = [&](const Expr& expr) {
        if (result) {
            return;
        }
        if ((expr.kind == ExprKind::Name || expr.kind == ExprKind::Member) &&
            contains(expr_name_location(expr), expr.name)) {
            result = expr_path_from_expr(expr);
            return;
        }
        for (const Expr& callee : expr.callee) {
            visit_expr(callee);
        }
        for (const Expr& param : expr.params) {
            visit_expr(param);
        }
        for (const Expr& arg : expr.template_args) {
            visit_expr(arg);
        }
        for (const Expr& child : expr.children) {
            visit_expr(child);
        }
    };
    const std::function<void(const std::vector<Stmt>&)> visit_stmts =
        [&](const std::vector<Stmt>& statements) {
            for (const Stmt& stmt : statements) {
                visit_stmt_expressions(stmt, visit_expr);
                visit_stmts(stmt.children);
            }
        };
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ConstDecl& constant : module.constants) {
            visit_expr(constant.value_expr);
        }
        for (const EnumDecl& en : module.enums) {
            for (const EnumValueDecl& value : en.values) {
                visit_expr(value.value_expr);
            }
        }
        for (const ClassDecl& klass : module.classes) {
            for (const FieldDecl& field : klass.fields) {
                visit_expr(field.value_expr);
            }
            for (const ConstDecl& constant : klass.constants) {
                visit_expr(constant.value_expr);
            }
            for (const ConstDecl& field : klass.static_fields) {
                visit_expr(field.value_expr);
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

std::vector<ReferenceLocation> references_in(const Document& doc, const std::string& query) {
    std::vector<ReferenceLocation> out;
    if (query.empty()) {
        return out;
    }
    std::set<std::pair<int, int>> seen;
    const auto add = [&](const std::string& name, const SourceLocation& location) {
        if (name != query || location.line <= 0 || location.column <= 0) {
            return;
        }
        const auto key = std::pair{location.line, location.column};
        if (!seen.insert(key).second) {
            return;
        }
        const int line = location.line - 1;
        const int start = location.column - 1;
        out.push_back({uri_for_location(location, doc),
                       range_json(line, start, start + static_cast<int>(name.size()))});
    };
    const std::function<void(const Expr&)> visit_expr = [&](const Expr& expr) {
        if (expr.kind == ExprKind::Name || expr.kind == ExprKind::Member) {
            add(expr.name, expr_name_location(expr));
        }
        for (const Expr& callee : expr.callee) {
            visit_expr(callee);
        }
        for (const Expr& param : expr.params) {
            visit_expr(param);
        }
        for (const Expr& arg : expr.template_args) {
            visit_expr(arg);
        }
        for (const Expr& child : expr.children) {
            visit_expr(child);
        }
    };
    const std::function<void(const std::vector<Stmt>&)> visit_stmts =
        [&](const std::vector<Stmt>& statements) {
            for (const Stmt& stmt : statements) {
                visit_stmt_binding_names(stmt, add);
                visit_stmt_expressions(stmt, visit_expr);
                visit_stmts(stmt.children);
            }
        };
    try {
        const ModuleAst module = parse_source(doc.text, doc.path);
        for (const ImportDecl& import : module.imports) {
            add(bound_import_name(import), import.location);
            add(import.imported_name, import.location);
        }
        for (const TypeAliasDecl& alias : module.aliases) {
            add(alias.name, alias.location);
        }
        for (const ConstDecl& constant : module.constants) {
            add(constant.name, constant.location);
            visit_expr(constant.value_expr);
        }
        for (const EnumDecl& en : module.enums) {
            add(en.name, en.location);
            for (const EnumValueDecl& value : en.values) {
                add(value.name, value.location);
                visit_expr(value.value_expr);
            }
        }
        for (const ClassDecl& klass : module.classes) {
            add(klass.name, klass.location);
            for (const FieldDecl& field : klass.fields) {
                add(field.name, field.location);
                visit_expr(field.value_expr);
            }
            for (const ConstDecl& constant : klass.constants) {
                add(constant.name, constant.location);
                visit_expr(constant.value_expr);
            }
            for (const ConstDecl& field : klass.static_fields) {
                add(field.name, field.location);
                visit_expr(field.value_expr);
            }
            for (const FunctionDecl& method : klass.methods) {
                add(method.name, method.location);
                for (const ParamDecl& param : method.params) {
                    add(param.name, param.location);
                }
                visit_stmts(method.statements);
            }
        }
        for (const FunctionDecl& fn : module.functions) {
            add(fn.name, fn.location);
            for (const ParamDecl& param : fn.params) {
                add(param.name, param.location);
            }
            visit_stmts(fn.statements);
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
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

std::filesystem::path module_path_to_file(const std::filesystem::path& base,
                                          const std::string& module_path) {
    std::filesystem::path out = base;
    size_t start = 0;
    while (start < module_path.size()) {
        const size_t dot = module_path.find('.', start);
        const size_t end = dot == std::string::npos ? module_path.size() : dot;
        out /= module_path.substr(start, end - start);
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    out += ".dd";
    return out;
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace dudu
