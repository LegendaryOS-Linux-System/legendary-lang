cmake_minimum_required(VERSION 3.20)
project(legendary_lang_stage0 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(lgc
    src/main.cpp
    src/lexer.cpp
)

target_include_directories(lgc PRIVATE src)
target_compile_options(lgc PRIVATE -Wall -Wextra)

# Docelowo: lpm (Legendary Package Manager) jako osobny target w tym samym repo,
# na razie skupiamy się na `lgc` (transpiler .lg -> C++20).
