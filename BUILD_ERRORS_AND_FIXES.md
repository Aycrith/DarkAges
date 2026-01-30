# Build Errors Analysis & Fixes

**Date**: 2026-01-29  
**Status**: Compilation in progress - Errors identified

---

## Summary

The first compilation attempt with GCC 13.2.0 has revealed several real C++ errors that need fixing. These are legitimate issues that our static analysis tools didn't catch.

---

## Errors Found

### 1. Missing `ConnectionID` Type ❌

**Files Affected:**
- `src/server/include/zones/AreaOfInterest.hpp`
- `src/server/include/zones/EntityMigration.hpp`
- `src/server/include/security/AntiCheat.hpp`

**Error:**
```
error: 'ConnectionID' has not been declared
```

**Fix:** Add type definition to `CoreTypes.hpp`:
```cpp
using ConnectionID = uint32_t;  // or appropriate type
```

---

### 2. Catch2 Header Mismatch ❌

**File:** `src/server/tests/TestCombatSystem.cpp`

**Error:**
```
fatal error: catch2/catch.hpp: No such file or directory
```

**Fix:** Update to Catch2 v3 header:
```cpp
#include <catch2/catch_test_macros.hpp>
```

---

### 3. C++20 Aggregate Initialization ❌

**File:** `src/server/include/security/DDoSProtection.hpp`

**Error:**
```
error: default member initializer for 'Config::maxConnectionsPerIP' 
required before the end of its enclosing class
```

**Cause:** GCC is stricter about C++20 aggregate initialization with default member initializers.

**Fix:** Either:
- Option A: Remove `= Config{}` default parameter
- Option B: Make Config non-aggregate by adding a constructor
- Option C: Move default values to constructor initializer list

---

### 4. const Method Modifying Members ❌

**File:** `src/server/src/memory/LeakDetector.cpp`

**Error:**
```
error: assignment of member 'LeakDetector::checked_' in read-only object
```

**Fix:** Remove `const` from method or make members `mutable`.

---

### 5. Malformed String Literal ❌

**File:** `src/server/src/profiling/PerformanceMonitor.cpp:160`

**Error:**
```
error: missing terminating " character
std::cout << "  Violations (>": " << ...
```

**Fix:** Correct the string literal syntax error.

---

### 6. Type Mismatch in std::clamp ❌

**File:** `src/server/src/security/AntiCheat.cpp:83`

**Error:**
```
error: no matching function for call to 'clamp(int16_t&, int, int)'
```

**Fix:** Use consistent types:
```cpp
std::clamp(static_cast<int>(newScore), 0, 100)
```

---

### 7. EntityID Type Mismatch in Tests ❌

**File:** `src/server/tests/TestMain.cpp`

**Error:**
```
error: no matching function for call to 'SpatialHash::insert(int, float, float)'
```

**Fix:** Cast integers to `EntityID` (which is `entt::entity`):
```cpp
hash.insert(static_cast<EntityID>(1), 5.0f, 5.0f);
```

---

## Fixes Applied

Let me now fix all these issues...
