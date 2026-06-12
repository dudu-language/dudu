#include "dudu/escapes.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/sema.hpp"

#include <cctype>

namespace dudu {
namespace {

bool is_borrowed_or_pointer(std::string type) {
    type = trim_copy(std::move(type));
    return !type.empty() && (type.front() == '&' || type.front() == '*');
}

void fail_if_value_local_escapes(const RawStmt& stmt,
                                 const std::map<std::string, std::string>& locals,
                                 const std::string& name) {
    const auto it = locals.find(name);
    if (it != locals.end() && !is_borrowed_or_pointer(it->second)) {
        throw CompileError(stmt.location, "cannot let local address escape: " + name);
    }
}

std::string address_name_after(const std::string& text, size_t ampersand) {
    size_t start = ampersand + 1;
    while (start < text.size() && text[start] == ' ') {
        ++start;
    }
    size_t end = start;
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_')) {
        ++end;
    }
    return text.substr(start, end - start);
}

} // namespace

void check_local_address_escape(const RawStmt& stmt,
                                const std::map<std::string, std::string>& locals) {
    const std::string text = trim_copy(stmt.text);
    if (starts_with(text, "return &")) {
        fail_if_value_local_escapes(stmt, locals, address_name_after(text, text.find('&')));
        return;
    }
    size_t pos = text.find(".append(&");
    while (pos != std::string::npos) {
        fail_if_value_local_escapes(stmt, locals, address_name_after(text, pos + 8));
        pos = text.find(".append(&", pos + 1);
    }
}

} // namespace dudu
