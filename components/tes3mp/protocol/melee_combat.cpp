#include <tes3mp/melee_combat.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    bool finite(float value) noexcept
    {
        return std::isfinite(value);
    }

    bool validSettings(const TES3MP::OpenMwMeleeSettings& value) noexcept
    {
        return finite(value.combatInvisibilityMultiplier) && finite(value.fatigueAttackBase)
            && finite(value.fatigueAttackMultiplier) && finite(value.weaponFatigueMultiplier)
            && finite(value.weaponDamageMultiplier) && finite(value.damageStrengthBase)
            && finite(value.damageStrengthMultiplier) && finite(value.minimumHandToHandMultiplier)
            && finite(value.maximumHandToHandMultiplier) && finite(value.handToHandHealthPercent)
            && finite(value.combatCriticalStrikeMultiplier) && finite(value.combatKnockdownDamageMultiplier);
    }

    bool validAttacker(const TES3MP::OpenMwMeleeAttacker& value) noexcept
    {
        return finite(value.agility) && finite(value.luck) && finite(value.strength) && finite(value.fatigueTerm)
            && finite(value.normalizedEncumbrance) && finite(value.fortifyAttack) && finite(value.blind)
            && finite(value.weaponSkill) && finite(value.handToHandSkill) && finite(value.fatigue);
    }

    bool validVictim(const TES3MP::OpenMwMeleeVictim& value) noexcept
    {
        return finite(value.health) && finite(value.fatigue) && finite(value.evasion) && finite(value.chameleon)
            && finite(value.invisibility) && finite(value.normalWeaponResistance)
            && finite(value.normalWeaponWeakness);
    }

    bool validWeapon(const TES3MP::OpenMwMeleeWeapon& value) noexcept
    {
        return finite(value.chopMinimum) && finite(value.chopMaximum) && finite(value.slashMinimum)
            && finite(value.slashMaximum) && finite(value.thrustMinimum) && finite(value.thrustMaximum)
            && finite(value.weight) && finite(value.normalizedCondition) && value.weight >= 0.f
            && value.normalizedCondition >= 0.f && value.normalizedCondition <= 1.f && value.condition >= 0;
    }
}

namespace TES3MP
{
    float openMwMeleeHitChance(const OpenMwMeleeSettings& settings, const OpenMwMeleeAttacker& attacker,
        const OpenMwMeleeVictim& victim) noexcept
    {
        float defense = 0.f;
        if (victim.fatigueNonNegative)
        {
            if (!(victim.knockedDown || victim.paralyzed || victim.unaware))
                defense = victim.evasion;
            defense += std::min(100.f, settings.combatInvisibilityMultiplier * victim.chameleon);
            defense += std::min(100.f, settings.combatInvisibilityMultiplier * victim.invisibility);
        }
        float attack = attacker.weaponSkill + attacker.agility / 5.f + attacker.luck / 10.f;
        attack *= attacker.fatigueTerm;
        attack += attacker.fortifyAttack - attacker.blind;
        return std::round(attack - defense);
    }

    float openMwMeleeFatigueCost(const OpenMwMeleeSettings& settings, const OpenMwMeleeAttacker& attacker,
        const OpenMwMeleeAttempt& attempt) noexcept
    {
        if (attacker.godMode)
            return 0.f;
        float result = settings.fatigueAttackBase
            + attacker.normalizedEncumbrance * settings.fatigueAttackMultiplier;
        if (attempt.weapon)
            result += attempt.weapon->weight * attempt.attackStrength * settings.weaponFatigueMultiplier;
        return result;
    }

    float openMwMeleeWeaponDamage(const OpenMwMeleeSettings& settings, const OpenMwMeleeAttacker& attacker,
        const OpenMwMeleeWeapon& weapon, MeleeAttackType type, float attackStrength) noexcept
    {
        float minimum = weapon.chopMinimum;
        float maximum = weapon.chopMaximum;
        if (type == MeleeAttackType::Slash)
        {
            minimum = weapon.slashMinimum;
            maximum = weapon.slashMaximum;
        }
        else if (type == MeleeAttackType::Thrust)
        {
            minimum = weapon.thrustMinimum;
            maximum = weapon.thrustMaximum;
        }
        float damage = minimum + (maximum - minimum) * attackStrength;
        return openMwAdjustedWeaponDamage(
            settings, attacker.strength, weapon.normalizedCondition, weapon.hasCondition, damage);
    }

    float openMwAdjustedWeaponDamage(const OpenMwMeleeSettings& settings, float strength,
        float normalizedCondition, bool hasCondition, float damage) noexcept
    {
        if (hasCondition)
            damage *= normalizedCondition;
        return damage * (settings.damageStrengthBase + strength * settings.damageStrengthMultiplier * 0.1f);
    }

