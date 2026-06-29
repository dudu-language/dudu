#include "dudu/language_server.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        return dudu::run_language_server(std::cin, std::cout, std::cerr);
    } catch (const std::exception& error) {
        std::cerr << "dudu-lsp: " << error.what() << '\n';
        return 1;
    }
}
