#pragma once

#include <cstddef>
#include <iosfwd>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace dudu {

struct Json;

class LspTransport {
  public:
    LspTransport(std::istream& in, std::ostream& out);

    std::optional<std::string> read_message();
    void respond(const Json& id, const std::string& result);
    void respond_error(const Json& id, int code, const std::string& message);
    void notify(std::string_view method, const std::string& params);
    void request_client_refresh(std::string_view method);

  private:
    std::istream& in_;
    std::ostream& out_;
    std::mutex output_mutex_;
    size_t server_request_sequence_ = 0;

    void write_message(const std::string& body);
    void write_message_unlocked(const std::string& body);
    static std::string id_json(const Json& id);
};

} // namespace dudu
