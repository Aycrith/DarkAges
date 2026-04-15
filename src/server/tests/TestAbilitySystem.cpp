// [COMBAT_AGENT] Unit tests for AbilitySystem

#include <catch2/catch_test_macros.hpp>
#include "combat/AbilitySystem.hpp"
#include "ecs/CoreTypes.hpp"
#include <entt/entt.hpp>
#include <string>

using namespace DarkAges;

TEST_CASE("Ability validation", "[AbilitySystem]") {
    SECTION("Valid ability passes validation") {
        AbilityDefinition ability("Fireball", 5000, 100, 30.0f, AbilityEffectType::Damage);
        REQUIRE(ability.validate() == true);
    }

    SECTION("Ability with empty name fails validation") {
        AbilityDefinition ability("", 5000, 100, 30.0f, AbilityEffectType::Damage);
        REQUIRE(ability.validate() == false);
    }

    SECTION("Zero mana cost fails validation") {
        AbilityDefinition ability("Fireball", 5000, 0, 30.0f, AbilityEffectType::Damage);
        REQUIRE(ability.validate() == false);
    }

    SECTION("Zero range fails validation") {
        AbilityDefinition ability("Fireball", 5000, 100, 0.0f, AbilityEffectType::Damage);
        REQUIRE(ability.validate() == false);
    }
}

TEST_CASE("AbilityEffectType enum values", "[AbilitySystem]") {
    REQUIRE(static_cast<uint8_t>(AbilityEffectType::Damage) == 1);
    REQUIRE(static_cast<uint8_t>(AbilityEffectType::Heal) == 2);
    REQUIRE(static_cast<uint8_t>(AbilityEffectType::Buff) == 3);
    REQUIRE(static_cast<uint8_t>(AbilityEffectType::Debuff) == 4);
    REQUIRE(static_cast<uint8_t>(AbilityEffectType::Status) == 5);
}

TEST_CASE("AbilitySystem registration", "[AbilitySystem]") {
    AbilitySystem system;

    SECTION("Register and retrieve ability") {
        AbilityDefinition fireball("Fireball", 5000, 100, 30.0f, AbilityEffectType::Damage);
        system.registerAbility(1, fireball);

        const AbilityDefinition* found = system.getAbility(1);
        REQUIRE(found != nullptr);
        REQUIRE(found->name == "Fireball");
    }

    SECTION("Get unknown ability returns nullptr") {
        const AbilityDefinition* found = system.getAbility(999);
        REQUIRE(found == nullptr);
    }
}

TEST_CASE("AbilitySystem hasMana", "[AbilitySystem]") {
    Registry registry;
    AbilitySystem system;

    SECTION("Returns false when entity has no Mana component") {
        EntityID caster = registry.create();
        AbilityDefinition ability("Fireball", 5000, 50, 30.0f, AbilityEffectType::Damage);
        REQUIRE(system.hasMana(registry, caster, ability) == false);
    }

    SECTION("Returns false when mana is insufficient") {
        EntityID caster = registry.create();
        registry.emplace<Mana>(caster, 10.0f, 100.0f, 1.0f);
        AbilityDefinition ability("Fireball", 5000, 50, 30.0f, AbilityEffectType::Damage);
        REQUIRE(system.hasMana(registry, caster, ability) == false);
    }

    SECTION("Returns true when mana is sufficient") {
        EntityID caster = registry.create();
        registry.emplace<Mana>(caster, 100.0f, 100.0f, 1.0f);
        AbilityDefinition ability("Fireball", 5000, 50, 30.0f, AbilityEffectType::Damage);
        REQUIRE(system.hasMana(registry, caster, ability) == true);
    }
}

