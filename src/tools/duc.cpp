#include "dudu/frontend/cli_command.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        return dudu::run_cli(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "duc: " << error.what() << '\n';
        return 1;
    }
}
