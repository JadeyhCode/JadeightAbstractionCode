cmake_minimum_required(VERSION 3.22)
project(JadeightAbstractionCode CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# jasm：ISA v3 汇编器 / 反汇编器（main.cpp 与 jasm.cpp 内容相同，取其一）
# isa.hpp（ISA v3 定义）在 Jadeight2ReWrite 下
add_executable(jasm main.cpp)
target_include_directories(jasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Jadeight2ReWrite)
target_compile_options(jasm PRIVATE -Wall -Wextra -Wno-unused-parameter)
