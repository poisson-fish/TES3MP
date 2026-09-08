#ifndef TES3MP_MELEE_COMBAT_HPP
#define TES3MP_MELEE_COMBAT_HPP

#include "value_types.hpp"

#include <cstdint>
#include <optional>

namespace TES3MP
{
    enum class MeleeAttackType : std::uint8_t
    {
        Chop = 0,
        Slash = 1,
        Thrust = 2,
    };

    enum class MeleeDamageStat : std::uint8_t
    {
        Health = 0,
        Fatigue = 1,
    };

    struct OpenMwMeleeSettings
    {
        float combatInvisibilityMultiplier = 0.f;
        float fatigueAttackBase = 0.f;
        float fatigueAttackMultiplier = 0.f;
        float weaponFatigueMultiplier = 0.f;
        float weaponDamageMultiplier = 0.f;
        float damageStrengthBase = 1.f;
        float damageStrengthMultiplier = 0.f;
        float minimumHandToHandMultiplier = 0.f;
        float maximumHandToHandMultiplier = 0.f;
        float handToHandHealthPercent = 1.f;
        float combatCriticalStrikeMultiplier = 1.f;
        float combatKnockdownDamageMultiplier = 1.f;

        friend constexpr bool operator==(OpenMwMeleeSettings, OpenMwMeleeSettings) noexcept = default;
    };

    struct OpenMwMeleeAttacker
    {
        float agility = 0.f;
        float luck = 0.f;
        float strength = 0.f;
        float fatigueTerm = 1.f;
        float normalizedEncumbrance = 0.f;
        float fortifyAttack = 0.f;
        float blind = 0.f;
        float weaponSkill = 0.f;
        float handToHandSkill = 0.f;
        float fatigue = 0.f;
        bool werewolf = false;
        bool godMode = false;

        friend constexpr bool operator==(OpenMwMeleeAttacker, OpenMwMeleeAttacker) noexcept = default;
    };

    struct OpenMwMeleeVictim
    {
        float health = 0.f;
        float fatigue = 0.f;
        float evasion = 0.f;
        float chameleon = 0.f;
        float invisibility = 0.f;
        float normalWeaponResistance = 0.f;
        float normalWeaponWeakness = 0.f;
        bool fatigueNonNegative = true;
        bool knockedDown = false;
        bool paralyzed = false;
        bool unaware = false;
        bool dead = false;

        friend constexpr bool operator==(OpenMwMeleeVictim, OpenMwMeleeVictim) noexcept = default;
    };

    struct OpenMwMeleeWeapon
    {
        float chopMinimum = 0.f;
        float chopMaximum = 0.f;
        float slashMinimum = 0.f;
        float slashMaximum = 0.f;
        float thrustMinimum = 0.f;
        float thrustMaximum = 0.f;
        float weight = 0.f;
        float normalizedCondition = 1.f;
        std::int32_t condition = 0;
        bool hasCondition = false;
        bool normalWeapon = true;

        friend constexpr bool operator==(OpenMwMeleeWeapon, OpenMwMeleeWeapon) noexcept = default;
    };

    struct OpenMwMeleeAttempt
    {
        MeleeAttackType type = MeleeAttackType::Chop;
        float attackStrength = 0.f;
        std::uint8_t hitRoll0To99 = 0;
        bool contact = false;
        bool blocked = false;
        bool strengthInfluencesHandToHand = false;
        float werewolfClawMultiplier = 1.f;
        std::optional<OpenMwMeleeWeapon> weapon;

        friend constexpr bool operator==(const OpenMwMeleeAttempt&, const OpenMwMeleeAttempt&) noexcept = default;
    };

    enum class OpenMwMeleeResolutionCode : std::uint8_t
    {
        Resolved,
        InvalidInput,
        DeadVictim,
    };

    struct OpenMwMeleeResolution
    {
        OpenMwMeleeResolutionCode code = OpenMwMeleeResolutionCode::InvalidInput;
        float hitChance = 0.f;
        float fatigueCost = 0.f;
        float damage = 0.f;
        float victimHealth = 0.f;
        float victimFatigue = 0.f;
        float attackerFatigue = 0.f;
        std::int32_t weaponCondition = 0;
        MeleeDamageStat damagedStat = MeleeDamageStat::Health;
        bool hit = false;
        bool blocked = false;
        bool weaponBroken = false;
        bool victimDied = false;

        friend constexpr bool operator==(OpenMwMeleeResolution, OpenMwMeleeResolution) noexcept = default;
    };

    float openMwMeleeHitChance(const OpenMwMeleeSettings& settings, const OpenMwMeleeAttacker& attacker,
        const OpenMwMeleeVictim& victim) noexcept;
    float openMwMeleeFatigueCost(const OpenMwMeleeSettings& settings, const OpenMwMeleeAttacker& attacker,
        const OpenMwMeleeAttempt& attempt) noexcept;
    float openMwMeleeWeaponDamage(const OpenMwMeleeSettings& settings, const OpenMwMeleeAttacker& attacker,
        const OpenMwMeleeWeapon& weapon, MeleeAttackType type, float attackStrength) noexcept;
    float openMwAdjustedWeaponDamage(const OpenMwMeleeSettings& settings, float strength,
        float normalizedCondition, bool hasCondition, float damage) noexcept;
    float openMwHandToHandDamage(const OpenMwMeleeSettings& settings, float handToHandSkill, float strength,
        float attackStrength, bool factorStrength, bool werewolf, float werewolfClawMultiplier,
        bool healthDamage) noexcept;
    OpenMwMeleeResolution resolveOpenMwMelee(const OpenMwMeleeSettings& settings,
        const OpenMwMeleeAttacker& attacker, const OpenMwMeleeVictim& victim,
        const OpenMwMeleeAttempt& attempt) noexcept;
}

#endif
