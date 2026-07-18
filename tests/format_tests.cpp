#include "dudu/format/format.hpp"
#include "dudu/parser/parser.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void test_multiline_docstrings_are_canonical_and_idempotent() {
    const std::string source = "class Player:\n"
                               "\t''' Player docs.   \n"
                               "\t\n"
                               "\t\tKeeps two details.   \n"
                               "\t\n"
                               "\t\t\n"
                               "\t\tAnd keeps both blank lines.\n"
                               "\t'''\n"
                               "\thp: i32\n";
    const std::string expected = "class Player:\n"
                                 "    '''Player docs.\n"
                                 "\n"
                                 "    Keeps two details.\n"
                                 "\n"
                                 "\n"
                                 "    And keeps both blank lines.\n"
                                 "    '''\n"
                                 "    hp: i32\n";
    const std::string formatted = dudu::format_source(source);
    assert(formatted == expected);
    assert(dudu::format_source(formatted) == formatted);

    const dudu::ModuleAst module = dudu::parse_source(formatted, "docstrings.dd");
    assert(module.classes.front().doc_comment ==
           "Player docs.\n\nKeeps two details.\n\n\nAnd keeps both blank lines.");
}

void test_one_line_docstrings_stay_compact() {
    const std::string formatted = dudu::format_source("''' Module docs. '''   \n\n"
                                                      "def main():\n"
                                                      "\t''' Main docs. '''\n"
                                                      "\tpass\n");
    assert(formatted == "'''Module docs.'''\n\n"
                        "def main():\n"
                        "    '''Main docs.'''\n"
                        "    pass\n");
}

void test_runtime_triple_string_contents_are_preserved() {
    const std::string source = "def text() -> str:\n"
                               "\tvalue: str = '''first  \n"
                               "\t  literal indent\t\n"
                               "\n"
                               "last  '''   \n"
                               "\treturn value\n";
    const std::string expected = "def text() -> str:\n"
                                 "    value: str = '''first  \n"
                                 "\t  literal indent\t\n"
                                 "\n"
                                 "last  '''\n"
                                 "    return value\n";
    assert(dudu::format_source(source) == expected);
}

void test_unterminated_triple_string_contents_are_preserved() {
    const std::string source = "def text() -> str:\n"
                               "\tvalue: str = '''first  \n"
                               "\t  literal indent\t\n";
    const std::string expected = "def text() -> str:\n"
                                 "    value: str = '''first  \n"
                                 "\t  literal indent\t\n";
    assert(dudu::format_source(source) == expected);
}

void test_enum_variants_are_compact() {
    const std::string source = "enum Token:\n"
                               "    Eof\n"
                               "\n"
                               "    Ident:\n"
                               "        text: str\n"
                               "\n"
                               "    Pair:\n"
                               "        left: *Token\n"
                               "\n"
                               "        right: *Token\n"
                               "\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    return 0\n";
    const std::string expected = "enum Token:\n"
                                 "    Eof\n"
                                 "    Ident:\n"
                                 "        text: str\n"
                                 "    Pair:\n"
                                 "        left: *Token\n"
                                 "        right: *Token\n"
                                 "\n"
                                 "\n"
                                 "def main() -> i32:\n"
                                 "    return 0\n";
    const std::string formatted = dudu::format_source(source);
    assert(formatted == expected);
    assert(dudu::format_source(formatted) == formatted);
}

void test_macro_decorators_are_canonical_and_idempotent() {
    const std::string source = "class DebugOptions:\n"
                               "\tlabel: str = \"\"\n"
                               "\n"
                               "@macro(attributes = DebugOptions)\n"
                               "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
                               "\treturn ast.expansion()\n"
                               "\n"
                               "@derive(Debug,Json)\n"
                               "class Player:\n"
                               "\t@Json(rename = \"displayName\")\n"
                               "\tname: str\n"
                               "\t@Debug(label = \"health\")\n"
                               "\thp: i32\n";
    const std::string expected = "class DebugOptions:\n"
                                 "    label: str = \"\"\n"
                                 "\n"
                                 "@macro(attributes = DebugOptions)\n"
                                 "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
                                 "    return ast.expansion()\n"
                                 "\n"
                                 "@derive(Debug,Json)\n"
                                 "class Player:\n"
                                 "    @Json(rename = \"displayName\")\n"
                                 "    name: str\n"
                                 "    @Debug(label = \"health\")\n"
                                 "    hp: i32\n";
    const std::string formatted = dudu::format_source(source);
    assert(formatted == expected);
    assert(dudu::format_source(formatted) == formatted);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

void test_example_corpus_is_idempotent_and_parseable() {
    const std::filesystem::path examples = std::filesystem::path(DUDU_REPO_ROOT) / "examples";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(examples)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".dd") {
            continue;
        }
        const std::string formatted = dudu::format_source(read_file(entry.path()));
        if (dudu::format_source(formatted) != formatted) {
            throw std::runtime_error("formatter is not idempotent for " + entry.path().string());
        }
        try {
            (void)dudu::parse_source(formatted, entry.path());
        } catch (const std::exception& error) {
            throw std::runtime_error("formatted example does not parse: " + entry.path().string() +
                                     ": " + error.what());
        }
    }
}

} // namespace

int main() {
    test_multiline_docstrings_are_canonical_and_idempotent();
    test_one_line_docstrings_stay_compact();
    test_runtime_triple_string_contents_are_preserved();
    test_unterminated_triple_string_contents_are_preserved();
    test_enum_variants_are_compact();
    test_macro_decorators_are_canonical_and_idempotent();
    test_example_corpus_is_idempotent_and_parseable();
    std::cout << "format tests passed\n";
    return 0;
}
