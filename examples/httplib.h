#pragma once

#include <string>
#include <vector>

namespace httplib {

struct Request {
    std::vector<std::string> matches;
};

struct Response {
    int status = 200;

    void set_content(const std::string& body, const char* content_type);
};

struct Server {
    template <typename Handler>
    void Get(const char* pattern, Handler handler);

    void listen(const char* host, int port);
};

} // namespace httplib