    float openMwHandToHandDamage(const OpenMwMeleeSettings& settings, float handToHandSkill, float strength,
        float attackStrength, bool factorStrength, bool werewolf, float werewolfClawMultiplier,
        bool healthDamage) noexcept
    {
        float damage = handToHandSkill
            * (settings.minimumHandToHandMultiplier
                + (settings.maximumHandToHandMultiplier - settings.minimumHandToHandMultiplier) * attackStrength);
        if (factorStrength)
            damage *= strength / 40.f;
        if (werewolf)
            damage *= werewolfClawMultiplier;
        if (healthDamage)
            damage *= settings.handToHandHealthPercent;
        return damage;
    }

    OpenMwMeleeResolution resolveOpenMwMelee(const OpenMwMeleeSettings& settings,
        const OpenMwMeleeAttacker& attacker, const OpenMwMeleeVictim& victim,
        const OpenMwMeleeAttempt& attempt) noexcept
    {
        OpenMwMeleeResolution result;
        result.victimHealth = victim.health;
        result.victimFatigue = victim.fatigue;
        result.attackerFatigue = attacker.fatigue;
        result.weaponCondition = attempt.weapon ? attempt.weapon->condition : 0;
        if (!validSettings(settings) || !validAttacker(attacker) || !validVictim(victim)
            || !finite(attempt.attackStrength) || attempt.attackStrength < 0.f || attempt.attackStrength > 1.f
            || attempt.hitRoll0To99 > 99 || (attempt.weapon && !validWeapon(*attempt.weapon)))
            return result;
        result.code = OpenMwMeleeResolutionCode::Resolved;
        result.fatigueCost = openMwMeleeFatigueCost(settings, attacker, attempt);
        result.attackerFatigue -= result.fatigueCost;
        if (!attempt.contact)
            return result;
        if (victim.dead)
        {
            result.code = OpenMwMeleeResolutionCode::DeadVictim;
            return result;
        }

        OpenMwMeleeAttacker chanceAttacker = attacker;
        chanceAttacker.weaponSkill = attempt.weapon ? attacker.weaponSkill : attacker.handToHandSkill;
        result.hitChance = openMwMeleeHitChance(settings, chanceAttacker, victim);
        result.hit = static_cast<float>(attempt.hitRoll0To99) < result.hitChance;
        if (!result.hit)
        {
            if (attempt.weapon && attempt.weapon->hasCondition && !attacker.godMode)
            {
                const auto loss = std::min<std::int32_t>(1, result.weaponCondition);
                result.weaponCondition -= loss;
                result.weaponBroken = result.weaponCondition == 0;
            }
            return result;
        }

        if (attempt.weapon)
        {
            result.damage = openMwMeleeWeaponDamage(
                settings, attacker, *attempt.weapon, attempt.type, attempt.attackStrength);
            // OpenMW damages the weapon from pre-resistance, pre-critical damage.
            if (attempt.weapon->hasCondition && !attacker.godMode)
            {
                const float conditionDamage = std::max(1.f, settings.weaponDamageMultiplier * result.damage);
                const auto loss = std::min(static_cast<std::int32_t>(conditionDamage), result.weaponCondition);
                result.weaponCondition -= loss;
                result.weaponBroken = result.weaponCondition == 0;
            }
            if (attempt.weapon->normalWeapon)
            {
                const float resistance = victim.normalWeaponResistance / 100.f;
                const float weakness = victim.normalWeaponWeakness / 100.f;
                result.damage *= 1.f - std::min(1.f, resistance - weakness);
            }
            result.damagedStat = MeleeDamageStat::Health;
        }
        else
        {
            const bool healthDamage = victim.paralyzed || victim.knockedDown || attacker.werewolf;
            result.damage = openMwHandToHandDamage(settings, attacker.handToHandSkill, attacker.strength,
                attempt.attackStrength, attempt.strengthInfluencesHandToHand, attacker.werewolf,
                attempt.werewolfClawMultiplier, healthDamage);
            result.damagedStat = healthDamage ? MeleeDamageStat::Health : MeleeDamageStat::Fatigue;
        }

        if (victim.unaware)
            result.damage *= settings.combatCriticalStrikeMultiplier;
        if (victim.knockedDown)
            result.damage *= settings.combatKnockdownDamageMultiplier;
        result.blocked = attempt.blocked;
        if (result.blocked)
            result.damage = 0.f;

        if (result.damagedStat == MeleeDamageStat::Health)
        {
            const float remaining = victim.health - result.damage;
            result.victimDied = remaining < 1.f;
            result.victimHealth = result.victimDied ? 0.f : remaining;
        }
        else
            result.victimFatigue = victim.fatigue - result.damage;
        return result;
    }
}
