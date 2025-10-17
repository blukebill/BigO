cmake_minimum_required(VERSION 3.16)
project(parser_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Locate dependencies
find_library(JANSSON jansson)
find_package(Threads REQUIRED)

# --- Tree-sitter core ---
add_library(tree-sitter STATIC
  third_party/tree-sitter/lib/src/lib.c
)
target_include_directories(tree-sitter PUBLIC
  third_party/tree-sitter/lib/include
)

# --- Tree-sitter C grammar (no scanner.c required) ---
add_library(ts-c STATIC
  third_party/tree-sitter-c/src/parser.c
)
target_include_directories(ts-c PUBLIC third_party/tree-sitter-c/src)

# --- Parser service executable ---
add_executable(parser
  main.c
  http.c
  json.c
  parse.c
)

target_include_directories(parser PRIVATE
  third_party/tree-sitter/lib/include
  third_party/tree-sitter-c/src
)

target_link_libraries(parser
  PRIVATE
    tree-sitter
    ts-c
    ${JANSSON}
    Threads::Threads
)

