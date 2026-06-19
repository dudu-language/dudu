#include "dudu/language_server_json.hpp"

#include <cctype>
#include <stdexcept>

namespace dudu {

bool Json::is_null() const {
    return std::holds_alternative<std::nullptr_t>(value);
}

const std::string* Json::string() const {
    return std::get_if<std::string>(&value);
}

const JsonArray* Json::array() const {
    return std::get_if<JsonArray>(&value);
}

const JsonObject* Json::object() const {
    return std::get_if<JsonObject>(&value);
}

const Json* Json::get(std::string_view key) const {
    const JsonObject* obj = object();
    if (obj == nullptr) {
        return nullptr;
    }
    const auto found = obj->find(std::string(key));
    return found == obj->end() ? nullptr : &found->second;
}

JsonParser::JsonParser(std::string_view text) : text_(text) {
}

Json JsonParser::parse() {
    skip_ws();
    return parse_value();
}

char JsonParser::peek() const {
    return pos_ < text_.size() ? text_[pos_] : '\0';
}

char JsonParser::get() {
    return pos_ < text_.size() ? text_[pos_++] : '\0';
}

void JsonParser::skip_ws() {
    while (std::isspace(static_cast<unsigned char>(peek())) != 0) {
        ++pos_;
    }
}

void JsonParser::consume(std::string_view token) {
    if (text_.substr(pos_, token.size()) != token) {
        throw std::runtime_error("invalid json");
    }
    pos_ += token.size();
}

Json JsonParser::parse_value() {
    skip_ws();
    const char c = peek();
    if (c == '"') {
        return Json{parse_string()};
    }
    if (c == '{') {
        return Json{parse_object()};
    }
    if (c == '[') {
        return Json{parse_array()};
    }
    if (c == 't') {
        consume("true");
        return Json{true};
    }
    if (c == 'f') {
        consume("false");
        return Json{false};
    }
    if (c == 'n') {
        consume("null");
        return Json{nullptr};
    }
    return Json{parse_number()};
}

std::string JsonParser::parse_string() {
    if (get() != '"') {
        throw std::runtime_error("expected string");
    }
    std::string out;
    while (peek() != '\0') {
        const char c = get();
        if (c == '"') {
            return out;
        }
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        const char esc = get();
        switch (esc) {
        case '"':
        case '\\':
        case '/':
            out.push_back(esc);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u':
            out += "?";
            pos_ += 4;
            break;
        default:
            throw std::runtime_error("invalid escape");
        }
    }
    throw std::runtime_error("unterminated string");
}

double JsonParser::parse_number() {
    const size_t start = pos_;
    if (peek() == '-') {
        ++pos_;
    }
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        ++pos_;
    }
    if (peek() == '.') {
        ++pos_;
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            ++pos_;
        }
    }
    return std::stod(std::string(text_.substr(start, pos_ - start)));
}

JsonArray JsonParser::parse_array() {
    JsonArray out;
    get();
    skip_ws();
    if (peek() == ']') {
        get();
        return out;
    }
    while (true) {
        out.push_back(parse_value());
        skip_ws();
        const char c = get();
        if (c == ']') {
            return out;
        }
        if (c != ',') {
            throw std::runtime_error("expected array comma");
        }
    }
}

JsonObject JsonParser::parse_object() {
    JsonObject out;
    get();
    skip_ws();
    if (peek() == '}') {
        get();
        return out;
    }
    while (true) {
        skip_ws();
        std::string key = parse_string();
        skip_ws();
        if (get() != ':') {
            throw std::runtime_error("expected object colon");
        }
        out.emplace(std::move(key), parse_value());
        skip_ws();
        const char c = get();
        if (c == '}') {
            return out;
        }
        if (c != ',') {
            throw std::runtime_error("expected object comma");
        }
    }
}

std::string json_escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string string_value(const Json* json) {
    if (json == nullptr) {
        return {};
    }
    const std::string* text = json->string();
    return text == nullptr ? std::string{} : *text;
}

int optional_int_value(const Json* json, int default_value) {
    if (json == nullptr) {
        return default_value;
    }
    if (const double* number = std::get_if<double>(&json->value)) {
        return static_cast<int>(*number);
    }
    return default_value;
}

int required_int_value(const Json* json, std::string_view field_name) {
    if (const double* number = json == nullptr ? nullptr : std::get_if<double>(&json->value)) {
        return static_cast<int>(*number);
    }
    throw std::runtime_error("missing or invalid required LSP integer field: " +
                             std::string(field_name));
}

} // namespace dudu
