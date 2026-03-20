cmake_minimum_required(VERSION 3.14)
project(mcp-prototype VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Tree Sitter - use vendor/ (populate via: ./scripts/fetch-deps.sh)
set(TS_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tree-sitter)
set(TS_PYTHON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tree-sitter-python)
set(TS_JS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tree-sitter-javascript)
set(TS_RUBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tree-sitter-ruby)

if(NOT EXISTS ${TS_CORE_DIR}/lib/src/lib.c)
  message(FATAL_ERROR "Tree Sitter not found. Run: ./scripts/fetch-deps.sh")
endif()
message(STATUS "Using Tree Sitter from vendor/")

# Tree-sitter core library (all required sources)
add_library(tree-sitter-lib STATIC
  ${TS_CORE_DIR}/lib/src/lib.c
  ${TS_CORE_DIR}/lib/src/parser.c
  ${TS_CORE_DIR}/lib/src/lexer.c
  ${TS_CORE_DIR}/lib/src/language.c
  ${TS_CORE_DIR}/lib/src/node.c
  ${TS_CORE_DIR}/lib/src/tree.c
  ${TS_CORE_DIR}/lib/src/subtree.c
  ${TS_CORE_DIR}/lib/src/stack.c
  ${TS_CORE_DIR}/lib/src/point.c
  ${TS_CORE_DIR}/lib/src/tree_cursor.c
  ${TS_CORE_DIR}/lib/src/query.c
  ${TS_CORE_DIR}/lib/src/get_changed_ranges.c
)
target_include_directories(tree-sitter-lib PUBLIC
  ${TS_CORE_DIR}/lib/include
  ${TS_CORE_DIR}/lib/src
)

# Tree-sitter Python
set(TS_PYTHON_SRCS ${TS_PYTHON_DIR}/src/parser.c)
if(EXISTS ${TS_PYTHON_DIR}/src/scanner.c)
  list(APPEND TS_PYTHON_SRCS ${TS_PYTHON_DIR}/src/scanner.c)
endif()
add_library(tree-sitter-python-lib STATIC ${TS_PYTHON_SRCS})
target_include_directories(tree-sitter-python-lib PUBLIC
  ${TS_CORE_DIR}/lib/include
  ${TS_PYTHON_DIR}/src
)

# Tree-sitter JavaScript
set(TS_JS_SRCS ${TS_JS_DIR}/src/parser.c)
if(EXISTS ${TS_JS_DIR}/src/scanner.c)
  list(APPEND TS_JS_SRCS ${TS_JS_DIR}/src/scanner.c)
endif()
add_library(tree-sitter-javascript-lib STATIC ${TS_JS_SRCS})
target_include_directories(tree-sitter-javascript-lib PUBLIC
  ${TS_CORE_DIR}/lib/include
  ${TS_JS_DIR}/src
)

# Tree-sitter Ruby
set(TS_RUBY_SRCS ${TS_RUBY_DIR}/src/parser.c)
if(EXISTS ${TS_RUBY_DIR}/src/scanner.c)
  list(APPEND TS_RUBY_SRCS ${TS_RUBY_DIR}/src/scanner.c)
endif()
add_library(tree-sitter-ruby-lib STATIC ${TS_RUBY_SRCS})
target_include_directories(tree-sitter-ruby-lib PUBLIC
  ${TS_CORE_DIR}/lib/include
  ${TS_RUBY_DIR}/src
)

# MCP Parser library
add_library(mcp_parser STATIC
  src/parser.c
  src/extractors/python_extractor.c
  src/extractors/js_extractor.c
  src/extractors/ruby_extractor.c
  src/dependency_builder.c
  src/api.c
)

target_include_directories(mcp_parser PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${TS_CORE_DIR}/lib/include
  ${TS_PYTHON_DIR}/src
  ${TS_JS_DIR}/src
  ${TS_RUBY_DIR}/src
)

target_link_libraries(mcp_parser PRIVATE
  tree-sitter-lib
  tree-sitter-python-lib
  tree-sitter-javascript-lib
  tree-sitter-ruby-lib
)

# CLI executable
add_executable(metacall-parser cli/main.c)
target_link_libraries(metacall-parser PRIVATE mcp_parser)
target_include_directories(metacall-parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Install
install(TARGETS metacall-parser mcp_parser
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
install(FILES include/mcp_parser.h DESTINATION include)
