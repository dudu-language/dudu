cmake_minimum_required(VERSION 3.20)

project(dudu LANGUAGES CXX)

option(DUDU_STRICT "Enable strict compiler warnings" ON)
option(DUDU_WARN_AS_ERROR "Treat warnings as errors" ON)

add_executable(dudu src/main.cpp)
target_compile_features(dudu PRIVATE cxx_std_20)

if(DUDU_STRICT)
    target_compile_options(dudu PRIVATE -Wall -Wextra -Wpedantic)
    if(DUDU_WARN_AS_ERROR)
        target_compile_options(dudu PRIVATE -Werror)
    endif()
endif()
