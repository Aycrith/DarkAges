# PRP-001: Fix Unit Test Harness

**Status:** 🔴 Open  
**Priority:** P0 - Critical  
**Related PRD:** PRD-007 Testing  
**Estimate:** 2-4 hours

---

## Problem Statement
Unit tests fail with exit code 1. Test infrastructure works but the test harness cannot execute the server binary or tests are not linked correctly.

```
[FAIL] Unit tests failed
ERROR: Unit tests failed with code 1
```

---

## Proposed Solution

### Option A: Debug CMake Test Configuration
1. Check `src/server/CMakeLists.txt` test target definitions
2. Verify Catch2 is properly linked
3. Run `ctest --verbose` for detailed output

### Option B: Direct Binary Test
1. Run server with `--test` flag directly
2. Bypass TestRunner.py wrapper
3. Identify specific failing tests

---

## Acceptance Criteria
- [ ] Unit tests pass (exit code 0)
- [ ] TestRunner.py --tier=unit succeeds
- [ ] CI pipeline can run tests

---

## Tasks
- [TASK-001](../tasks/TASK-001-DEBUG-TESTS.md)
