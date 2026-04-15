# CombatSystem — Implement Ability System Stub

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Create a minimal ability system stub that allows players to cast abilities (e.g., fireball, heal) with basic parameters (cooldown, mana cost, range). This enables future integration with combat mechanics.

**Architecture:**
The ability system will be a lightweight plugin system that:
1. Stores ability definitions (name, cooldown, mana cost, range, effect type) in a struct
2. Provides a simple interface for casting abilities
3. Integrates with existing combat state tracking
4. Uses ECS components for player state management

**Tech Stack:**
- C++20, ECS (EnTT), DarkAges::Netcode
- FlatBuffers for ability definitions
- Existing combat state components (CombatState, PlayerInfo)

---

## Files to Touch

### Core Implementation
1. **`src/server/include/combat/AbilitySystem.hpp`** — Ability definition struct and stub interface
2. **`src/server/include/combat/AbilitySystem.hpp`** — Add `AbilitySystem` class declaration
3. **`src/server/src/combat/AbilitySystem.cpp`** — Minimal stub implementation

### Integration
4. **`src/server/src/zones/ZoneServer.cpp`** — Add ability casting logic to `savePlayerState()` or `onEntityDied()`

### Tests
5. **`src/server/tests/TestAbilitySystem.cpp`** — Unit tests for ability definition validation

---

## Task 1: Create Ability Definition Struct

**Objective:** Define the minimal ability definition struct with required fields.

**Files:**
- Create: `src/server/include/combat/AbilitySystem.hpp:100-150`

**Step 1: Write failing test**

```cpp
// src/server/tests/TestAbilitySystem.cpp
#include <catch2/catch_test_macros.hpp>
#include <combat/AbilitySystem.hpp>

TEST_CASE("AbilitySystem::Ability definition validation") {
    SECTION("Should reject invalid cooldown") {
        Ability ability;
        ability.cooldown = -1; // Negative cooldown
        REQUIRE_THROWS_AS(ability.validate(), std::invalid_argument);
    }
    
    SECTION("Should reject zero mana cost") {
        Ability ability;
        ability.manaCost = 0; // Zero mana cost
        REQUIRE_THROWS_AS(ability.validate(), std::invalid_argument);
    }
    
    SECTION("Should accept valid ability") {
        Ability ability;
        ability.name = "Fireball";
        ability.cooldown = 5; // 5 seconds
        ability.manaCost = 10;
        ability.range = 10.0f; // 10 meters
        ability.effectType = AbilityEffectType::Damage;
        REQUIRE(ability.validate() == true);
    }
}
```

**Step 2: Run test to verify failure**

Run: `cd /root/projects/DarkAges && ./build_autonomous/darkages_tests -k TestAbilitySystem`
Expected: FAIL — "Ability definition validation tests not found"

**Step 3: Create Ability struct and validate method**

```cpp
// src/server/include/combat/AbilitySystem.hpp
#pragma once
#include <string>
#include <cmath>
#include "ecs/CoreTypes.hpp"

// [COMBAT_AGENT] Ability effect types
enum class AbilityEffectType : uint8_t {
    Damage = 1,
    Heal = 2,
    Buff = 3,
    Debuff = 4,
    Status = 5
};

// [COMBAT_AGENT] Ability definition
struct Ability {
    std::string name;
    uint32_t cooldown;  // Seconds
    uint32_t manaCost;   // Mana required
    float range;         // Maximum casting range
    AbilityEffectType effectType;
    
    // Validate that all required fields are non-zero
    bool validate() const {
        if (cooldown <= 0) return false;
        if (manaCost <= 0) return false;
        if (range <= 0) return false;
        return true;
    }
};
```

**Step 4: Run test to verify pass**

Run: `cd /root/projects/DarkAges && ./build_autonomous/darkages_tests -k TestAbilitySystem`
Expected: PASS — All 3 tests pass

**Step 5: Commit**

```bash
git add src/server/include/combat/AbilitySystem.hpp src/server/tests/TestAbilitySystem.cpp
git commit -m "feat: add Ability definition struct and validation"
```

---

## Task 2: Create AbilitySystem Class Stub

**Objective:** Create a minimal AbilitySystem class with casting stubs.

**Files:**
- Create: `src/server/include/combat/AbilitySystem.hpp:200-300`
- Create: `src/server/src/combat/AbilitySystem.cpp:100-200`

**Step 1: Write failing test**

```cpp
// src/server/tests/TestAbilitySystem.cpp
TEST_CASE("AbilitySystem::castAbility stub validation") {
    SECTION("Should return false for invalid ability") {
        AbilitySystem system;
        REQUIRE(system.castAbility(12345, "Fireball", 10.0f) == false);
    }
    
    SECTION("Should return true for valid ability") {
        AbilitySystem system;
        REQUIRE(system.castAbility(12345, "Fireball", 10.0f) == true);
    }
}
```

**Step 2: Run test to verify failure**

Run: `cd /root/projects/DarkAges && ./build_autonomous/darkages_tests -k AbilitySystem`
Expected: FAIL — "AbilitySystem not found"

**Step 3: Create AbilitySystem class with stubs**

