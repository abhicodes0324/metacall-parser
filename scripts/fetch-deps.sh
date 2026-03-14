#!/bin/sh
# Fetch Tree Sitter dependencies
# Run from mcp-prototype directory: ./scripts/fetch-deps.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR="${SCRIPT_DIR}/../vendor"
mkdir -p "$VENDOR"
cd "$VENDOR"

fetch() {
    local name="$1"
    local repo="$2"
    if [ -d "$name" ]; then
        if [ "$name" = "tree-sitter" ] && [ -f "$name/lib/src/lib.c" ]; then
            echo "$name already exists"
            return 0
        fi
        if [ "$name" != "tree-sitter" ] && [ -f "$name/src/parser.c" ]; then
            echo "$name already exists"
            return 0
        fi
    fi
    echo "Fetching $name..."
    rm -rf "$name" "${name}.tgz"
    curl -sL "https://github.com/${repo}/archive/refs/heads/master.tar.gz" -o "${name}.tgz"
    tar xzf "${name}.tgz"
    extracted=$(ls -d *-master 2>/dev/null | head -1)
    if [ -n "$extracted" ]; then
        mv "$extracted" "$name"
    fi
    rm -f "${name}.tgz"
}

fetch "tree-sitter" "tree-sitter/tree-sitter"
fetch "tree-sitter-python" "tree-sitter/tree-sitter-python"
fetch "tree-sitter-javascript" "tree-sitter/tree-sitter-javascript"

echo "Done. Run: mkdir -p build && cd build && cmake .. && cmake --build ."
