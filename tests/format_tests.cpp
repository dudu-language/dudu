#include "dudu/format/format.hpp"
#include "dudu/parser/parser.hpp"

#include <cassert>
#include <iostream>
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

} // namespace

int main() {
    test_multiline_docstrings_are_canonical_and_idempotent();
    test_one_line_docstrings_stay_compact();
    test_runtime_triple_string_contents_are_preserved();
    test_unterminated_triple_string_contents_are_preserved();
    std::cout << "format tests passed\n";
    return 0;
}
