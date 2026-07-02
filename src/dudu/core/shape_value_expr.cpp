#include "dudu/core/shape_value_expr.hpp"

#include "dudu/parser/ast_parse_utils.hpp"

#include <cctype>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace dudu {
namespace {

enum class TokKind {
    End,
    Number,
    Identifier,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    LParen,
    RParen,
    Invalid,
};

struct Tok {
    TokKind kind = TokKind::End;
    std::string text;
};

std::vector<Tok> lex(std::string_view text) {
    std::vector<Tok> out;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            const size_t begin = i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            out.push_back({TokKind::Number, std::string(text.substr(begin, i - begin))});
            continue;
        }
        if (is_identifier_start(c)) {
            const size_t begin = i;
            ++i;
            while (i < text.size() && is_identifier_continue(text[i])) {
                ++i;
            }
            out.push_back({TokKind::Identifier, std::string(text.substr(begin, i - begin))});
            continue;
        }
        switch (c) {
        case '+':
            out.push_back({TokKind::Plus, "+"});
            break;
        case '-':
            out.push_back({TokKind::Minus, "-"});
            break;
        case '*':
            out.push_back({TokKind::Star, "*"});
            break;
        case '/':
            out.push_back({TokKind::Slash, "/"});
            break;
        case '%':
            out.push_back({TokKind::Percent, "%"});
            break;
        case '(':
            out.push_back({TokKind::LParen, "("});
            break;
        case ')':
            out.push_back({TokKind::RParen, ")"});
            break;
        default:
            out.push_back({TokKind::Invalid, std::string(1, c)});
            break;
        }
        ++i;
    }
    out.push_back({TokKind::End, {}});
    return out;
}

struct Node {
    TokKind op = TokKind::End;
    std::string atom;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
};

class Parser {
  public:
    explicit Parser(std::string_view text) : tokens_(lex(text)) {
    }

    std::optional<Node> parse() {
        std::optional<Node> expr = parse_add();
        if (!expr || current().kind != TokKind::End) {
            return std::nullopt;
        }
        return expr;
    }

  private:
    const Tok& current() const {
        return tokens_[cursor_];
    }

    bool match(TokKind kind) {
        if (current().kind != kind) {
            return false;
        }
        ++cursor_;
        return true;
    }

    std::optional<Node> parse_add() {
        std::optional<Node> left = parse_mul();
        while (left && (current().kind == TokKind::Plus || current().kind == TokKind::Minus)) {
            const TokKind op = current().kind;
            ++cursor_;
            std::optional<Node> right = parse_mul();
            if (!right) {
                return std::nullopt;
            }
            Node node;
            node.op = op;
            node.left = std::make_unique<Node>(std::move(*left));
            node.right = std::make_unique<Node>(std::move(*right));
            left = std::move(node);
        }
        return left;
    }

    std::optional<Node> parse_mul() {
        std::optional<Node> left = parse_unary();
        while (left && (current().kind == TokKind::Star || current().kind == TokKind::Slash ||
                        current().kind == TokKind::Percent)) {
            const TokKind op = current().kind;
            ++cursor_;
            std::optional<Node> right = parse_unary();
            if (!right) {
                return std::nullopt;
            }
            Node node;
            node.op = op;
            node.left = std::make_unique<Node>(std::move(*left));
            node.right = std::make_unique<Node>(std::move(*right));
            left = std::move(node);
        }
        return left;
    }

    std::optional<Node> parse_unary() {
        if (match(TokKind::Plus)) {
            return parse_unary();
        }
        if (match(TokKind::Minus)) {
            std::optional<Node> right = parse_unary();
            if (!right) {
                return std::nullopt;
            }
            Node zero;
            zero.atom = "0";
            Node node;
            node.op = TokKind::Minus;
            node.left = std::make_unique<Node>(std::move(zero));
            node.right = std::make_unique<Node>(std::move(*right));
            return node;
        }
        return parse_primary();
    }

    std::optional<Node> parse_primary() {
        if (current().kind == TokKind::Number || current().kind == TokKind::Identifier) {
            Node node;
            node.atom = current().text;
            ++cursor_;
            return node;
        }
        if (match(TokKind::LParen)) {
            std::optional<Node> inner = parse_add();
            if (!inner || !match(TokKind::RParen)) {
                return std::nullopt;
            }
            return inner;
        }
        return std::nullopt;
    }

    std::vector<Tok> tokens_;
    size_t cursor_ = 0;
};

std::string op_text(TokKind op) {
    switch (op) {
    case TokKind::Plus:
        return "+";
    case TokKind::Minus:
        return "-";
    case TokKind::Star:
        return "*";
    case TokKind::Slash:
        return "/";
    case TokKind::Percent:
        return "%";
    default:
        return {};
    }
}

