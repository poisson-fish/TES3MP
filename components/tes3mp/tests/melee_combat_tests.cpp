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
            .combatKnockdownDamageMultiplier = 1.5f,
            .fatigueBase = 1.25f,
            .fatigueMultiplier = 0.5f,
            .fatigueReturnBase = 0.02f,
            .fatigueReturnMultiplier = 0.04f,
            .enduranceFatigueMultiplier = 0.1f };
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

    bool fatigue_term_and_recovery_match_openmw_formulas()
    {
        const auto input = settings();
        const float term = TES3MP::openMwFatigueTerm(input, 50.f, 100.f);
        const float recovery = TES3MP::openMwFatigueRecoveryPerSecond(input, 40.f, 0.5f);
        return near(term, 1.f) && near(recovery, 0.16f)
            && near(TES3MP::openMwFatigueTerm(input, 100.f, 0.f), 1.25f)
            && near(TES3MP::openMwFatigueRecoveryPerSecond(input, 40.f, 2.f), 0.08f)
            && near(TES3MP::openMwFatigueRecoveryPerSecond(input,
                std::numeric_limits<float>::quiet_NaN(), 0.f), 0.f);
    }

    bool difficulty_and_block_helpers_match_openmw_formulas()
    {
        auto input = settings();
        input.difficultyMultiplier = 5.f;
        input.swingBlockMultiplier = 1.f;
        input.swingBlockBase = 1.f;
        input.blockStillBonus = 1.25f;
        input.blockMinimumChance = 10.f;
        input.blockMaximumChance = 50.f;
        input.fatigueBlockBase = 4.f;
        input.fatigueBlockMultiplier = 2.f;
        input.weaponFatigueBlockMultiplier = 1.f;
        auto enemy = attacker();
        enemy.weaponSkill = 20.f;
        auto invalidBlock = input;
        invalidBlock.fatigueBlockBase = -1.f;
        return near(TES3MP::openMwDifficultyScaledDamage(input, 10.f, 100, true), 60.f)
            && near(TES3MP::openMwDifficultyScaledDamage(input, 10.f, 100, false), 8.f)
            && near(TES3MP::openMwDifficultyScaledDamage(input, 10.f, -100, true), 8.f)
            && near(TES3MP::openMwDifficultyScaledDamage(input, 10.f, -100, false), 60.f)
            && near(TES3MP::openMwMeleeBlockChance(input, 20.f, attacker(), enemy, 1.f, false), 34.f)
            && near(TES3MP::openMwMeleeBlockChance(input, 20.f, attacker(), enemy, 1.f, true), 50.f)
            && near(TES3MP::openMwMeleeBlockFatigueCost(input, 0.5f, weapon(), 0.5f), 11.f)
            && near(TES3MP::openMwMeleeBlockFatigueCost(invalidBlock, 0.5f, weapon(), 0.5f), 0.f)
            && near(TES3MP::openMwDifficultyScaledDamage(input, 10.f, 101, true), 0.f);
    }

    bool armor_helpers_match_openmw_formulas_and_bounds()
    {
        auto input = settings();
        input.baseArmorSkill = 30.f;
        input.unarmoredBase1 = 0.01f;
        input.unarmoredBase2 = 0.01f;
        input.combatArmorMinimumMultiplier = 0.25f;
        auto invalid = input;
        invalid.baseArmorSkill = 0.f;
        return near(TES3MP::openMwSkillAdjustedArmorRating(input, 20.f, 45.f, 0.5f), 15.f)
            && near(TES3MP::openMwUnarmoredRating(input, 50.f), 0.25f)
            && near(TES3MP::openMwArmorAdjustedDamage(input, 10.f, 30.f), 2.5f)
            && near(TES3MP::openMwArmorAdjustedDamage(input, 2.f, 100.f), 0.5f)
            && near(TES3MP::openMwSkillAdjustedArmorRating(invalid, 20.f, 45.f, 1.f), 0.f)
            && near(TES3MP::openMwArmorAdjustedDamage(input,
                std::numeric_limits<float>::quiet_NaN(), 10.f), 0.f);
    }
}

int main()
{
    return weapon_attack_matches_openmw_ordering() && miss_wears_weapon_and_empty_swing_only_spends_fatigue()
            && unarmed_fatigue_and_health_paths_match_openmw() && invalid_external_values_fail_without_results()
            && fatigue_term_and_recovery_match_openmw_formulas()
            && difficulty_and_block_helpers_match_openmw_formulas()
            && armor_helpers_match_openmw_formulas_and_bounds()
        ? 0
        : 1;
}
