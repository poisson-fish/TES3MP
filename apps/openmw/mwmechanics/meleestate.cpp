#include "meleestate.hpp"

#include <algorithm>
#include <cmath>

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadweap.hpp>
#include <tes3mp/melee_combat.hpp>

#include "../mwworld/esmstore.hpp"
#include "creaturestats.hpp"

namespace MWMechanics
{
    float getHitChance(const MWWorld::ESMStore& store, const CreatureStats& attacker,
        const CreatureStats& victim, int skillValue, bool unaware, bool paralyzed)
    {
        TES3MP::OpenMwMeleeSettings settings;
        settings.combatInvisibilityMultiplier
            = store.get<ESM::GameSetting>().find("fCombatInvisoMult")->mValue.getFloat();
        const auto& effects = attacker.getMagicEffects();
        TES3MP::OpenMwMeleeAttacker attack;
        attack.weaponSkill = static_cast<float>(skillValue);
        attack.agility = attacker.getAttribute(ESM::Attribute::Agility).getModified();
        attack.luck = attacker.getAttribute(ESM::Attribute::Luck).getModified();
        attack.fatigueTerm = attacker.getFatigueTerm(store);
        attack.fortifyAttack = effects.getOrDefault(ESM::MagicEffect::FortifyAttack).getMagnitude();
        attack.blind = effects.getOrDefault(ESM::MagicEffect::Blind).getMagnitude();
        TES3MP::OpenMwMeleeVictim defense;
        defense.evasion = victim.getEvasion(store);
        defense.chameleon = victim.getMagicEffects().getOrDefault(ESM::MagicEffect::Chameleon).getMagnitude();
        defense.invisibility = victim.getMagicEffects().getOrDefault(ESM::MagicEffect::Invisibility).getMagnitude();
        defense.fatigueNonNegative = victim.getFatigue().getCurrent() >= 0;
        defense.knockedDown = victim.getKnockedDown();
        defense.paralyzed = paralyzed;
        defense.unaware = unaware;
        return TES3MP::openMwMeleeHitChance(settings, attack, defense);
    }

    void applyFatigueLoss(CreatureStats& attacker, const MWWorld::ESMStore& content,
        float weaponWeight, float attackStrength, float normalizedEncumbrance)
    {
        const auto& store = content.get<ESM::GameSetting>();
        TES3MP::OpenMwMeleeSettings settings;
        settings.fatigueAttackBase = store.find("fFatigueAttackBase")->mValue.getFloat();
        settings.fatigueAttackMultiplier = store.find("fFatigueAttackMult")->mValue.getFloat();
        settings.weaponFatigueMultiplier = store.find("fWeaponFatigueMult")->mValue.getFloat();
        TES3MP::OpenMwMeleeAttacker state;
        state.normalizedEncumbrance = normalizedEncumbrance;
        TES3MP::OpenMwMeleeAttempt attempt;
        attempt.attackStrength = attackStrength;
        attempt.weapon.emplace().weight = weaponWeight;
        auto fatigue = attacker.getFatigue();
        fatigue.setCurrent(fatigue.getCurrent() - TES3MP::openMwMeleeFatigueCost(settings, state, attempt));
        attacker.setFatigue(fatigue);
    }

    int weaponConditionAfterHit(int condition, float damage, bool hit, float damageMultiplier)
    {
        if (!hit) damage = 0.f;
        const float wear = std::max(1.f, damageMultiplier * damage);
        return condition - std::min(int(wear), condition);
    }

    float getMeleeWeaponReach(const MWWorld::ESMStore& content, const ESM::Weapon* weapon, bool npc)
    {
        const auto& store = content.get<ESM::GameSetting>();
        const float distance = store.find("fCombatDistance")->mValue.getFloat();
        if (weapon) return distance * weapon->mData.mReach;
        if (npc) return distance * store.find("fHandToHandReach")->mValue.getFloat();
        return distance;
    }

    bool isInMeleeReach(const osg::Vec3f& attacker, const osg::Vec3f& target,
        float attackerHalfExtentY, float targetHalfExtentY, float reach)
    {
        // Preserve the engine's strict vertical and bounds-distance tests;
        // this is not a line-of-sight or attack-cone query.
        float distance = (target - attacker).length();
        distance -= attackerHalfExtentY;
        distance -= targetHalfExtentY;
        return std::abs(attacker.z() - target.z()) < reach && distance < reach;
    }

    HitDamageResult applyHitDamage(CreatureStats& victim, const std::map<std::string, float>& damages,
        const std::optional<MWWorld::TimeStamp>& time)
    {
        HitDamageResult result;
        for (const auto& [stat, damage] : damages)
        {
            if (damage < 0.001f) continue;
            result.mHasDamage = true;
            if (stat == "health")
            {
                result.mHasHealthDamage = true;
                result.mHealthDamage = damage;
                auto health = victim.getHealth();
                health.setCurrent(health.getCurrent() - damage);
                if (time) victim.setHealth(health, *time);
                else victim.setHealth(health);
            }
            else if (stat == "fatigue")
            {
                auto fatigue = victim.getFatigue();
                fatigue.setCurrent(fatigue.getCurrent() - damage, true);
                victim.setFatigue(fatigue);
            }
            else if (stat == "magicka")
            {
                auto magicka = victim.getMagicka();
                magicka.setCurrent(magicka.getCurrent() - damage);
                victim.setMagicka(magicka);
            }
        }
        return result;
    }

    float attackWindUp(float currentTime, float minimumTime, float maximumTime)
    {
        if (minimumTime == -1.f || minimumTime >= maximumTime) return -1.f;
        return std::clamp((currentTime - minimumTime) / (maximumTime - minimumTime), 0.f, 1.f);
    }

    float attackReleaseStartPoint(float strength, float minimumAttackTime, float maximumAttackTime,
        float minimumHitTime, float hitTime)
    {
        if (minimumAttackTime == -1.f || minimumAttackTime >= maximumAttackTime) return 0.f;
        float startPoint = 1.f - strength;
        if (maximumAttackTime <= minimumHitTime && minimumHitTime < hitTime)
            startPoint *= (minimumHitTime - maximumAttackTime) / (hitTime - maximumAttackTime);
        return startPoint;
    }

    std::string_view attackFollowStrength(float strength)
    {
        return strength < .33f ? "small" : strength < .66f ? "medium" : "large";
    }

    int meleeHitType(std::string_view group, std::string_view action)
    {
        if (action == "chop hit") return ESM::Weapon::AT_Chop;
        if (action == "slash hit") return ESM::Weapon::AT_Slash;
        if (action == "thrust hit") return ESM::Weapon::AT_Thrust;
        if (action == "hit")
        {
            if (group == "attack1" || group == "swimattack1") return ESM::Weapon::AT_Chop;
            if (group == "attack2" || group == "swimattack2") return ESM::Weapon::AT_Slash;
            if (group == "attack3" || group == "swimattack3") return ESM::Weapon::AT_Thrust;
        }
        return -1;
    }

    bool hasMeleeHitKey(std::string_view group, SceneUtil::TextKeyMap::ConstIterator start,
        const SceneUtil::TextKeyMap& keys)
    {
        for (auto key = start; key != keys.end(); ++key)
        {
            if (!key->second.starts_with(group)) continue;
            const auto suffix = std::string_view(key->second).substr(group.size());
            if (suffix == ": hit") return true;
            if (suffix == ": stop") break;
        }
        return false;
    }
}