int precedence(TokKind op) {
    switch (op) {
    case TokKind::Plus:
    case TokKind::Minus:
        return 1;
    case TokKind::Star:
    case TokKind::Slash:
    case TokKind::Percent:
        return 2;
    default:
        return 3;
    }
}

std::string normalize_node(const Node& node, int parent_precedence = 0) {
    if (node.op == TokKind::End) {
        return node.atom;
    }
    const int current_precedence = precedence(node.op);
    std::string text = normalize_node(*node.left, current_precedence) + " " + op_text(node.op) +
                       " " + normalize_node(*node.right, current_precedence + 1);
    if (current_precedence < parent_precedence) {
        return "(" + text + ")";
    }
    return text;
}

void collect_identifiers(const Node& node, std::set<std::string>& out) {
    if (node.op == TokKind::End) {
        if (!node.atom.empty() && is_identifier(node.atom)) {
            out.insert(node.atom);
        }
        return;
    }
    collect_identifiers(*node.left, out);
    collect_identifiers(*node.right, out);
}

std::optional<long long> parse_integer(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    long long value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const long long digit = c - '0';
        if (value > (std::numeric_limits<long long>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

std::optional<long long> eval_node(const Node& node,
                                   const std::map<std::string, long long>& bindings) {
    if (node.op == TokKind::End) {
        if (const auto number = parse_integer(node.atom)) {
            return number;
        }
        const auto found = bindings.find(node.atom);
        if (found == bindings.end()) {
            return std::nullopt;
        }
        return found->second;
    }
    const auto left = eval_node(*node.left, bindings);
    const auto right = eval_node(*node.right, bindings);
    if (!left || !right) {
        return std::nullopt;
    }
    switch (node.op) {
    case TokKind::Plus:
        return *left + *right;
    case TokKind::Minus:
        return *left - *right;
    case TokKind::Star:
        return *left * *right;
    case TokKind::Slash:
        if (*right == 0) {
            return std::nullopt;
        }
        return *left / *right;
    case TokKind::Percent:
        if (*right == 0) {
            return std::nullopt;
        }
        return *left % *right;
    default:
        return std::nullopt;
    }
}

Node substitute_node(Node node, const std::map<std::string, std::string>& bindings) {
    if (node.op == TokKind::End) {
        const auto found = bindings.find(node.atom);
        if (found != bindings.end()) {
            Parser parser(found->second);
            if (std::optional<Node> parsed = parser.parse()) {
                return *std::move(parsed);
            }
            node.atom = found->second;
        }
        return node;
    }
    node.left = std::make_unique<Node>(substitute_node(std::move(*node.left), bindings));
    node.right = std::make_unique<Node>(substitute_node(std::move(*node.right), bindings));
    return node;
}

std::optional<Node> parse_expr(std::string_view text) {
    Parser parser(trim_view(text));
    return parser.parse();
}

} // namespace

bool shape_value_expr_valid(std::string_view text) {
    return parse_expr(text).has_value();
}

std::string normalize_shape_value_expr(std::string_view text) {
    std::optional<Node> expr = parse_expr(text);
    return expr ? normalize_node(*expr) : std::string{};
}

std::set<std::string> shape_value_expr_identifiers(std::string_view text) {
    std::set<std::string> out;
    std::optional<Node> expr = parse_expr(text);
    if (expr) {
        collect_identifiers(*expr, out);
    }
    return out;
}

std::optional<long long> shape_value_expr_eval(std::string_view text,
                                               const std::map<std::string, long long>& bindings) {
    std::optional<Node> expr = parse_expr(text);
    if (!expr) {
        return std::nullopt;
    }
    return eval_node(*expr, bindings);
}

std::string shape_value_expr_substitute(std::string_view text,
                                        const std::map<std::string, std::string>& bindings) {
    std::optional<Node> expr = parse_expr(text);
    if (!expr) {
        return trim_string(text);
    }
    Node substituted = substitute_node(std::move(*expr), bindings);
    const std::string normalized = normalize_node(substituted);
    if (const auto value = shape_value_expr_eval(normalized)) {
        return std::to_string(*value);
    }
    return normalized;
}

bool shape_value_expr_equivalent(std::string_view left, std::string_view right) {
    const std::string left_normal = normalize_shape_value_expr(left);
    const std::string right_normal = normalize_shape_value_expr(right);
    if (left_normal.empty() || right_normal.empty()) {
        return trim_view(left) == trim_view(right);
    }
    if (left_normal == right_normal) {
        return true;
    }
    const auto left_value = shape_value_expr_eval(left_normal);
    const auto right_value = shape_value_expr_eval(right_normal);
    return left_value && right_value && *left_value == *right_value;
}

} // namespace dudu
