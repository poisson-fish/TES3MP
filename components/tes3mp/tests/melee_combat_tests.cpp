#include <tes3mp/melee_combat.hpp>

#include <cmath>
#include <limits>

namespace
{
    bool near(float lhs, float rhs)
    {
        return std::abs(lhs - rhs) < 0.0001f;
    }

    TES3MP::OpenMwMeleeSettings settings()
    {
        return { .combatInvisibilityMultiplier = 1.f,
            .fatigueAttackBase = 2.f,
            .fatigueAttackMultiplier = 3.f,
            .weaponFatigueMultiplier = 0.5f,
            .weaponDamageMultiplier = 1.f,
            .damageStrengthBase = 0.5f,
            .damageStrengthMultiplier = 0.1f,
            .minimumHandToHandMultiplier = 0.1f,
            .maximumHandToHandMultiplier = 0.5f,
            .handToHandHealthPercent = 0.1f,
            .combatCriticalStrikeMultiplier = 4.f,
            .combatKnockdownDamageMultiplier = 1.5f };
    }

    TES3MP::OpenMwMeleeAttacker attacker()
    {
        return { .agility = 50.f,
            .luck = 40.f,
            .strength = 60.f,
            .fatigueTerm = 1.f,
            .normalizedEncumbrance = 0.5f,
            .fortifyAttack = 5.f,
            .blind = 1.f,
            .weaponSkill = 60.f,
            .handToHandSkill = 40.f,
            .fatigue = 100.f };
    }

    TES3MP::OpenMwMeleeVictim victim()
    {
        return { .health = 100.f, .fatigue = 80.f, .evasion = 20.f, .fatigueNonNegative = true };
    }

    TES3MP::OpenMwMeleeWeapon weapon()
    {
        return { .chopMinimum = 5.f,
            .chopMaximum = 15.f,
            .slashMinimum = 10.f,
            .slashMaximum = 20.f,
            .thrustMinimum = 2.f,
            .thrustMaximum = 8.f,
            .weight = 12.f,
            .normalizedCondition = 0.5f,
            .condition = 50,
            .hasCondition = true,
            .normalWeapon = true };
    }

    bool weapon_attack_matches_openmw_ordering()
    {
        auto target = victim();
        target.normalWeaponResistance = 25.f;
        TES3MP::OpenMwMeleeAttempt attempt{ .type = TES3MP::MeleeAttackType::Slash,
            .attackStrength = 0.5f,
            .hitRoll0To99 = 10,
            .contact = true,
            .weapon = weapon() };
        const auto result = TES3MP::resolveOpenMwMelee(settings(), attacker(), target, attempt);
        // (10 + (20-10)*.5) * .5 condition * (0.5 + 60*.1*.1) * .75 resistance
        const float expectedDamage = 15.f * 0.5f * 1.1f * 0.75f;
        return result.code == TES3MP::OpenMwMeleeResolutionCode::Resolved && result.hit
            && near(result.hitChance, 58.f) && near(result.fatigueCost, 6.5f)
            && near(result.attackerFatigue, 93.5f) && near(result.damage, expectedDamage)
            && near(result.victimHealth, 100.f - expectedDamage) && result.weaponCondition == 42
            && !result.weaponBroken && !result.victimDied;
    }

    bool miss_wears_weapon_and_empty_swing_only_spends_fatigue()
    {
        auto attempt = TES3MP::OpenMwMeleeAttempt{ .attackStrength = 1.f, .hitRoll0To99 = 99,
            .contact = true, .weapon = weapon() };
        const auto miss = TES3MP::resolveOpenMwMelee(settings(), attacker(), victim(), attempt);
        attempt.contact = false;
        const auto empty = TES3MP::resolveOpenMwMelee(settings(), attacker(), victim(), attempt);
        return !miss.hit && near(miss.damage, 0.f) && miss.weaponCondition == 49
            && near(miss.attackerFatigue, 90.5f) && !empty.hit && near(empty.attackerFatigue, 90.5f)
            && near(empty.victimHealth, 100.f);
    }

    bool unarmed_fatigue_and_health_paths_match_openmw()
    {
        TES3MP::OpenMwMeleeAttempt attempt{ .attackStrength = 0.5f, .hitRoll0To99 = 0,
            .contact = true, .strengthInfluencesHandToHand = true };
        const auto fatigue = TES3MP::resolveOpenMwMelee(settings(), attacker(), victim(), attempt);
        auto down = victim();
        down.knockedDown = true;
        down.health = 3.f;
        const auto health = TES3MP::resolveOpenMwMelee(settings(), attacker(), down, attempt);
        return fatigue.hit && fatigue.damagedStat == TES3MP::MeleeDamageStat::Fatigue
            && near(fatigue.damage, 18.f) && near(fatigue.victimFatigue, 62.f)
            && health.hit && health.damagedStat == TES3MP::MeleeDamageStat::Health
            && near(health.damage, 2.7f) && near(health.victimHealth, 0.f) && health.victimDied;
    }

    bool invalid_external_values_fail_without_results()
    {
        auto attempt = TES3MP::OpenMwMeleeAttempt{ .attackStrength = std::numeric_limits<float>::quiet_NaN(),
            .contact = true };
        const auto invalid = TES3MP::resolveOpenMwMelee(settings(), attacker(), victim(), attempt);
        auto dead = victim();
        dead.dead = true;
        attempt.attackStrength = 0.f;
        const auto deadResult = TES3MP::resolveOpenMwMelee(settings(), attacker(), dead, attempt);
        return invalid.code == TES3MP::OpenMwMeleeResolutionCode::InvalidInput
            && near(invalid.victimHealth, victim().health)
            && deadResult.code == TES3MP::OpenMwMeleeResolutionCode::DeadVictim
            && near(deadResult.attackerFatigue, 96.5f);
    }
}

int main()
{
    return weapon_attack_matches_openmw_ordering() && miss_wears_weapon_and_empty_swing_only_spends_fatigue()
            && unarmed_fatigue_and_health_paths_match_openmw() && invalid_external_values_fail_without_results()
        ? 0
        : 1;
}
