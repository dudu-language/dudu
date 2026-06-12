#include "dudu/cpp_emit.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct SourceLine {
    int number = 0;
    int indent = 0;
    std::string text;
};

struct TreeLine {
    SourceLine line;
    std::vector<TreeLine> children;
};

struct IncludeDecl {
    std::string mode;
    std::string path;
    std::string alias;
    bool system = false;
};

struct FieldDecl {
    std::string name;
    std::vector<std::string> type;
};

struct ThingDecl {
    std::string name;
    std::vector<FieldDecl> fields;
};

struct AliasDecl {
    std::string name;
    std::vector<std::string> type;
};

struct EnumValue {
    std::string name;
    std::optional<std::string> value;
};

struct EnumDecl {
    std::string name;
    std::vector<std::string> underlying;
    std::vector<EnumValue> values;
};

struct ParamDecl {
    std::string name;
    std::vector<std::string> type;
};

struct FunctionDecl {
    std::string name;
    std::vector<std::string> return_type;
    std::vector<ParamDecl> params;
    std::vector<TreeLine> body;
};

struct Program {
    std::vector<IncludeDecl> includes;
    std::vector<ThingDecl> things;
    std::vector<AliasDecl> aliases;
    std::vector<EnumDecl> enums;
    std::vector<FunctionDecl> functions;
    std::set<std::string> type_names;
    std::map<std::string, IncludeDecl> external_aliases;
};

struct Options {
    std::filesystem::path input;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> header_output;
    bool emit_cpp = false;
    bool check = false;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string trim(const std::string& text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
        --last;
    }
    return text.substr(first, last - first);
}

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

std::string strip_comment(const std::string& text) {
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        if (c == '#' && !in_string) {
            return text.substr(0, i);
        }
    }
    return text;
}

int count_indent(const std::string& text, int line_number) {
    int spaces = 0;
    for (const char c : text) {
        if (c == ' ') {
            ++spaces;
            continue;
        }
        if (c == '\t') {
            fail("line " + std::to_string(line_number) +
                 ": tabs are not supported for indentation yet");
        }
        break;
    }
    if (spaces % 4 != 0) {
        fail("line " + std::to_string(line_number) +
             ": indentation must be a multiple of 4 spaces");
    }
    return spaces / 4;
}

std::vector<SourceLine> read_source(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        fail("could not open " + path.string());
    }

    std::vector<SourceLine> lines;
    std::string raw;
    int number = 1;
    while (std::getline(file, raw)) {
        std::string text = trim(strip_comment(raw));
        if (!text.empty()) {
            lines.push_back({number, count_indent(raw, number), text});
        } else {
            lines.push_back({number, -1, ""});
        }
        ++number;
    }
    return lines;
}

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        if (text[i] == '"') {
            size_t start = i++;
            bool escaped = false;
            while (i < text.size()) {
                if (escaped) {
                    escaped = false;
                } else if (text[i] == '\\') {
                    escaped = true;
                } else if (text[i] == '"') {
                    ++i;
                    break;
                }
                ++i;
            }
            words.push_back(text.substr(start, i - start));
            continue;
        }
        if (is_identifier_start(text[i])) {
            size_t start = i++;
            while (i < text.size() && is_identifier_char(text[i])) {
                ++i;
            }
            words.push_back(text.substr(start, i - start));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
            size_t start = i++;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 ||
                                       text[i] == '.' || text[i] == '_')) {
                ++i;
            }
            words.push_back(text.substr(start, i - start));
            continue;
        }
        if (i + 1 < text.size()) {
            const std::string two = text.substr(i, 2);
            if (two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "+=" ||
                two == "-=" || two == "*=" || two == "/=" || two == "&&" || two == "||" ||
                two == "<<" || two == ">>" || two == "..") {
                words.push_back(two);
                i += 2;
                continue;
            }
        }
        words.push_back(text.substr(i, 1));
        ++i;
    }
    return words;
}

std::string join(const std::vector<std::string>& values, std::string_view separator) {
    std::string result;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            result += separator;
        }
        result += values[i];
    }
    return result;
}

