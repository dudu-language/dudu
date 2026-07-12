#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_text_sync.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

dudu::JsonArray changes(std::string_view json) {
    const dudu::Json parsed = dudu::JsonParser(json).parse();
    assert(parsed.array() != nullptr);
    return *parsed.array();
}

void test_full_document_change() {
    std::string text = "old";
    dudu::apply_lsp_content_changes(text, changes(R"([{"text":"new document"}])"));
    assert(text == "new document");
}

void test_incremental_changes_are_sequential() {
    std::string text = "for i in range(3):\n";
    dudu::apply_lsp_content_changes(text, changes(R"([
            {"range":{"start":{"line":0,"character":4},"end":{"line":0,"character":5}},"text":"_"},
            {"range":{"start":{"line":0,"character":5},"end":{"line":0,"character":5}},"text":": i32"}
        ])"));
    assert(text == "for _: i32 in range(3):\n");
}

void test_positions_use_utf16_code_units() {
    std::string text = "label = \"😀\"; value\n";
    dudu::apply_lsp_content_changes(
        text,
        changes(
            R"([{"range":{"start":{"line":0,"character":14},"end":{"line":0,"character":19}},"text":"result"}])"));
    assert(text == "label = \"😀\"; result\n");
}

void test_invalid_range_does_not_partially_update_document() {
    std::string text = "abc\n";
    bool failed = false;
    try {
        dudu::apply_lsp_content_changes(text, changes(R"([
                {"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}},"text":"x"},
                {"range":{"start":{"line":9,"character":0},"end":{"line":9,"character":1}},"text":"y"}
            ])"));
    } catch (const std::exception&) {
        failed = true;
    }
    assert(failed);
    assert(text == "abc\n");
}

} // namespace

int main() {
    test_full_document_change();
    test_incremental_changes_are_sequential();
    test_positions_use_utf16_code_units();
    test_invalid_range_does_not_partially_update_document();
    std::cout << "language server text sync tests passed\n";
    return 0;
}
