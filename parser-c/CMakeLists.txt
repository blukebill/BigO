cmake_minimum_required(VERSION 3.16)
project(parser_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Jansson (JSON) and pthreads
find_library(JANSSON jansson)
find_package(Threads REQUIRED)

# -- tree-sitter core --
# Expect submodules in third_party/
#   third_party/tree-sitter
#   third_party/tree-sitter-c
#
# Add them with:
#   git submodule add https://github.com/tree-sitter/tree-sitter third_party/tree-sitter
#   git submodule add https://github.com/tree-sitter/tree-sitter-c third_party/tree-sitter-c
#   git submodule update --init --recursive

add_library(tree-sitter STATIC
  third_party/tree-sitter/lib/src/lib.c
)
target_include_directories(tree-sitter PUBLIC
  third_party/tree-sitter/lib/include
)

add_library(ts-c STATIC
  third_party/tree-sitter-c/src/parser.c
  third_party/tree-sitter-c/src/scanner.c
)
target_include_directories(ts-c PUBLIC third_party/tree-sitter-c/src)

# ---- Parser service binary ----
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
