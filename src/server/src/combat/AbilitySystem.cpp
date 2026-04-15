#include "combat/AbilitySystem.hpp"
#include "ecs/CoreTypes.hpp"
#include <entt/entt.hpp>
#include <cmath>

namespace DarkAges {

// ============================================================================
// Ability Implementation
// ============================================================================

bool AbilityDefinition::validate() const {
    return !name.empty() && manaCost > 0 && range > 0.0f;
}

// ============================================================================
// Ability System Implementation
// ============================================================================

bool AbilitySystem::castAbility(Registry& registry, EntityID caster,
                                  EntityID target, const AbilityDefinition& ability,
                                  uint32_t currentTimeMs) {
    if (!ability.validate()) {
        return false;
    }

    if (caster == entt::null || target == entt::null) {
        return false;
    }

    if (!hasMana(registry, caster, ability)) {
        return false;
    }

    if (!isInRange(registry, caster, target, ability.range)) {
        return false;
    }

    if (!checkCooldown(registry, caster, ability.abilityId, currentTimeMs, ability.cooldownMs)) {
        return false;
    }

    applyAbilityEffect(registry, caster, target, ability, currentTimeMs);

    return true;
}

bool AbilitySystem::hasMana(Registry& registry, EntityID caster, const AbilityDefinition& ability) const {
    Mana* mana = registry.try_get<Mana>(caster);
    if (!mana) {
        return false;
    }
    return mana->current >= static_cast<float>(ability.manaCost);
}

const AbilityDefinition* AbilitySystem::getAbility(uint32_t abilityId) const {
    for (const auto& [id, def] : abilities_) {
        if (id == abilityId) {
            return &def;
        }
    }
    return nullptr;
}

void AbilitySystem::registerAbility(uint32_t abilityId, const AbilityDefinition& ability) {
    if (ability.validate()) {
        abilities_.emplace_back(abilityId, ability);
    }
}

bool AbilitySystem::isInRange(Registry& registry, EntityID caster, EntityID target, float range) const {
    const Position* casterPos = registry.try_get<Position>(caster);
    const Position* targetPos = registry.try_get<Position>(target);
    
    if (!casterPos || !targetPos) {
        return false;
    }

    const auto distSq = casterPos->distanceSqTo(*targetPos);
    const float rangeSq = range * range;
    
    return distSq <= static_cast<Constants::Fixed>(rangeSq * Constants::FLOAT_TO_FIXED);
}

bool AbilitySystem::checkCooldown(Registry& registry, EntityID caster, uint32_t abilityId, 
                                 uint32_t currentTimeMs, uint32_t cooldownMs) const {
    Abilities* abilities = registry.try_get<Abilities>(caster);
    if (!abilities) {
        return true;
    }

    for (uint32_t i = 0; i < abilities->count && i < Abilities::MAX_ABILITIES; ++i) {
        Ability& ab = abilities->abilities[i];
        if (ab.abilityId == abilityId) {
            uint32_t elapsed = currentTimeMs - static_cast<uint32_t>(ab.cooldownRemaining * 1000.0f);
            return elapsed >= cooldownMs;
        }
    }

    return true;
}

void AbilitySystem::applyAbilityEffect(Registry& registry, EntityID caster, EntityID target,
                                     const AbilityDefinition& ability, uint32_t currentTimeMs) {
    Mana* mana = registry.try_get<Mana>(caster);
    if (mana) {
        mana->current -= static_cast<float>(ability.manaCost);
        if (mana->current < 0.0f) {
            mana->current = 0.0f;
        }
    }

    Abilities* abilities = registry.try_get<Abilities>(caster);
    if (abilities) {
        bool found = false;
        for (uint32_t i = 0; i < abilities->count && i < Abilities::MAX_ABILITIES; ++i) {
            if (abilities->abilities[i].abilityId == ability.abilityId) {
                abilities->abilities[i].cooldownRemaining = static_cast<float>(ability.cooldownMs) / 1000.0f;
                found = true;
                break;
            }
        }
        if (!found && abilities->count < Abilities::MAX_ABILITIES) {
            abilities->abilities[abilities->count].abilityId = ability.abilityId;
            abilities->abilities[abilities->count].cooldownRemaining = static_cast<float>(ability.cooldownMs) / 1000.0f;
            abilities->abilities[abilities->count].manaCost = static_cast<float>(ability.manaCost);
            abilities->count++;
        }
    }

    switch (ability.effectType) {
        case AbilityEffectType::Damage: {
            CombatState* targetCombat = registry.try_get<CombatState>(target);
            if (targetCombat) {
                const int16_t damage = static_cast<int16_t>(ability.manaCost * 10);
                targetCombat->health -= damage;
                if (targetCombat->health < 0) {
                    targetCombat->health = 0;
                    targetCombat->isDead = true;
                }
                targetCombat->lastAttacker = caster;
            }
            break;
        }
        case AbilityEffectType::Heal: {
            CombatState* targetCombat = registry.try_get<CombatState>(target);
            if (targetCombat) {
                const int16_t heal = static_cast<int16_t>(ability.manaCost * 20);
                targetCombat->health += heal;
                if (targetCombat->health > targetCombat->maxHealth) {
                    targetCombat->health = targetCombat->maxHealth;
                }
            }
            break;
        }
        default:
            break;
    }
}

} // namespace DarkAges