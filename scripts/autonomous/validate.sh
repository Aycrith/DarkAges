#!/usr/bin/env bash
# =============================================================================
# DarkAges Quick Validator — lightweight checks that don't need a full build
# =============================================================================
set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PASS=0
FAIL=0
WARN=0

check_pass() { echo "  PASS: $1"; PASS=$((PASS+1)); }
check_fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }
check_warn() { echo "  WARN: $1"; WARN=$((WARN+1)); }

echo "=== DarkAges Quick Validation ==="
echo ""

# 1. C++ syntax check (no full compile)
echo "--- Syntax & Structure ---"
src_count=$(find "$PROJECT_ROOT/src/server/src" -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | wc -l)
if [ "$src_count" -gt 0 ]; then
    check_pass "$src_count C++ source files found"
else
    check_fail "No C++ source files found"
fi

# 2. Header guard check
echo ""
echo "--- Header Quality ---"
for h in $(find "$PROJECT_ROOT/src" -name "*.hpp" -o -name "*.h" 2>/dev/null | head -20); do
    if grep -q "#pragma once\|#ifndef.*_H\|GUARD" "$h" 2>/dev/null; then
        : # has guard
    else
        check_warn "$(basename $h): missing header guard"
    fi
done

# 3. CMakeLists integrity
echo ""
echo "--- Build System ---"
if [ -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
    check_pass "Root CMakeLists.txt exists"
    if grep -q "project(" "$PROJECT_ROOT/CMakeLists.txt"; then
        check_pass "CMake project() defined"
    fi
    if grep -q "CMAKE_CXX_STANDARD" "$PROJECT_ROOT/CMakeLists.txt"; then
        check_pass "C++ standard specified"
    fi
else
    check_fail "No root CMakeLists.txt"
fi

# 4. Test file presence
echo ""
echo "--- Test Coverage ---"
test_count=$(find "$PROJECT_ROOT/tests" "$PROJECT_ROOT/src/server/tests" -name "*.cpp" 2>/dev/null | wc -l)
if [ "$test_count" -gt 5 ]; then
    check_pass "$test_count test files"
else
    check_warn "Only $test_count test files (consider more coverage)"
fi

# 5. Check for stub files (partial implementations)
echo ""
echo "--- Stub Detection ---"
stub_count=$(find "$PROJECT_ROOT/src" -name "*stub*" 2>/dev/null | wc -l)
if [ "$stub_count" -gt 0 ]; then
    check_warn "$stub_count stub files (may need real implementations)"
    find "$PROJECT_ROOT/src" -name "*stub*" 2>/dev/null | sed 's|.*/||' | while read f; do
        echo "    - $f"
    done
fi

# 6. TODO/FIXME inventory
echo ""
echo "--- TODO/FIXME ---"
    todo_count=$(grep -rn "TODO\|FIXME\|HACK\|XXX" "$PROJECT_ROOT/src/" --include="*.cpp" --include="*.hpp" --include="*.h" 2>/dev/null | wc -l || echo "0")
if [ "$todo_count" -eq 0 ]; then
    check_pass "No TODO/FIXME markers"
else
    check_warn "$todo_count TODO/FIXME markers in source code"
    grep -rn "TODO\|FIXME\|HACK\|XXX" "$PROJECT_ROOT/src/" --include="*.cpp" --include="*.hpp" --include="*.h" 2>/dev/null | head -10 | sed 's/^/    /'
fi

# 7. Documentation
echo ""
echo "--- Documentation ---"
doc_count=$(find "$PROJECT_ROOT/docs" -name "*.md" 2>/dev/null | wc -l)
if [ "$doc_count" -gt 5 ]; then
    check_pass "$doc_count documentation files"
else
    check_warn "Only $doc_count doc files"
fi

# Summary
echo ""
echo "=== Summary ==="
echo "  PASS: $PASS | FAIL: $FAIL | WARN: $WARN"
if [ "$FAIL" -gt 0 ]; then
    echo "  Status: NEEDS ATTENTION"
    exit 1
else
    echo "  Status: HEALTHY"
    exit 0
fi
