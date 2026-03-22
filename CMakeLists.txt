cmake_minimum_required(VERSION 3.24)
project(metacall-parser VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Tree Sitter - fetch via CMake FetchContent (per mentor guidance)
# Populate only (no add_subdirectory) to avoid conflicting targets from grammar CMakeLists
include(FetchContent)
FetchContent_Declare(treesitter
  URL      https://github.com/tree-sitter/tree-sitter/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(treesitter_python
  URL      https://github.com/tree-sitter/tree-sitter-python/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(treesitter_javascript
  URL      https://github.com/tree-sitter/tree-sitter-javascript/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(treesitter_ruby
  URL      https://github.com/tree-sitter/tree-sitter-ruby/archive/refs/heads/master.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Populate(treesitter)
FetchContent_Populate(treesitter_python)
FetchContent_Populate(treesitter_javascript)
FetchContent_Populate(treesitter_ruby)

set(TS_CORE_DIR ${treesitter_SOURCE_DIR})
set(TS_PYTHON_DIR ${treesitter_python_SOURCE_DIR})
set(TS_JS_DIR ${treesitter_javascript_SOURCE_DIR})
set(TS_RUBY_DIR ${treesitter_ruby_SOURCE_DIR})
message(STATUS "Using Tree Sitter from FetchContent")

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

# metacall-parser library
add_library(metacall_parser STATIC
  src/parser.c
  src/extractors/python_extractor.c
  src/extractors/js_extractor.c
  src/extractors/ruby_extractor.c
  src/dependency_builder.c
  src/api.c
)

target_include_directories(metacall_parser PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${TS_CORE_DIR}/lib/include
  ${TS_PYTHON_DIR}/src
  ${TS_JS_DIR}/src
  ${TS_RUBY_DIR}/src
)

target_link_libraries(metacall_parser PRIVATE
  tree-sitter-lib
  tree-sitter-python-lib
  tree-sitter-javascript-lib
  tree-sitter-ruby-lib
)

# CLI executable
add_executable(metacall-parser cli/main.c)
target_link_libraries(metacall-parser PRIVATE metacall_parser)
target_include_directories(metacall-parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Install
install(TARGETS metacall-parser metacall_parser
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
install(FILES include/metacall_parser.h DESTINATION include)
