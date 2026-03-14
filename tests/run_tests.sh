#!/bin/sh
# Run MCP parser tests
# Usage: ./tests/run_tests.sh [path-to-mcp-binary]
# Default: ./build/mcp

set -e
MCP="${1:-./build/mcp}"
PASS=0
FAIL=0

check() {
    if eval "$2"; then
        echo "PASS: $1"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $1"
        FAIL=$((FAIL + 1))
    fi
}

# Ensure we run from project root
cd "$(dirname "$0")/.."

# Test 1: Python parse finds greet and add
OUT=$($MCP parse tests/sample.py --format json 2>/dev/null)
check "Python finds greet"   'echo "$OUT" | grep -q "greet"'
check "Python finds add"     'echo "$OUT" | grep -q "\"name\":\"add\""'
check "Python finds Calculator" 'echo "$OUT" | grep -q "Calculator"'

# Test 2: JS parse finds greet
OUT=$($MCP parse tests/sample.js --format json 2>/dev/null)
check "JS finds greet"       'echo "$OUT" | grep -q "greet"'
check "JS finds Calculator"  'echo "$OUT" | grep -q "Calculator"'

# Test 2b: Ruby parse (if sample.rb exists) - smoke test
if [ -f tests/sample.rb ]; then
  OUT=$($MCP parse tests/sample.rb --format json 2>/dev/null)
  check "Ruby parse runs"    'echo "$OUT" | grep -q "file"'
fi

# Test 3: Dependency graph finds edge from sample.py to utils.py
OUT=$($MCP deps tests/ 2>/dev/null)
check "Dep edge sample->utils" 'echo "$OUT" | grep -q "utils"'

# Test 4: Dependency graph finds edge from sample.js to utils.js (relative import ./utils)
check "Dep edge sample.js->utils.js" 'echo "$OUT" | grep -q "sample.js" && echo "$OUT" | grep -q "utils.js"'

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
