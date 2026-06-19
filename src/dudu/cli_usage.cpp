#include "dudu/cli_usage.hpp"

#include <iostream>

namespace dudu {

void print_cli_usage(bool project_driver) {
    if (project_driver) {
        std::cout << "usage: dudu init\n"
                     "       dudu init [path]\n"
                     "       dudu new <name>\n"
                     "       dudu run [input.dd|target] [-o output] [--quiet]\n"
                     "       dudu build [input.dd|target] [-o output] [--quiet]\n"
                     "       dudu bench [--quiet] [-- args...]\n"
                     "       dudu check [input.dd|dir]\n"
                     "       dudu clean [path]\n"
                     "       dudu clean-cache [path]\n"
                     "       dudu fmt [input.dd|dir] [--check]\n"
                     "       dudu lsp\n"
                     "       dudu test [input.dd|target|filter] [--filter text] [--no-capture] "
                     "[--quiet]\n"
                     "       dudu cmake [input.dd|target] [-o CMakeLists.txt]\n";
        return;
    }
    std::cout
        << "usage: duc bench [args...]\n"
           "       duc build [input.dd|target] [-o output]\n"
           "       duc check [input.dd]\n"
           "       duc clean [path]\n"
           "       duc clean-cache [input.dd|path]\n"
           "       duc cmake [input.dd|target] [-o CMakeLists.txt]\n"
           "       duc emit [input.dd] [-o output.cpp]\n"
           "       duc emit-modules [input.dd] -o output-dir\n"
           "       duc emit-test-modules [input.dd] -o output-dir [--filter text] "
           "[--no-capture]\n"
           "       duc fmt <input.dd|dir> [--check] [-o output.dd]\n"
           "       duc lsp\n"
           "       duc run [input.dd|target] [-o output] [--quiet]\n"
           "       duc test [input.dd|target|filter] [--filter text] [--no-capture] [--quiet]\n"
           "       duc <input.dd> [--check] [--format <path|->] "
           "[--emit-header <path|->] [--emit-c-header <path|->] "
           "[--emit-cpp <path|->] [-DNAME=value] [--verbose]\n";
}

void print_cli_version() {
    std::cout << "duc 0.1.0\n";
}

} // namespace dudu
