cmake_minimum_required(VERSION 3.20)

project(user_cmake_tool LANGUAGES CXX)

add_executable(user_cmake_tool native_main.cpp)
target_compile_features(user_cmake_tool PRIVATE cxx_std_20)