TEST_CASE("AbilitySystem casting failures", "[AbilitySystem]") {
    Registry registry;
    AbilitySystem system;
    AbilityDefinition fireball("Fireball", 5000, 50, 30.0f, AbilityEffectType::Damage);
    system.registerAbility(1, fireball);

    SECTION("Returns false for null caster") {
        REQUIRE(system.castAbility(registry, entt::null, entt::null, fireball, 1000) == false);
    }

    SECTION("Returns false for null target") {
        EntityID caster = registry.create();
        registry.emplace<Position>(caster, Position{100, 0, 100, 0});
        registry.emplace<Mana>(caster, 100.0f, 100.0f, 1.0f);
        registry.emplace<Abilities>(caster);
        REQUIRE(system.castAbility(registry, caster, entt::null, fireball, 1000) == false);
    }

    SECTION("Returns false when mana insufficient") {
        EntityID caster = registry.create();
        EntityID target = registry.create();
        registry.emplace<Position>(caster, Position{100, 0, 100, 0});
        registry.emplace<Position>(target, Position{100, 0, 100, 0});
        registry.emplace<Mana>(caster, 10.0f, 100.0f, 1.0f);
        registry.emplace<Abilities>(caster);
        REQUIRE(system.castAbility(registry, caster, target, fireball, 1000) == false);
    }

    SECTION("Returns false when out of range") {
        EntityID caster = registry.create();
        EntityID target = registry.create();
        registry.emplace<Position>(caster, Position{0, 0, 0, 0});
        registry.emplace<Position>(target, Position{50000, 0, 0, 0});
        registry.emplace<Mana>(caster, 100.0f, 100.0f, 1.0f);
        registry.emplace<Abilities>(caster);
        REQUIRE(system.castAbility(registry, caster, target, fireball, 1000) == false);
    }
}

TEST_CASE("AbilitySystem casting success", "[AbilitySystem]") {
    Registry registry;
    AbilitySystem system;
    AbilityDefinition fireball("Fireball", 5000, 50, 30.0f, AbilityEffectType::Damage);
    system.registerAbility(1, fireball);

    SECTION("Returns true when all checks pass") {
        EntityID caster = registry.create();
        EntityID target = registry.create();
        registry.emplace<Position>(caster, Position{0, 0, 0, 0});
        registry.emplace<Position>(target, Position{100, 0, 100, 0});
        registry.emplace<Mana>(caster, 100.0f, 100.0f, 1.0f);
        registry.emplace<Abilities>(caster);
        CombatState targetCombat;
        targetCombat.health = 10000;
        targetCombat.maxHealth = 10000;
        registry.emplace<CombatState>(target, targetCombat);

        REQUIRE(system.castAbility(registry, caster, target, fireball, 1000) == true);
    }

    SECTION("Deducts mana on successful cast") {
        EntityID caster = registry.create();
        EntityID target = registry.create();
        registry.emplace<Position>(caster, Position{0, 0, 0, 0});
        registry.emplace<Position>(target, Position{100, 0, 100, 0});
        registry.emplace<Mana>(caster, 100.0f, 100.0f, 1.0f);
        registry.emplace<Abilities>(caster);
        CombatState targetCombat;
        targetCombat.health = 10000;
        targetCombat.maxHealth = 10000;
        registry.emplace<CombatState>(target, targetCombat);

        system.castAbility(registry, caster, target, fireball, 1000);

        Mana* mana = registry.try_get<Mana>(caster);
        REQUIRE(mana->current == 50.0f);
    }

    SECTION("Applies damage to target on successful cast") {
        EntityID caster = registry.create();
        EntityID target = registry.create();
        registry.emplace<Position>(caster, Position{0, 0, 0, 0});
        registry.emplace<Position>(target, Position{100, 0, 100, 0});
        registry.emplace<Mana>(caster, 100.0f, 100.0f, 1.0f);
        registry.emplace<Abilities>(caster);
        CombatState targetCombat;
        targetCombat.health = 10000;
        targetCombat.maxHealth = 10000;
        registry.emplace<CombatState>(target, targetCombat);

        system.castAbility(registry, caster, target, fireball, 1000);

        CombatState* result = registry.try_get<CombatState>(target);
        REQUIRE(result->health == 9500);
    }

    SECTION("Heals target for Heal ability type") {
        Registry registry;
        AbilitySystem system;
        AbilityDefinition heal("Heal", 5000, 50, 30.0f, AbilityEffectType::Heal);
        system.registerAbility(2, heal);

        EntityID caster = registry.create();
        EntityID target = registry.create();
        registry.emplace<Position>(caster, Position{0, 0, 0, 0});
        registry.emplace<Position>(target, Position{100, 0, 100, 0});
        registry.emplace<Mana>(caster, 100.0f, 100.0f, 1.0f);
        registry.emplace<Abilities>(caster);
        CombatState targetCombat;
        targetCombat.health = 1000;
        targetCombat.maxHealth = 10000;
        registry.emplace<CombatState>(target, targetCombat);

        system.castAbility(registry, caster, target, heal, 1000);

        CombatState* result = registry.try_get<CombatState>(target);
        REQUIRE(result->health == 2000);
    }
}