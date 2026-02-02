# TASK-001: Debug Unit Test Failures

**Status:** 🔴 Open  
**Priority:** P0  
**Estimate:** 2 hours  
**Assignee:** TBD  
**PRP:** [PRP-001](../PRP/PRP-001-TEST-HARNESS-FIX.md)

---

## Description
Unit tests fail with exit code 1 when run via TestRunner.py. Need to identify root cause and fix.

## Steps
1. [ ] Run `ctest --verbose` in build directory
2. [ ] Check if server binary exists at expected path
3. [ ] Verify Catch2 test target is built
4. [ ] Run tests directly: `./darkages_server.exe --test`
5. [ ] Fix identified issues
6. [ ] Re-run TestRunner.py --tier=unit

## Acceptance Criteria
- Unit tests pass (0 failures)
- TestRunner.py reports success

## Notes
Last failure: 2026-02-02 04:22
Error: "Unit tests failed with code 1"