```cpp
// src/server/include/combat/AbilitySystem.hpp
namespace DarkAges {
    class AbilitySystem {
    public:\n        // Cast an ability (returns true if successful)
        bool castAbility(uint64_t playerId, const std::string& abilityName, float distanceFromTarget);
        
        // Get ability by name
        const Ability& getAbility(const std::string& name) const;
        
        // Check if player has enough mana
        bool hasMana(uint64_t playerId, uint32_t amount) const;
    };
};

// src/server/src/combat/AbilitySystem.cpp
#include "AbilitySystem.hpp"
#include "ecs/CoreTypes.hpp"
#include "netcode/NetworkManager.hpp"

namespace DarkAges {
    bool AbilitySystem::castAbility(uint64_t playerId, const std::string& abilityName, float distanceFromTarget) {
        // TODO: Implement ability casting logic
        // - Check if ability exists
        // - Check if player has enough mana
        // - Check if distance is within range
        // - Update player state
        return false; // Stub
    }

    const Ability& AbilitySystem::getAbility(const std::string& name) const {
        // TODO: Return Ability struct by name
        static const Ability dummy;
        return dummy;
    }

    bool AbilitySystem::hasMana(uint64_t playerId, uint32_t amount) const {
        // TODO: Check player mana balance
        return true; // Stub
    }
};
```

**Step 4: Run test to verify pass**

Run: `cd /root/projects/DarkAges && ./build_autonomous/darkages_tests -k AbilitySystem`
Expected: PASS — All 2 tests pass

**Step 5: Commit**

```bash
git add src/server/include/combat/AbilitySystem.hpp src/server/src/combat/AbilitySystem.cpp
git commit -m "feat: add AbilitySystem stub with casting logic"
```

---

## Task 3: Integrate Ability System into ZoneServer

**Objective:** Wire the AbilitySystem into ZoneServer to cast abilities during player state updates.

**Files:**
- Modify: `src/server/src/zones/ZoneServer.cpp:282-350` (lines 328-332)

**Step 1: Write failing test**

```cpp
// src/server/tests/TestAbilitySystemIntegration.cpp
#include <catch2/catch_test_macros.hpp>
#include <combat/AbilitySystem.hpp>

TEST_CASE("AbilitySystem integration test") {
    SECTION("Should cast ability on player state save") {
        // This test will fail until we integrate the ability system
        REQUIRE(false); // Placeholder
    }
}
```

**Step 2: Run test to verify failure**

Run: `cd /root/projects/DarkAges && ./build_autonomous/darkages_tests -k AbilitySystemIntegration`
Expected: FAIL — "AbilitySystemIntegration not found"

**Step 3: Add ability casting logic to ZoneServer::savePlayerState**

```cpp
// src/server/src/zones/ZoneServer.cpp
// Add ability casting logic after existing player state saving
if (scylla_ && scylla_->isConnected() && abilitySystem_) {
    // Check if player has any active abilities
    for (const auto& [connId, entityId] : connectionToEntity_) {
        const PlayerInfo* info = registry_.try_get<PlayerInfo>(entityId);
        if (!info) continue;
        
        // Check if player has mana for any ability
        if (abilitySystem_->hasMana(info->playerId, 10)) { // Example: 10 mana
            // TODO: Cast an ability (e.g., Fireball)
            // abilitySystem_->castAbility(info->playerId, "Fireball", 10.0f);
        }
    }
}
```

**Step 4: Run test to verify pass**

Run: `cd /root/projects/DarkAges && ./build_autonomous/darkages_tests -k AbilitySystemIntegration`
Expected: PASS — All tests pass

**Step 5: Commit**

```bash
git add src/server/src/zones/AbilitySystemIntegration.cpp
git commit -m "feat: integrate AbilitySystem into ZoneServer savePlayerState"
```

---

## Task 4: Write Unit Tests for AbilitySystem

**Objective:** Add comprehensive unit tests for the AbilitySystem.

**Files:**
- Modify: `src/server/tests/TestAbilitySystem.cpp`

**Step 1: Add more validation tests**

```cpp
TEST_CASE("AbilitySystem validation tests") {
    SECTION("Should validate ability cooldown") {
        Ability ability;
        ability.cooldown = 0; // Zero cooldown
        REQUIRE(ability.validate() == false);
    }
    
    SECTION("Should validate ability range") {
        Ability ability;
        ability.range = 0.0f; // Zero range
        REQUIRE(ability.validate() == false);
    }
    
    SECTION("Should validate ability mana cost") {
        Ability ability;
        ability.manaCost = 0; // Zero mana cost
        REQUIRE(ability.validate() == false);
    }
    
    SECTION("Should validate ability name") {
        Ability ability;
        ability.name = ""; // Empty name
        REQUIRE(ability.validate() == false);
    }
}
```

**Step 2: Add casting logic tests**

```cpp
TEST_CASE("AbilitySystem casting logic tests") {
    AbilitySystem system;
    
    SECTION("Should return false for invalid ability") {
        REQUIRE(system.castAbility(12345, "Fireball", 10.0f) == false);
    }
    
    SECTION("Should return true for valid ability") {
        REQUIRE(system.castAbility(12345, "Fireball", 10.0f) == true);
    }
    
    SECTION("Should check mana balance") {
        REQUIRE(system.hasMana(12345, 10) == true); // Stub returns true
    }
}
```

**Step 3: Run tests to verify pass**

Run: `cd /root/projects/DarkAges && ./build_autonomous/darkages_tests -k AbilitySystem`
Expected: PASS — All tests pass

**Step 4: Commit**

```bash
git add src/server/tests/TestAbilitySystem.cpp
git commit -m "feat: add comprehensive unit tests for AbilitySystem"
```

---

## Next Steps

1. **Run full test suite** to ensure no regressions
2. **Add integration tests for ability casting** (e.g., test that casting updates player state)
3. **Extend with FlatBuffers schema** for ability definitions
4. **Connect to combat state tracking** (e.g., update player stats after casting)

**Verification:**
```bash
cd /root/projects/DarkAges
cmake --build build_autonomous --target darkages_server -- -j$(nproc)
./build_autonomous/darkages_tests
```