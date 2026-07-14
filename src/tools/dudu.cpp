#include "dudu/core/source.hpp"
#include "dudu/frontend/cli_command.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        return dudu::run_cli(argc, argv);
    } catch (const dudu::CompileError& error) {
        std::cerr << "dudu: " << error.what() << '\n';
        for (const dudu::CompileNote& note : error.notes())
            std::cerr << "  note: " << dudu::format_location(note.location) << ": " << note.message
                      << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "dudu: " << error.what() << '\n';
        return 1;
    }
}
