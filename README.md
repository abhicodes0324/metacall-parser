# metacall-parser - Multi-Language Parser for MetaCall

A cross-platform C/C++ tool and library for parsing multi-language projects using [Tree Sitter](https://tree-sitter.github.io/tree-sitter/). Extracts functions, classes, and builds dependency trees without runtime execution.

Owned and maintained by [abhicodes0324](https://github.com/abhicodes0324).

**GSoC Project:** Implement Multi-Language Parser

## Features

- **Static analysis** - Parse source files without executing code
- **Multi-language** - Python, JavaScript/TypeScript, and Ruby (extensible to more)
- **Symbol extraction** - Functions, classes, and methods
- **Import/require detection** - Build dependency graphs
- **MetaCall inspect format** - Optional output compatible with `metacall_inspect` (per file)
- **C API** - Embeddable in other projects
- **CLI** - Command-line interface for quick analysis

## Build

### Prerequisites

- CMake 3.14+
- C11 compiler (GCC, Clang, MSVC)
- Git (for fetching dependencies)

### Build Steps

```bash
# 1. Fetch dependencies (requires curl, run once)
./scripts/fetch-deps.sh

# 2. Build (from project root)
mkdir build && cd build
cmake ..
cmake --build .
```

### Install

Run from the project root:

```bash
# Option A: Install to /usr/local (requires sudo)
cd ..
sudo cmake --build build --target install

# Option B: Install to user directory (no sudo)
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build --target install
# Add to PATH if needed: export PATH="$HOME/.local/bin:$PATH"
```

After install, the `metacall-parser` binary is available in your PATH. To run without installing, use `./build/metacall-parser` from the project root (paths are relative to your current directory).

## Usage

### CLI

```bash
# From project root (after install, or use ./build/metacall-parser without install)
metacall-parser parse tests/sample.py

# Parse and show human-readable output
metacall-parser parse tests/sample.py --format text

# Parse and show MetaCall inspect-compatible JSON
metacall-parser parse tests/sample.py --format inspect

# List functions and classes
metacall-parser list-functions tests/sample.js

# Build dependency graph for a directory
metacall-parser deps tests/
```

When running from the `build/` directory, use relative paths like `../tests/sample.py`.

### C API Example

```c
#include "mcp_parser.h"

int main(void) {
    mcp_parser *parser = mcp_parser_create();
    mcp_result *result = mcp_parser_parse_file(parser, "script.py", NULL, 0);

    if (result) {
        const mcp_file_result *fr = mcp_result_get_file(result);
        for (size_t i = 0; i < fr->symbol_count; i++) {
            const mcp_symbol *s = &fr->symbols[i];
            if (s->type == MCP_SYMBOL_FUNCTION || s->type == MCP_SYMBOL_METHOD) {
                printf("%s%s: %s(",
                       s->is_async ? "async " : "",
                       s->type == MCP_SYMBOL_METHOD ? "method" : "function",
                       s->name);
                for (size_t p = 0; p < s->param_count; p++) {
                    if (p > 0) printf(", ");
                    printf("%s", s->param_names[p]);
                }
                printf(") [line %u]\n", s->line);
            } else if (s->type == MCP_SYMBOL_CLASS) {
                printf("class: %s [line %u]\n", s->name, s->line);
            }
        }
        mcp_result_free(result);
    }

    mcp_parser_destroy(parser);
    return 0;
}
```

## Output Format

### Parse Result (JSON)

```json
{
  "file": "sample.py",
  "language": "python",
  "functions": [
    {
      "name": "greet",
      "line": 7,
      "async": false,
      "params": [{ "name": "name" }]
    },
    {
      "name": "add",
      "line": 11,
      "async": false,
      "params": [{ "name": "a" }, { "name": "b" }]
    }
  ],
  "classes": [
    { "name": "Calculator", "line": 14 },
    { "name": "DataProcessor", "line": 21 }
  ],
  "imports": [
    {"module": "os"},
    {"module": "sys"},
    {"module": "utils"}
  ]
}
```

### Dependency Graph (JSON)

```json
{
  "nodes": [
    {"id": "/path/to/main.py", "path": "...", "symbols": [...]},
    {"id": "/path/to/utils.py", "path": "...", "symbols": [...]}
  ],
  "edges": [
    {"from": "/path/to/main.py", "to": "/path/to/utils.py"}
  ]
}
```

## Sample Output

### Python — JSON and Text

```bash
$ metacall-parser parse tests/sample.py
{"file":"tests/sample.py","language":"python","functions":[{"name":"greet","line":7,"async":false,"params":[{"name":"name"}]},{"name":"add","line":11,"async":false,"params":[{"name":"a"},{"name":"b"}]},{"name":"add","line":15,"async":false,"params":[{"name":"x"},{"name":"y"}]},{"name":"multiply","line":18,"async":false,"params":[{"name":"x"},{"name":"y"}]},{"name":"process","line":22,"async":false,"params":[{"name":"data"}]}],"classes":[{"name":"Calculator","line":14},{"name":"DataProcessor","line":21}],"imports":[{"module":"utils"}]}

$ metacall-parser parse tests/sample.py --format text
File: tests/sample.py
Language: python

Functions:
  - greet (line 7)
  - add (line 11)
  - add (line 15) [Calculator]
  - multiply (line 18) [Calculator]
  - process (line 22) [DataProcessor]

Classes:
  - Calculator (line 14)
  - DataProcessor (line 21)

Imports:
  - utils
```

### Python — MetaCall Inspect Format

```bash
$ metacall-parser parse tests/sample.py --format inspect
{"py":[{"name":"tests/sample.py","scope":{"name":"sample","funcs":[{"name":"greet","async":false,"signature":{"ret":{"type":"Unknown"},"args":[{"name":"name","type":"Unknown"}]}},{"name":"add","async":false,"signature":{"ret":{"type":"Unknown"},"args":[{"name":"a","type":"Unknown"},{"name":"b","type":"Unknown"}]}},{"name":"add","async":false,"signature":{"ret":{"type":"Unknown"},"args":[{"name":"x","type":"Unknown"},{"name":"y","type":"Unknown"}]}},{"name":"multiply","async":false,"signature":{"ret":{"type":"Unknown"},"args":[{"name":"x","type":"Unknown"},{"name":"y","type":"Unknown"}]}},{"name":"process","async":false,"signature":{"ret":{"type":"Unknown"},"args":[{"name":"data","type":"Unknown"}]}}],"classes":[{"name":"Calculator"},{"name":"DataProcessor"}],"objects":[]}}]}
```

### JavaScript — Functions and Imports

```bash
$ metacall-parser list-functions tests/sample.js
File: tests/sample.js
Language: javascript

Functions:
  - greet (line 6)
  - multiply (line 14)
  - add (line 17) [Calculator]
  - multiply (line 21) [Calculator]
  - process (line 27) [DataProcessor]

Classes:
  - Calculator (line 16)
  - DataProcessor (line 26)

Imports:
  - fs
  - ./utils
```

### Cross-Language Dependency Graph

```bash
$ metacall-parser deps tests/
{"nodes":[{"id":"tests/utils.py","path":"tests/utils.py","symbols":[{"name":"helper","type":"function","line":3}]},{"id":"tests/sample.py","path":"tests/sample.py","symbols":[{"name":"greet","type":"function","line":7},{"name":"add","type":"function","line":11},{"name":"Calculator","type":"class","line":14},{"name":"add","type":"method","line":15},{"name":"multiply","type":"method","line":18},{"name":"DataProcessor","type":"class","line":21},{"name":"process","type":"method","line":22}]},{"id":"tests/utils.js","path":"tests/utils.js","symbols":[{"name":"helper","type":"function","line":3}]},{"id":"tests/sample.js","path":"tests/sample.js","symbols":[{"name":"greet","type":"function","line":6},{"name":"multiply","type":"function","line":14},{"name":"Calculator","type":"class","line":16},{"name":"add","type":"method","line":17},{"name":"multiply","type":"method","line":21},{"name":"DataProcessor","type":"class","line":26},{"name":"process","type":"method","line":27}]}],"edges":[{"from":"tests/sample.py","to":"tests/utils.py"},{"from":"tests/sample.js","to":"tests/utils.js"}]}
```

## Project Structure

```
metacall-parser/
├── include/
│   └── mcp_parser.h      # Public C API
├── src/
│   ├── parser.c          # Core parsing logic
│   ├── api.c             # JSON export helpers
│   ├── dependency_builder.c
│   └── extractors/
│       ├── python_extractor.c
│       ├── js_extractor.c
│       └── ruby_extractor.c
├── cli/
│   └── main.c
├── tests/
│   ├── sample.py
│   ├── sample.js
│   ├── sample.rb
│   ├── utils.py
│   ├── utils.js
│   └── utils.rb
├── scripts/
│   └── fetch-deps.sh
└── CMakeLists.txt
```

## Testing

From the project root:

```bash
./tests/run_tests.sh ./build/metacall-parser
```

The test suite covers:

- **Python** — symbol extraction (functions, classes, methods), import detection
- **JavaScript** — symbol extraction, relative imports (`./utils`)
- **Ruby** — symbol extraction, `require_relative` detection
- **Dependency graph** — edges `sample.py` → `utils.py`, `sample.js` → `utils.js`, `sample.rb` → `utils.rb`
- **CLI** — `--format text`, `--format inspect`

## Extending to New Languages

The parser is designed for easy extension. MetaCall supports Python, JavaScript, TypeScript, Ruby, C#, Go, Java, Rust, C, and more. To add a new language:

1. Add Tree Sitter grammar as FetchContent dependency
2. Create `src/extractors/<lang>_extractor.c` implementing extraction logic
3. Register in `mcp_lang_from_path()` and the `LANGUAGES[]` table in `parser.c`
4. Add the file extension to `is_source_file()` in `dependency_builder.c` so `metacall-parser deps` includes it
5. Add query/traversal for function, class, and import nodes

## Use Cases

- **Intellisense** - Feed parsed symbols to IDE plugins (e.g., VS Code)
- **Function Mesh** - Break multi-language projects into distributable subparts
- **Documentation** - Generate API docs from static analysis
- **Refactoring** - Understand cross-file dependencies

## Resources

- [Tree Sitter Documentation](https://tree-sitter.github.io/tree-sitter/)
- [Tree Sitter C API](https://github.com/tree-sitter/tree-sitter/blob/master/lib/include/tree_sitter/api.h)
- [Tree Sitter Tutorial](https://dev.to/shrsv/making-sense-of-tree-sitters-c-api-2318)
