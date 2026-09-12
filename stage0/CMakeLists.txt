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
    src/parser.cpp
    src/codegen.cpp
    src/typecheck.cpp
)
target_include_directories(lgc PRIVATE src)
target_compile_options(lgc PRIVATE -Wall -Wextra)

add_executable(lpm
    src/lpm_main.cpp
    src/lexer.cpp
    src/config_parser.cpp
)
target_include_directories(lpm PRIVATE src)
target_compile_options(lpm PRIVATE -Wall -Wextra)

# Runtime dolaczany przez kod wygenerowany przez lgc (`#include "lg_runtime.hpp"`).
# lpm kopiuje go do build/target/ przy kompilacji projektu uzytkownika.
install(FILES runtime/lg_runtime.hpp DESTINATION share/legendary-lang/runtime)