std::string indent(int depth) {
    return std::string(static_cast<size_t>(depth) * 4, ' ');
}

bool is_operator_token(const std::string& token) {
    static const std::set<std::string> operators = {
        "+",  "-",  "*",  "/",  "%", "==", "!=", "<",  ">",
        "<=", ">=", "&&", "||", "|", "&",  "^",  "<<", ">>"};
    return operators.contains(token);
}

bool is_assignment_operator(const std::string& token) {
    static const std::set<std::string> operators = {"=", "+=", "-=", "*=", "/="};
    return operators.contains(token);
}

bool is_builtin_type(const std::string& token) {
    static const std::set<std::string> types = {"bool",  "i8",  "i16", "i32",  "i64",
                                                "u8",    "u16", "u32", "u64",  "isize",
                                                "usize", "f32", "f64", "void", "cstr"};
    return types.contains(token);
}

bool is_type_modifier(const std::string& token) {
    static const std::set<std::string> modifiers = {"ref", "mut", "ptr", "const", "span", "arr"};
    return modifiers.contains(token);
}

bool looks_like_type_name(const std::string& token) {
    if (token.empty()) {
        return false;
    }
    if (is_builtin_type(token) || is_type_modifier(token)) {
        return true;
    }
    const size_t dot = token.rfind('.');
    const std::string name = dot == std::string::npos ? token : token.substr(dot + 1);
    const bool all_caps = std::all_of(name.begin(), name.end(), [](char c) {
        return std::isupper(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
    if (all_caps) {
        return false;
    }
    const char c = name[0];
    return std::isupper(static_cast<unsigned char>(c)) != 0;
}

std::string cpp_builtin_type(const std::string& token) {
    static const std::map<std::string, std::string> builtins = {
        {"bool", "bool"},    {"i8", "int8_t"},      {"i16", "int16_t"},     {"i32", "int32_t"},
        {"i64", "int64_t"},  {"u8", "uint8_t"},     {"u16", "uint16_t"},    {"u32", "uint32_t"},
        {"u64", "uint64_t"}, {"isize", "intptr_t"}, {"usize", "size_t"},    {"f32", "float"},
        {"f64", "double"},   {"void", "void"},      {"cstr", "const char*"}};
    const auto found = builtins.find(token);
    return found == builtins.end() ? token : found->second;
}

std::string strip_external_alias(const Program& program, const std::string& token) {
    const size_t dot = token.find('.');
    if (dot == std::string::npos) {
        return token;
    }
    const std::string alias = token.substr(0, dot);
    const auto found = program.external_aliases.find(alias);
    if (found == program.external_aliases.end()) {
        std::string out = token;
        std::replace(out.begin(), out.end(), '.', ':');
        return out;
    }
    return token.substr(dot + 1);
}

std::string cpp_type(const Program& program, const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        return "void";
    }
    if (tokens[0] == "ref") {
        if (tokens.size() >= 3 && tokens[1] == "mut") {
            return cpp_type(program, {tokens.begin() + 2, tokens.end()}) + "&";
        }
        return "const " + cpp_type(program, {tokens.begin() + 1, tokens.end()}) + "&";
    }
    if (tokens[0] == "ptr") {
        if (tokens.size() >= 3 && tokens[1] == "const") {
            return "const " + cpp_type(program, {tokens.begin() + 2, tokens.end()}) + "*";
        }
        return cpp_type(program, {tokens.begin() + 1, tokens.end()}) + "*";
    }
    if (tokens[0] == "span" && tokens.size() >= 2) {
        return "std::span<" + cpp_type(program, {tokens.begin() + 1, tokens.end()}) + ">";
    }
    if (tokens[0] == "arr" && tokens.size() >= 3) {
        return "std::array<" + cpp_type(program, {tokens.begin() + 1, tokens.end() - 1}) + ", " +
               tokens.back() + ">";
    }
    if (tokens.size() == 1) {
        return cpp_builtin_type(strip_external_alias(program, tokens[0]));
    }
    std::vector<std::string> converted;
    converted.reserve(tokens.size());
    for (const std::string& token : tokens) {
        converted.push_back(cpp_builtin_type(strip_external_alias(program, token)));
    }
    return join(converted, " ");
}

std::vector<TreeLine> parse_tree_block(const std::vector<SourceLine>& lines, size_t& index,
                                       int indent_level) {
    std::vector<TreeLine> out;
    while (index < lines.size()) {
        const SourceLine& line = lines[index];
        if (line.indent == -1) {
            ++index;
            continue;
        }
        if (line.indent < indent_level) {
            break;
        }
        if (line.indent > indent_level) {
            fail("line " + std::to_string(line.number) + ": unexpected indentation");
        }
        TreeLine node{line, {}};
        ++index;
        if (index < lines.size() && lines[index].indent > indent_level) {
            node.children = parse_tree_block(lines, index, lines[index].indent);
        }
        out.push_back(std::move(node));
    }
    return out;
}

std::vector<std::string> words_after(const std::vector<std::string>& words, size_t start) {
    if (start >= words.size()) {
        return {};
    }
    return {words.begin() + static_cast<std::ptrdiff_t>(start), words.end()};
}

std::vector<SourceLine> child_lines_until_top(const std::vector<SourceLine>& lines, size_t& index,
                                              int indent_level) {
    std::vector<SourceLine> out;
    while (index < lines.size()) {
        const SourceLine& line = lines[index];
        if (line.indent == -1) {
            ++index;
            continue;
        }
        if (line.indent < indent_level) {
            break;
        }
        if (line.indent == 0) {
            break;
        }
        if (line.indent != indent_level) {
            fail("line " + std::to_string(line.number) + ": unsupported nested top-level block");
        }
        out.push_back(line);
        ++index;
    }
    return out;
}

IncludeDecl parse_include(const std::vector<std::string>& words, int line_number) {
    IncludeDecl include;
    size_t path_index = 0;
    if (words.size() >= 3 && words[0] == "use" && (words[1] == "c" || words[1] == "cpp")) {
        include.mode = words[1];
        path_index = 2;
    } else if (words.size() >= 2 && words[0] == "use") {
        include.mode = "dudu";
        path_index = 1;
    } else if (words.size() >= 2 && words[0] == "include") {
        include.mode = "dudu";
        path_index = 1;
    } else if (words.size() >= 3 && (words[0] == "c" || words[0] == "cpp") &&
               words[1] == "include") {
        include.mode = words[0];
        path_index = 2;
    } else {
        fail("line " + std::to_string(line_number) + ": invalid include");
    }

    include.path = words[path_index];
    if (include.path.size() < 2 || include.path.front() != '"' || include.path.back() != '"') {
        fail("line " + std::to_string(line_number) + ": include path must be quoted");
    }
    include.path = include.path.substr(1, include.path.size() - 2);
    include.system =
        include.path.find('/') == std::string::npos && include.path.find('.') != std::string::npos;

    if (path_index + 2 < words.size() && words[path_index + 1] == "as") {
        include.alias = words[path_index + 2];
    } else if (include.mode == "c" || include.mode == "cpp") {
        fail("line " + std::to_string(line_number) + ": external include requires 'as alias'");
    }
    return include;
}

FunctionDecl parse_function(const std::vector<SourceLine>& lines, size_t& index,
                            const std::vector<std::string>& header) {
    FunctionDecl fn;
    fn.name = header[1];
    fn.return_type = words_after(header, 2);
    ++index;

    bool saw_blank = false;
    while (index < lines.size()) {
        const SourceLine& line = lines[index];
        if (line.indent == -1) {
            saw_blank = true;
            ++index;
            break;
        }
        if (line.indent == 0) {
            break;
        }
        if (line.indent != 1) {
            fail("line " + std::to_string(line.number) + ": function parameters use one indent");
        }
        const std::vector<std::string> words = split_words(line.text);
        if (words.size() < 2) {
            fail("line " + std::to_string(line.number) + ": parameter requires name and type");
        }
        fn.params.push_back({words[0], words_after(words, 1)});
        ++index;
    }

    if (!saw_blank && index < lines.size() && lines[index].indent > 0) {
        fail("line " + std::to_string(lines[index].number) + ": blank line required before body");
    }
    if (index < lines.size() && lines[index].indent > 0) {
        fn.body = parse_tree_block(lines, index, lines[index].indent);
    }
    return fn;
}

void parse_file_into(Program& program, const std::filesystem::path& path,
                     std::set<std::filesystem::path>& seen_files) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    if (seen_files.contains(canonical)) {
        return;
    }
    seen_files.insert(canonical);
    const std::vector<SourceLine> lines = read_source(path);
    size_t index = 0;
    while (index < lines.size()) {
        const SourceLine& line = lines[index];
        if (line.indent == -1) {
            ++index;
            continue;
        }
        if (line.indent != 0) {
            fail("line " + std::to_string(line.number) + ": expected top-level form");
        }
        const std::vector<std::string> words = split_words(line.text);
        if (words.empty()) {
            ++index;
            continue;
        }

        if (words[0] == "include" || words[0] == "use" || words[0] == "c" || words[0] == "cpp") {
            IncludeDecl include = parse_include(words, line.number);
            if (include.mode == "c" || include.mode == "cpp") {
                program.external_aliases[include.alias] = include;
                program.includes.push_back(std::move(include));
            } else {
                const std::filesystem::path include_path = path.parent_path() / include.path;
                parse_file_into(program, include_path, seen_files);
            }
            ++index;
            continue;
        }
        if (words[0] == "th") {
            if (words.size() != 2) {
                fail("line " + std::to_string(line.number) + ": th requires a name");
            }
            ThingDecl thing;
            thing.name = words[1];
            program.type_names.insert(thing.name);
            ++index;
            for (const SourceLine& child : child_lines_until_top(lines, index, 1)) {
                const std::vector<std::string> field_words = split_words(child.text);
                if (field_words.size() < 2) {
                    fail("line " + std::to_string(child.number) + ": field requires name and type");
                }
                thing.fields.push_back({field_words[0], words_after(field_words, 1)});
            }
            program.things.push_back(std::move(thing));
            continue;
        }
        if (words[0] == "tp") {
            if (words.size() < 3) {
                fail("line " + std::to_string(line.number) + ": tp requires name and type");
            }
            program.aliases.push_back({words[1], words_after(words, 2)});
            program.type_names.insert(words[1]);
            ++index;
            continue;
        }
        if (words[0] == "enum") {
            if (words.size() < 2) {
                fail("line " + std::to_string(line.number) + ": enum requires a name");
            }
            EnumDecl en;
            en.name = words[1];
            en.underlying = words_after(words, 2);
            program.type_names.insert(en.name);
            ++index;
            for (const SourceLine& child : child_lines_until_top(lines, index, 1)) {
                const std::vector<std::string> value_words = split_words(child.text);
                if (value_words.empty()) {
                    continue;
                }
                EnumValue value;
                value.name = value_words[0];
                if (value_words.size() >= 3 && value_words[1] == "=") {
                    value.value = join(words_after(value_words, 2), " ");
                }
                en.values.push_back(std::move(value));
            }
            program.enums.push_back(std::move(en));
            continue;
        }
        if (words[0] == "fn") {
            if (words.size() < 2) {
                fail("line " + std::to_string(line.number) + ": fn requires a name");
            }
            program.functions.push_back(parse_function(lines, index, words));
            continue;
        }
        fail("line " + std::to_string(line.number) + ": unknown top-level form: " + words[0]);
    }
}

Program parse_program(const std::filesystem::path& path) {
    Program program;
    for (const std::string type : {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                                   "isize", "usize", "f32", "f64", "void", "cstr"}) {
        program.type_names.insert(type);
    }
    std::set<std::filesystem::path> seen_files;
    parse_file_into(program, path, seen_files);
    return program;
}

class Emitter {
  public:
    explicit Emitter(Program program) : program_(std::move(program)) {
    }

    std::string emit() {
        std::ostringstream out;
        out << "#include <array>\n";
        out << "#include <cstddef>\n";
        out << "#include <cstdint>\n";
        out << "#include <span>\n";
        for (const IncludeDecl& include : program_.includes) {
            if (include.mode == "dudu") {
                continue;
            }
            out << "#include ";
            if (include.system) {
                out << '<' << include.path << ">\n";
            } else {
                out << '"' << include.path << "\"\n";
            }
        }
        out << "\n";

        for (const AliasDecl& alias : program_.aliases) {
            out << "using " << alias.name << " = " << cpp_type(program_, alias.type) << ";\n";
        }
        if (!program_.aliases.empty()) {
            out << "\n";
        }

        for (const ThingDecl& thing : program_.things) {
            out << "struct " << thing.name << " {\n";
            for (const FieldDecl& field : thing.fields) {
                out << "    " << cpp_type(program_, field.type) << ' ' << field.name << "{};\n";
            }
            out << "};\n\n";
        }

        for (const EnumDecl& en : program_.enums) {
            out << "enum class " << en.name;
            if (!en.underlying.empty()) {
                out << " : " << cpp_type(program_, en.underlying);
            }
            out << " {\n";
            for (const EnumValue& value : en.values) {
                out << "    " << value.name;
                if (value.value.has_value()) {
                    out << " = " << *value.value;
                }
                out << ",\n";
            }
            out << "};\n\n";
        }

        for (const FunctionDecl& fn : program_.functions) {
            emit_function(out, fn);
            out << "\n";
        }
        return out.str();
    }

  private:
    Program program_;

    std::string translate_identifier(const std::string& token) const {
        if (token == "null") {
            return "nullptr";
        }
        if (token == "true" || token == "false") {
            return token;
        }
        return strip_external_alias(program_, token);
    }

    bool is_dotted_external(const std::string& token) const {
        const size_t dot = token.find('.');
        if (dot == std::string::npos) {
            return false;
        }
        return program_.external_aliases.contains(token.substr(0, dot));
    }

    bool is_all_caps_external_constant(const std::string& token) const {
        if (!is_dotted_external(token)) {
            return false;
        }
        const std::string name = token.substr(token.rfind('.') + 1);
        return std::all_of(name.begin(), name.end(), [](char c) {
            return std::isupper(static_cast<unsigned char>(c)) != 0 || c == '_';
        });
    }

    std::string translate_callee(const std::string& token) const {
        return strip_external_alias(program_, token);
    }

    std::string translate_value_identifier(const std::string& token) const {
        const std::string translated = strip_external_alias(program_, token);
        if (is_dotted_external(token) && !is_all_caps_external_constant(token)) {
            return translated + "()";
        }
        return translated;
    }

    bool is_call_like(const std::vector<std::string>& tokens) const {
        if (tokens.size() < 2 || !is_identifier_start(tokens[0][0])) {
            return false;
        }
        for (const std::string& token : tokens) {
            if (is_operator_token(token) || is_assignment_operator(token) || token == "(" ||
                token == ")" || token == "[" || token == "]") {
                return false;
            }
        }
        return true;
    }

    bool is_constructor_call(const std::vector<std::string>& tokens) const {
        return !tokens.empty() && tokens.size() > 1 && program_.type_names.contains(tokens[0]);
    }

    std::string emit_atom(const std::string& token) const {
        if (token == "not") {
            return "!";
        }
        if (token == "and") {
            return "&&";
        }
        if (token == "or") {
            return "||";
        }
        if (token == "adr" || token == "at") {
            return token;
        }
        return translate_value_identifier(token);
    }

    std::string emit_expr_words(const std::vector<std::string>& tokens) const {
        if (tokens.empty()) {
            return "";
        }
        if (tokens[0] == "adr" && tokens.size() == 2) {
            return "&" + emit_expr_words({tokens[1]});
        }
        if (tokens[0] == "at" && tokens.size() == 2) {
            return "*" + emit_expr_words({tokens[1]});
        }
        if (is_constructor_call(tokens)) {
            std::vector<std::string> args;
            for (size_t i = 1; i < tokens.size(); ++i) {
                args.push_back(emit_expr_words({tokens[i]}));
            }
            return cpp_type(program_, {tokens[0]}) + "{" + join(args, ", ") + "}";
        }
        if (is_call_like(tokens)) {
            std::vector<std::string> args;
            for (size_t i = 1; i < tokens.size(); ++i) {
                args.push_back(emit_expr_words({tokens[i]}));
            }
            return translate_callee(tokens[0]) + "(" + join(args, ", ") + ")";
        }

        std::vector<std::string> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            const std::string& token = tokens[i];
            if (token == "not" && i + 1 < tokens.size()) {
                out.push_back("!");
                continue;
            }
            if (token == "adr" && i + 1 < tokens.size()) {
                out.push_back("&");
                continue;
            }
            if (token == "at" && i + 1 < tokens.size()) {
                out.push_back("*");
                continue;
            }
            out.push_back(emit_atom(token));
        }
        return join(out, " ");
    }

    std::string emit_expr_text(const std::string& text) const {
        return emit_expr_words(split_words(text));
    }

    std::string emit_expr_node(const TreeLine& node) const {
        std::vector<std::string> tokens = split_words(node.line.text);
        if (node.children.empty()) {
            return emit_expr_words(tokens);
        }
        if (tokens.empty()) {
            fail("line " + std::to_string(node.line.number) + ": empty expression node");
        }
        std::vector<std::string> args;
        for (size_t i = 1; i < tokens.size(); ++i) {
            args.push_back(emit_expr_words({tokens[i]}));
        }
        for (const TreeLine& child : node.children) {
            args.push_back(emit_expr_node(child));
        }
        if (is_constructor_call(tokens)) {
            return cpp_type(program_, {tokens[0]}) + "{" + join(args, ", ") + "}";
        }
        return translate_callee(tokens[0]) + "(" + join(args, ", ") + ")";
    }

    std::string emit_expr_with_children(const std::vector<std::string>& head,
                                        const std::vector<TreeLine>& children,
                                        int line_number) const {
        if (children.empty()) {
            return emit_expr_words(head);
        }
        if (head.empty()) {
            if (children.size() != 1) {
                fail("line " + std::to_string(line_number) +
                     ": block expression without a head needs one child");
            }
            return emit_expr_node(children[0]);
        }
        TreeLine node;
        node.line.number = line_number;
        node.line.text = join(head, " ");
        node.children = children;
        return emit_expr_node(node);
    }

    bool parse_decl(const std::vector<std::string>& tokens, std::string& name,
                    std::vector<std::string>& type, std::vector<std::string>& value,
                    std::string& op) const {
        if (tokens.empty()) {
            return false;
        }
        size_t op_index = tokens.size();
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (is_assignment_operator(tokens[i])) {
                op_index = i;
                break;
            }
        }
        if (op_index == tokens.size() || op_index == 0) {
            return false;
        }

        if (tokens[0] == "con") {
            if (op_index < 3) {
                return false;
            }
            name = tokens[1];
            type = {tokens.begin() + 2, tokens.begin() + static_cast<std::ptrdiff_t>(op_index)};
            value = words_after(tokens, op_index + 1);
            op = "con";
            return true;
        }

        if (op_index > 1 && looks_like_type_name(tokens[1])) {
            name = tokens[0];
            type = {tokens.begin() + 1, tokens.begin() + static_cast<std::ptrdiff_t>(op_index)};
            value = words_after(tokens, op_index + 1);
            op = tokens[op_index];
            return true;
        }
        return false;
    }

    bool parse_uninitialized_decl(const std::vector<std::string>& tokens, std::string& name,
                                  std::vector<std::string>& type) const {
        if (tokens.size() < 2 || tokens[0] == "con") {
            return false;
        }
        if (!looks_like_type_name(tokens[1])) {
            return false;
        }
        name = tokens[0];
        type = words_after(tokens, 1);
        return true;
    }

    void emit_statement(std::ostringstream& out, const TreeLine& node, int depth, bool final_value,
                        const std::vector<TreeLine>* siblings = nullptr, size_t* index = nullptr) {
        const std::vector<std::string> tokens = split_words(node.line.text);
        if (tokens.empty()) {
            return;
        }

        if (tokens[0] == "if") {
            bool if_final = final_value;
            if (siblings != nullptr && index != nullptr) {
                size_t end = *index;
                while (end + 1 < siblings->size()) {
                    const std::vector<std::string> next_tokens =
                        split_words((*siblings)[end + 1].line.text);
                    if (next_tokens.empty() || next_tokens[0] != "else") {
                        break;
                    }
                    ++end;
                    if (next_tokens.size() == 1) {
                        break;
                    }
                }
                if_final = if_final || (siblings != nullptr && end + 1 == siblings->size());
            }
            emit_if(out, node, depth, if_final, siblings, index);
            return;
        }
        if (tokens[0] == "while") {
            out << indent(depth) << "while (" << emit_expr_words(words_after(tokens, 1)) << ") {\n";
            emit_block(out, node.children, depth + 1, false);
            out << indent(depth) << "}\n";
            return;
        }
        if (tokens[0] == "for") {
            emit_for(out, node, depth);
            return;
        }
        if (tokens[0] == "break" || tokens[0] == "continue") {
            out << indent(depth) << tokens[0] << ";\n";
            return;
        }
        if (tokens[0] == "ret") {
            out << indent(depth) << "return";
            if (tokens.size() > 1) {
                out << ' ' << emit_expr_words(words_after(tokens, 1));
            }
            out << ";\n";
            return;
        }

        std::string name;
        std::vector<std::string> type;
        std::vector<std::string> value;
        std::string op;
        if (parse_decl(tokens, name, type, value, op)) {
            const std::string expr =
                emit_expr_with_children(value, node.children, node.line.number);
            if (op == "con") {
                out << indent(depth) << "const " << cpp_type(program_, type) << ' ' << name << " = "
                    << expr << ";\n";
            } else {
                out << indent(depth) << cpp_type(program_, type) << ' ' << name << " = " << expr
                    << ";\n";
            }
            return;
        }

        if (parse_uninitialized_decl(tokens, name, type)) {
            out << indent(depth) << cpp_type(program_, type) << ' ' << name << "{};\n";
            return;
        }

        if (tokens.size() >= 3 && is_assignment_operator(tokens[1])) {
            const std::string expr =
                emit_expr_with_children(words_after(tokens, 2), node.children, node.line.number);
            out << indent(depth) << emit_expr_words({tokens[0]}) << ' ' << tokens[1] << ' ' << expr
                << ";\n";
            return;
        }

        const std::string expr = emit_expr_node(node);
        if (final_value) {
            out << indent(depth) << "return " << expr << ";\n";
        } else {
            out << indent(depth) << expr << ";\n";
        }
    }

    void emit_if(std::ostringstream& out, const TreeLine& node, int depth, bool final_value,
                 const std::vector<TreeLine>* siblings, size_t* index) {
        const std::vector<std::string> tokens = split_words(node.line.text);
        out << indent(depth) << "if (" << emit_expr_words(words_after(tokens, 1)) << ") {\n";
        emit_block(out, node.children, depth + 1, final_value);
        out << indent(depth) << "}";

        size_t cursor = index == nullptr ? 0 : *index;
        while (siblings != nullptr && index != nullptr && cursor + 1 < siblings->size()) {
            const TreeLine& next = (*siblings)[cursor + 1];
            const std::vector<std::string> next_tokens = split_words(next.line.text);
            if (!next_tokens.empty() && next_tokens[0] == "else") {
                if (next_tokens.size() >= 2 && next_tokens[1] == "if") {
                    out << " else if (" << emit_expr_words(words_after(next_tokens, 2)) << ") {\n";
                    emit_block(out, next.children, depth + 1, final_value);
                    out << indent(depth) << "}";
                    ++cursor;
                    continue;
                } else {
                    out << " else {\n";
                    emit_block(out, next.children, depth + 1, final_value);
                    out << indent(depth) << "}\n";
                    *index = cursor + 1;
                    return;
                }
            }
            break;
        }
        if (index != nullptr) {
            *index = cursor;
        }
        out << "\n";
    }

    void emit_for(std::ostringstream& out, const TreeLine& node, int depth) {
        const std::vector<std::string> tokens = split_words(node.line.text);
        if (tokens.size() >= 4 && tokens[2] == "in") {
            auto range = std::find(tokens.begin() + 3, tokens.end(), "..");
            if (range != tokens.end()) {
                const std::string start = emit_expr_words({tokens.begin() + 3, range});
                const std::string end = emit_expr_words({range + 1, tokens.end()});
                out << indent(depth) << "for (auto " << tokens[1] << " = " << start << "; "
                    << tokens[1] << " < " << end << "; ++" << tokens[1] << ") {\n";
            } else {
                out << indent(depth) << "for (auto&& " << tokens[1] << " : "
                    << emit_expr_words(words_after(tokens, 3)) << ") {\n";
            }
            emit_block(out, node.children, depth + 1, false);
            out << indent(depth) << "}\n";
            return;
        }
        fail("line " + std::to_string(node.line.number) + ": invalid for syntax");
    }

    void emit_block(std::ostringstream& out, const std::vector<TreeLine>& nodes, int depth,
                    bool final_return) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            const bool final_value = final_return && i + 1 == nodes.size();
            emit_statement(out, nodes[i], depth, final_value, &nodes, &i);
        }
    }

    void emit_function(std::ostringstream& out, const FunctionDecl& fn) {
        const std::string return_type = cpp_type(program_, fn.return_type);
        out << return_type << ' ' << fn.name << '(';
        std::vector<std::string> params;
        for (const ParamDecl& param : fn.params) {
            params.push_back(cpp_type(program_, param.type) + " " + param.name);
        }
        out << join(params, ", ") << ") {\n";
        emit_block(out, fn.body, 1, return_type != "void");
        out << "}\n";
    }
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--emit-cpp") {
            if (i + 1 >= argc) {
                fail("--emit-cpp requires a path or '-'");
            }
            options.emit_cpp = true;
            const std::string value = argv[++i];
            if (value != "-") {
                options.output = value;
            }
            continue;
        }
        if (arg == "--emit-header") {
            if (i + 1 >= argc) {
                fail("--emit-header requires a path or '-'");
            }
            const std::string value = argv[++i];
            if (value != "-") {
                options.header_output = value;
            } else {
                options.header_output = std::filesystem::path{};
            }
            continue;
        }
        if (arg == "--check") {
            options.check = true;
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            std::cout << "usage: dudu <input.dd> [--check] [--emit-header <path|->] "
                         "[--emit-cpp <path|->]\n";
            std::exit(0);
        }
        if (options.input.empty()) {
            options.input = arg;
            continue;
        }
        fail("unexpected argument: " + arg);
    }
    if (options.input.empty()) {
        fail("missing input file");
    }
    return options;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        fail("could not open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_text_output(const std::optional<std::filesystem::path>& path, const std::string& text) {
    if (!path.has_value() || path->empty()) {
        std::cout << text;
        return;
    }
    std::ofstream out(*path);
    if (!out) {
        fail("could not open output " + path->string());
    }
    out << text;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const std::string source = read_text_file(options.input);
        if (options.header_output.has_value()) {
            const dudu::ModuleAst module = dudu::parse_source(source, options.input);
            write_text_output(options.header_output, dudu::emit_cpp_header(module));
            return 0;
        }
        if (options.emit_cpp) {
            const dudu::ModuleAst module = dudu::parse_source(source, options.input);
            write_text_output(options.output, dudu::emit_cpp_source(module));
            return 0;
        }
        if (options.check) {
            (void)dudu::parse_source(source, options.input);
            return 0;
        }
        Program program = parse_program(options.input);
        Emitter emitter(std::move(program));
        const std::string cpp = emitter.emit();
        if (options.output.has_value()) {
            std::ofstream out(*options.output);
            if (!out) {
                fail("could not open output " + options.output->string());
            }
            out << cpp;
        } else {
            std::cout << cpp;
        }
    } catch (const std::exception& error) {
        std::cerr << "dudu: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
