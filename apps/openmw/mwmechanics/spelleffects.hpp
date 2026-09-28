#ifndef GAME_MWMECHANICS_SPELLEFFECTS_H
#define GAME_MWMECHANICS_SPELLEFFECTS_H

#include "activespells.hpp"
#include <components/misc/rng.hpp>
#include <optional>

// These functions should probably be split up into separate Lua functions for each magic effect when magic is
// dehardcoded. That way ESM::MGEF could point to two Lua scripts for each effect. Needs discussion.

namespace MWWorld
{
    class Ptr;
    class TimeStamp;
}

namespace MWMechanics
{
    class CreatureStats;
    class NpcStats;
    enum class EffectProtection { None, Reflect, Absorb };
    EffectProtection rollEffectProtection(ESM::RefId defense, float magnitude,
        bool canReflect, bool canAbsorb, Misc::Rng::Generator& rng);
    bool rollDispel(float magnitude, Misc::Rng::Generator& rng);
    struct DisintegratedCondition
    {
        int condition;
        float remainder;
    };
    // The stock fractional charge rule, shared with detached equipment candidates.
    DisintegratedCondition disintegrateCondition(int condition, float remainder, float magnitude);
    // Effect-specific cures remove matching applied effects, preserving source siblings.
    ESM::RefId curedEffect(ESM::RefId cure);
    void absorbDynamicStat(CreatureStats& target, CreatureStats* caster, int stat, float magnitude,
        const MWWorld::TimeStamp* deathTime = nullptr);
    float rollEffectMagnitude(float minimum, float maximum, Misc::Rng::Generator& rng);
    void modifyFortifySkill(NpcStats& stats, ESM::RefId skill, float magnitude);
    // Positive damage applies Drain; negative damage removes it. Stock Damage
    // and Restore callers use the same mutation after their own magnitude cap.
    void modifyAttributeDamage(CreatureStats& stats, ESM::RefId attribute, float magnitude,
        std::optional<float> baseMagickaMultiplier = {});
    void modifySkillDamage(NpcStats& stats, ESM::RefId skill, float magnitude);
    // Permanent Damage/Restore caps differ from temporary Drain/Absorb overlays.
    void applyAttributeDamage(CreatureStats& stats, ESM::RefId attribute, float magnitude,
        std::optional<float> baseMagickaMultiplier = {});
    void applySkillDamage(NpcStats& stats, ESM::RefId skill, float magnitude);
    // The stat mutation used by Restore Health. Explicit stats let a server
    // stage the same OpenMW effect before publishing it to a live actor.
    void restoreHealth(CreatureStats& stats, float magnitude);
    // Shared Restore Health/Magicka/Fatigue mutation for detached server stats
    // and the stock actor effect path. Index follows CreatureStats dynamic stats.
    void restoreDynamicStat(CreatureStats& stats, int index, float magnitude);
    void adjustDynamicStatValue(CreatureStats& stats, int index, float magnitude,
        bool allowDecreaseBelowZero = false, bool allowIncreaseAboveModified = false,
        const MWWorld::TimeStamp* deathTime = nullptr);
    // Shared attribute mutation; explicit stats also permit isolated preparation.
    void modifyFortifyAttribute(CreatureStats& stats, ESM::RefId attribute, float magnitude, bool affectsBase = false,
        std::optional<float> baseMagickaMultiplier = {});
    struct MagicApplicationResult
    {
        enum class Type
        {
            APPLIED,
            REMOVED,
            REFLECTED
        };
        Type mType;
        bool mShowHit;
        bool mShowHealth;
    };

    // Applies a tick of a single effect. Returns true if the effect should be removed immediately
    MagicApplicationResult applyMagicEffect(const MWWorld::Ptr& target, const MWWorld::Ptr& caster,
        ActiveSpells::ActiveSpellParams& spellParams, ESM::ActiveEffect& effect, float dt,
        bool playNonLoopingEffect = true);

    // Undoes permanent effects created by ESM::MagicEffect::AppliedOnce
    void onMagicEffectRemoved(
        const MWWorld::Ptr& target, ActiveSpells::ActiveSpellParams& spell, const ESM::ActiveEffect& effect);
}

#endif
