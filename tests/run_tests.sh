#!/bin/sh
# Run metacall-parser tests
# Usage: ./tests/run_tests.sh [path-to-cli-binary]
# Default: ./build/metacall-parser

set -e
PARSER="${1:-./build/metacall-parser}"
PASS=0
FAIL=0

# Paths
TD="tests"
SAMPLE_PY="$TD/sample.py"
SAMPLE_JS="$TD/sample.js"
SAMPLE_RB="$TD/sample.rb"
UTILS_RB="$TD/utils.rb"

check() {
    if eval "$2"; then
        echo "PASS: $1"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $1"
        FAIL=$((FAIL + 1))
    fi
}

run_parse() {
    $PARSER parse "$1" --format json 2>/dev/null
}

run_parse_format() {
    $PARSER parse "$1" --format "$2" 2>/dev/null
}

# Ensure we run from project root
cd "$(dirname "$0")/.."

echo "=== metacall-parser Test Suite ==="
echo ""

# Test 1: Python parse - symbols
echo "--- Python ---"
OUT=$(run_parse "$SAMPLE_PY")
check "Python finds greet"        'echo "$OUT" | grep -q "greet"'
check "Python finds add"          'echo "$OUT" | grep -q "\"name\":\"add\""'
check "Python finds Calculator"   'echo "$OUT" | grep -q "Calculator"'
check "Python finds DataProcessor" 'echo "$OUT" | grep -q "DataProcessor"'
check "Python finds utils import" 'echo "$OUT" | grep -q "\"module\":\"utils\""'

# Test 2: JS parse - symbols
echo "--- JavaScript ---"
OUT=$(run_parse "$SAMPLE_JS")
check "JS finds greet"            'echo "$OUT" | grep -q "greet"'
check "JS finds Calculator"       'echo "$OUT" | grep -q "Calculator"'
check "JS finds ./utils import"   'echo "$OUT" | grep -q "\"module\":\"\./utils\""'

# Test 3: Ruby parse - symbols and require_relative
echo "--- Ruby ---"
if [ -f "$SAMPLE_RB" ]; then
  OUT=$(run_parse "$SAMPLE_RB")
  check "Ruby parse runs"         'echo "$OUT" | grep -q "file"'
  check "Ruby finds greet"        'echo "$OUT" | grep -q "greet"'
  check "Ruby finds Calculator"   'echo "$OUT" | grep -q "Calculator"'
  check "Ruby finds utils import" 'echo "$OUT" | grep -q "\"module\":\"utils\""'
fi

# Test 4: Dependency graph - edges for all languages
echo "--- Dependency Graph ---"
OUT=$($PARSER deps "$TD/" 2>/dev/null)
check "Deps contains nodes"       'echo "$OUT" | grep -q "\"nodes\""'
check "Dep edge sample.py->utils.py" 'echo "$OUT" | grep -q "sample.py" && echo "$OUT" | grep -q "utils.py"'
check "Dep edge sample.js->utils.js" 'echo "$OUT" | grep -q "sample.js" && echo "$OUT" | grep -q "utils.js"'
if [ -f "$SAMPLE_RB" ] && [ -f "$UTILS_RB" ]; then
  check "Dep edge sample.rb->utils.rb" 'echo "$OUT" | grep -q "sample.rb" && echo "$OUT" | grep -q "utils.rb"'
fi

# Test 5: CLI --format options
echo "--- CLI ---"
check "Format text works"         'run_parse_format "$SAMPLE_PY" text | grep -q "File:"'
check "Format inspect works"      'run_parse_format "$SAMPLE_PY" inspect | grep -q "py"'

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
