#!/bin/sh
# Run MCP parser tests
# Usage: ./tests/run_tests.sh [path-to-cli-binary]
# Default: ./build/metacall-parser

set -e
MCP="${1:-./build/metacall-parser}"
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

echo "=== MCP Parser Test Suite ==="
echo ""

# Test 1: Python parse - symbols
echo "--- Python ---"
OUT=$($MCP parse tests/sample.py --format json 2>/dev/null)
check "Python finds greet"        'echo "$OUT" | grep -q "greet"'
check "Python finds add"          'echo "$OUT" | grep -q "\"name\":\"add\""'
check "Python finds Calculator"   'echo "$OUT" | grep -q "Calculator"'
check "Python finds DataProcessor" 'echo "$OUT" | grep -q "DataProcessor"'
check "Python finds utils import" 'echo "$OUT" | grep -q "\"module\":\"utils\""'

# Test 2: JS parse - symbols
echo "--- JavaScript ---"
OUT=$($MCP parse tests/sample.js --format json 2>/dev/null)
check "JS finds greet"            'echo "$OUT" | grep -q "greet"'
check "JS finds Calculator"       'echo "$OUT" | grep -q "Calculator"'
check "JS finds ./utils import"   'echo "$OUT" | grep -q "\"module\":\"\./utils\""'

# Test 3: Ruby parse - symbols and require_relative
echo "--- Ruby ---"
if [ -f tests/sample.rb ]; then
  OUT=$($MCP parse tests/sample.rb --format json 2>/dev/null)
  check "Ruby parse runs"         'echo "$OUT" | grep -q "file"'
  check "Ruby finds greet"        'echo "$OUT" | grep -q "greet"'
  check "Ruby finds Calculator"   'echo "$OUT" | grep -q "Calculator"'
  check "Ruby finds utils import" 'echo "$OUT" | grep -q "\"module\":\"utils\""'
fi

# Test 4: Dependency graph - edges for all languages
echo "--- Dependency Graph ---"
OUT=$($MCP deps tests/ 2>/dev/null)
check "Deps contains nodes"       'echo "$OUT" | grep -q "\"nodes\""'
check "Dep edge sample.py->utils.py" 'echo "$OUT" | grep -q "sample.py" && echo "$OUT" | grep -q "utils.py"'
check "Dep edge sample.js->utils.js" 'echo "$OUT" | grep -q "sample.js" && echo "$OUT" | grep -q "utils.js"'
if [ -f tests/sample.rb ] && [ -f tests/utils.rb ]; then
  check "Dep edge sample.rb->utils.rb" 'echo "$OUT" | grep -q "sample.rb" && echo "$OUT" | grep -q "utils.rb"'
fi

# Test 5: CLI --format options
echo "--- CLI ---"
check "Format text works"         '$MCP parse tests/sample.py --format text 2>/dev/null | grep -q "File:"'
check "Format inspect works"      '$MCP parse tests/sample.py --format inspect 2>/dev/null | grep -q "py"'

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
