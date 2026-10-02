#include "meleestate.hpp"
#include <components/esm3/loadcrea.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/constants.hpp>
#include <tes3mp/melee_combat.hpp>

#include "../mwworld/esmstore.hpp"
#include "creaturestats.hpp"

namespace MWMechanics
{
    float awarenessTarget(const MWWorld::ESMStore& store, const CreatureStats& target,
        const CreatureStats& observer, const AwarenessContext& context)
    {
        const auto setting = [&](const char* id) { return store.get<ESM::GameSetting>().find(id)->mValue.getFloat(); };
        const auto delta = context.targetPosition - context.observerPosition;
        const float sneak = context.sneaking ? setting("fSneakSkillMult") * context.targetSneak
            + .2f * target.getAttribute(ESM::Attribute::Agility).getModified()
            + .1f * target.getAttribute(ESM::Attribute::Luck).getModified()
            + context.bootWeight * setting("fSneakBootMult") : 0.f;
        const float x = sneak * (setting("fSneakDistanceBase")
            + setting("fSneakDistanceMultiplier") * delta.length()) * target.getFatigueTerm(store)
            + magicConcealmentTarget(target);
        float y = 0.f;
        if (context.observerDirection)
        {
            const float term = context.observerSneak
                + .2f * observer.getAttribute(ESM::Attribute::Agility).getModified()
                + .1f * observer.getAttribute(ESM::Attribute::Luck).getModified()
                - observer.getMagicEffects().getOrDefault(ESM::MagicEffect::Blind).getMagnitude();
            y = term * observer.getFatigueTerm(store) * setting(
                *context.observerDirection * delta < 0.f ? "fSneakNoViewMult" : "fSneakViewMult");
        }
        return x - y;
    }

    float creatureAttackDamage(const ESM::Creature& creature, int type, float strength)
    {
        const auto offset = type == 0 ? 0 : type == 1 ? 2 : 4;
        const auto& attack = creature.mData.mAttack;
        return attack[offset] + (attack[offset + 1] - attack[offset]) * strength;
    }
    float magicConcealmentTarget(const CreatureStats& target)
    {
        const auto& effects = target.getMagicEffects();
        return effects.getOrDefault(ESM::MagicEffect::Chameleon).getMagnitude()
            + (effects.getOrDefault(ESM::MagicEffect::Invisibility).getMagnitude() > 0.f ? 100.f : 0.f);
    }

    bool isTargetMagicallyHidden(const CreatureStats& target)
    {
        const auto& effects = target.getMagicEffects();
        return effects.getOrDefault(ESM::MagicEffect::Invisibility).getMagnitude() > 0.f
            || effects.getOrDefault(ESM::MagicEffect::Chameleon).getMagnitude() >= 75.f;
    }

    bool isFatigueKnockout(float baseFatigue, float currentFatigue)
    {
        return currentFatigue < 0 || baseFatigue == 0;
    }

    bool rollHitKnockdown(const MWWorld::ESMStore& store, const CreatureStats& victim,
        float healthDamage, Misc::Rng::Generator& rng)
    {
        if (healthDamage <= 0) return false;
        const auto& gmst = store.get<ESM::GameSetting>();
        const float agility = victim.getAttribute(ESM::Attribute::Agility).getModified();
        const float agilityTerm = agility * gmst.find("fKnockDownMult")->mValue.getFloat();
        const float knockdownTerm = agility * gmst.find("iKnockDownOddsMult")->mValue.getInteger() * .01f
            + gmst.find("iKnockDownOddsBase")->mValue.getInteger();
        return agilityTerm <= healthDamage && knockdownTerm <= Misc::Rng::roll0to99(rng);
    }

    float projectileLaunchSpeed(const MWWorld::ESMStore& store, bool thrown, float strength)
    {
        const auto& gmst = store.get<ESM::GameSetting>();
        const float minimum = gmst.find(thrown ? "fThrownWeaponMinSpeed" : "fProjectileMinSpeed")->mValue.getFloat();
        const float maximum = gmst.find(thrown ? "fThrownWeaponMaxSpeed" : "fProjectileMaxSpeed")->mValue.getFloat();
        return minimum + (maximum - minimum) * strength;
    }

    osg::Vec3f advanceProjectileVelocity(const osg::Vec3f& velocity, float seconds)
    {
        return velocity - osg::Vec3f(0, 0, Constants::GravityConst * Constants::UnitsPerMeter * .1f) * seconds;
    }

    float projectileBaseDamage(const ESM::Weapon& weapon, const ESM::Weapon& ammunition, float strength)
    {
        const auto& bow = weapon.mData.mChop;
        const auto& arrow = ammunition.mData.mChop;
        // Stock thrown weapons contribute both terms, even though the records are identical.
        return (bow[0] + (bow[1] - bow[0]) * strength) + (arrow[0] + (arrow[1] - arrow[0]) * strength);
    }

    std::string_view chooseMeleeAttack(const ESM::Weapon* weapon, Misc::Rng::Generator& rng)
    {
        if (weapon)
        {
            // Preserve stock integer averages, closed roll and boundary ties.
            const int slash = (weapon->mData.mSlash[0] + weapon->mData.mSlash[1]) / 2;
            const int chop = (weapon->mData.mChop[0] + weapon->mData.mChop[1]) / 2;
            const int thrust = (weapon->mData.mThrust[0] + weapon->mData.mThrust[1]) / 2;
            const float roll = Misc::Rng::rollClosedProbability(rng) * (slash + chop + thrust);
            if (roll <= slash) return "slash";
            if (roll <= slash + thrust) return "thrust";
            return "chop";
        }
        const float roll = Misc::Rng::rollProbability(rng);
        if (roll >= 2 / 3.f) return "thrust";
        if (roll >= 1 / 3.f) return "slash";
        return "chop";
    }

