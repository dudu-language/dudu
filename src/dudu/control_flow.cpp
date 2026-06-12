#include "dudu/control_flow.hpp"

#include <cctype>

namespace dudu {
namespace {

std::string trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool branch_chain_guarantees_return(const std::vector<RawStmt>& body, size_t& index) {
    bool has_else = false;
    bool all_branches_return = block_guarantees_return(body[index].children);
    while (index + 1 < body.size()) {
        const std::string next = trim(body[index + 1].text);
        if (!starts_with(next, "elif ") && next != "else:") {
            break;
        }
        ++index;
        has_else = has_else || next == "else:";
        all_branches_return = all_branches_return && block_guarantees_return(body[index].children);
        if (has_else) {
            break;
        }
    }
    return has_else && all_branches_return;
}

} // namespace

bool block_guarantees_return(const std::vector<RawStmt>& body) {
    for (size_t i = 0; i < body.size(); ++i) {
        const std::string text = trim(body[i].text);
        if (starts_with(text, "return")) {
            return true;
        }
        if (starts_with(text, "if ") && branch_chain_guarantees_return(body, i)) {
            return true;
        }
    }
    return false;
}

} // namespace dudu
