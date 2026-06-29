#include "dudu/frontend/cli_usage.hpp"

#include <iostream>

namespace dudu {

void print_cli_usage(bool project_driver) {
    if (project_driver) {
        std::cout << "usage: dudu init\n"
                     "       dudu init [path]\n"
                     "       dudu new <name>\n"
                     "       dudu run [input.dd|target] [--quiet] [--verbose] [--timings] "
                     "[-- args...]\n"
                     "       dudu build [input.dd|target] [--quiet] [--verbose] [--timings]\n"
                     "       dudu bench [compiler] [--quiet] [--verbose] [--timings] "
                     "[-- args...]\n"
                     "       dudu check [input.dd|dir] [--quiet] [--timings]\n"
                     "       dudu clean [path] [--quiet]\n"
                     "       dudu clean-cache [path] [--quiet]\n"
                     "       dudu fmt [input.dd|dir] [--check]\n"
                     "       dudu test [input.dd|target|filter] [--filter text] [--no-capture] "
                     "[--quiet] [--verbose] [--timings]\n"
                     "       dudu cmake [input.dd|target] [-o CMakeLists.txt] [--quiet] "
                     "[--timings]\n";
        return;
    }
    std::cout
        << "usage: duc bench [args...]\n"
           "       duc build [input.dd|target] [-o output] [--quiet] [--verbose]\n"
           "       duc check [input.dd] [--quiet]\n"
           "       duc clean [path] [--quiet]\n"
           "       duc clean-cache [input.dd|path] [--quiet]\n"
           "       duc cmake [input.dd|target] [-o CMakeLists.txt] [--quiet]\n"
           "       duc emit [input.dd] [-o output.cpp]\n"
           "       duc emit-modules [input.dd] -o output-dir [--timings]\n"
           "       duc emit-test-modules [input.dd] -o output-dir [--filter text] "
           "[--no-capture] [--timings]\n"
           "       duc fmt <input.dd|dir> [--check] [-o output.dd]\n"
           "       duc run [input.dd|target] [-o output] [--quiet] [--verbose] [-- args...]\n"
           "       duc test [input.dd|target|filter] [--filter text] [--no-capture] [--quiet] "
           "[--verbose]\n"
           "       duc <input.dd> [--check] [--format <path|->] "
           "[--emit-header <path|->] [--emit-c-header <path|->] "
           "[--emit-cpp <path|->] [-DNAME=value] [--verbose]\n";
}

void print_cli_version(bool project_driver) {
    std::cout << (project_driver ? "dudu" : "duc") << " 0.1.0\n";
}

} // namespace dudu