    bool isNormalWeapon(const ESM::Weapon* weapon, bool enchantedWeaponsAreMagical)
    {
        return weapon && !(weapon->mData.mFlags & (ESM::Weapon::Silver | ESM::Weapon::Magical))
            && (weapon->mEnchant.empty() || !enchantedWeaponsAreMagical);
    }

    float applyNormalWeaponResistance(const CreatureStats& victim, float damage)
    {
        const auto& effects = victim.getMagicEffects();
        const float resistance = effects.getOrDefault(ESM::MagicEffect::ResistNormalWeapons).getMagnitude();
        const float weakness = effects.getOrDefault(ESM::MagicEffect::WeaknessToNormalWeapons).getMagnitude();
        return damage * (1.f - std::min(1.f, (resistance - weakness) / 100.f));
    }

    float applyKnockoutDamageMultiplier(const MWWorld::ESMStore& store,
        const CreatureStats& victim, float damage)
    {
        return victim.getKnockedDown()
            ? damage * store.get<ESM::GameSetting>().find("fCombatKODamageMult")->mValue.getFloat()
            : damage;
    }

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

    float getUnarmedFatigueDamage(const MWWorld::ESMStore& content, const CreatureStats& attacker,
        float handToHandSkill, float attackStrength)
    {
        const auto& store = content.get<ESM::GameSetting>();
        TES3MP::OpenMwMeleeSettings settings;
        settings.minimumHandToHandMultiplier = store.find("fMinHandToHandMult")->mValue.getFloat();
        settings.maximumHandToHandMultiplier = store.find("fMaxHandToHandMult")->mValue.getFloat();
        // This slice admits ordinary NPCs and the stock no-strength option.
        return TES3MP::openMwHandToHandDamage(settings, handToHandSkill,
            attacker.getAttribute(ESM::Attribute::Strength).getModified(), attackStrength,
            false, false, 1.f, false);
    }

    float getUnarmedHealthDamage(const MWWorld::ESMStore& content, const CreatureStats& attacker,
        float handToHandSkill, float attackStrength)
    {
        const auto& store = content.get<ESM::GameSetting>();
        TES3MP::OpenMwMeleeSettings settings;
        settings.minimumHandToHandMultiplier = store.find("fMinHandToHandMult")->mValue.getFloat();
        settings.maximumHandToHandMultiplier = store.find("fMaxHandToHandMult")->mValue.getFloat();
        settings.handToHandHealthPercent = store.find("fHandtoHandHealthPer")->mValue.getFloat();
        return TES3MP::openMwHandToHandDamage(settings, handToHandSkill,
            attacker.getAttribute(ESM::Attribute::Strength).getModified(), attackStrength,
            false, false, 1.f, true);
    }

    void restoreCombatFatigue(CreatureStats& actor, const MWWorld::ESMStore& content, float seconds)
    {
        auto fatigue = actor.getFatigue();
        if (fatigue.getCurrent() >= fatigue.getBase()) return;
        const auto& settings = content.get<ESM::GameSetting>();
        const float base = settings.find("fFatigueReturnBase")->mValue.getFloat();
        const float multiplier = settings.find("fFatigueReturnMult")->mValue.getFloat();
        const float endurance = actor.getAttribute(ESM::Attribute::Endurance).getModified();
        const float restored = fatigue.getCurrent() + seconds * (base + multiplier * endurance);
        if (!std::isfinite(restored) || std::abs(restored) > 1'000'000)
            throw std::invalid_argument("Native fatigue restoration invalid");
        fatigue.setCurrent(restored);
        actor.setFatigue(fatigue);
    }

    void restoreWaitRestStats(CreatureStats& actor, const MWWorld::ESMStore& content,
        double hours, bool sleep, float normalizedEncumbrance, double magickaHours)
    {
        if (!std::isfinite(hours) || hours < 0 || !std::isfinite(magickaHours) || magickaHours < 0
            || !std::isfinite(normalizedEncumbrance))
            throw std::invalid_argument("Invalid rest recovery input");
        const auto& settings = content.get<ESM::GameSetting>();
        if (sleep)
        {
            auto health = actor.getHealth();
            health.setCurrent(static_cast<float>(health.getCurrent()
                + 0.1 * actor.getAttribute(ESM::Attribute::Endurance).getModified() * hours));
            actor.setHealth(health);
            if (magickaHours > 0)
            {
                auto magicka = actor.getMagicka();
                magicka.setCurrent(static_cast<float>(magicka.getCurrent()
                    + settings.find("fRestMagicMult")->mValue.getFloat()
                        * actor.getAttribute(ESM::Attribute::Intelligence).getModified() * magickaHours));
                actor.setMagicka(magicka);
            }
        }
        auto fatigue = actor.getFatigue();
        if (fatigue.getCurrent() >= fatigue.getBase()) return;
        const float encumbrance = std::min(normalizedEncumbrance, 1.f);
        const float rate = (settings.find("fFatigueReturnBase")->mValue.getFloat()
                + settings.find("fFatigueReturnMult")->mValue.getFloat() * (1 - encumbrance))
            * settings.find("fEndFatigueMult")->mValue.getFloat()
            * actor.getAttribute(ESM::Attribute::Endurance).getModified();
        fatigue.setCurrent(static_cast<float>(fatigue.getCurrent() + 3600 * rate * hours));
        actor.setFatigue(fatigue);
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

    float resolveAttackStrength(float windUp, Misc::Rng::Generator& rng)
    {
        return windUp == -1.f ? std::min(1.f, 0.1f + Misc::Rng::rollClosedProbability(rng)) : windUp;
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
