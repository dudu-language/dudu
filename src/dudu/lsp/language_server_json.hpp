#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dudu {

struct Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::map<std::string, Json>;

struct Json {
    using Value = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

    Value value = nullptr;

    bool is_null() const;
    const std::string* string() const;
    const JsonArray* array() const;
    const JsonObject* object() const;
    const Json* get(std::string_view key) const;
};

class JsonParser {
  public:
    explicit JsonParser(std::string_view text);

    Json parse();

  private:
    std::string_view text_;
    size_t pos_ = 0;

    char peek() const;
    char get();
    void skip_ws();
    void consume(std::string_view token);
    Json parse_value();
    std::string parse_string();
    double parse_number();
    JsonArray parse_array();
    JsonObject parse_object();
};

std::string json_escape(std::string_view text);
std::string string_value(const Json* json);
int optional_int_value(const Json* json, int default_value = 0);
int required_int_value(const Json* json, std::string_view field_name);

} // namespace dudu
