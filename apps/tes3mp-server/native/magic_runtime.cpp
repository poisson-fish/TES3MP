#include <apps/openmw/mwmechanics/summoning.hpp>
#include "magic_runtime.hpp"
#include <apps/openmw/mwmechanics/boundequipment.hpp>

#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/spelleffects.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwmechanics/spellresistance.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/misc/rng.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace TES3MP::Native
{
    bool wholeSourceCure(ESM::RefId id)
    {
        return id == ESM::MagicEffect::CureCommonDisease || id == ESM::MagicEffect::CureBlightDisease
            || id == ESM::MagicEffect::RemoveCurse;
    }

    std::optional<PreparedInstantEffects> preparePersistentEffects(const ESM::Spell& spell,
        const MWWorld::ESMStore& content, bool specialConditions, bool equipmentEffects, bool summonEffects)
    {
        if (spell.mData.mType != ESM::Spell::ST_Disease && spell.mData.mType != ESM::Spell::ST_Blight
            && spell.mData.mType != ESM::Spell::ST_Curse) return std::nullopt;
        if (spell.mEffects.mList.empty() || spell.mEffects.mList.size() > 8) return std::nullopt;
        auto normalized = spell.mEffects;
        bool selfCorprus = false;
        for (auto& entry : normalized.mList)
        {
            auto& effect = entry.mData;
            if (effect.mEffectID == ESM::MagicEffect::Corprus && effect.mRange == ESM::RT_Self)
                selfCorprus = true;
            // Store sources retain ordinals; the resolver applies only Self entries indefinitely.
            if (wholeSourceCure(effect.mEffectID) || !MWMechanics::curedEffect(effect.mEffectID).empty()
                || (!specialConditions && (effect.mEffectID == ESM::MagicEffect::Corprus
                    || effect.mEffectID == ESM::MagicEffect::Vampirism))
                || effect.mEffectID == ESM::MagicEffect::Dispel
                || effect.mEffectID == ESM::MagicEffect::AbsorbAttribute || effect.mEffectID == ESM::MagicEffect::AbsorbSkill
                || effect.mEffectID == ESM::MagicEffect::AbsorbHealth || effect.mEffectID == ESM::MagicEffect::AbsorbMagicka
                || effect.mEffectID == ESM::MagicEffect::AbsorbFatigue) return std::nullopt;
            effect.mArea = 0; effect.mDuration = 1;
        }
        if (MWMechanics::Spells::hasCorprusEffect(&spell) && !selfCorprus) return std::nullopt;
        return prepareInstantEffects(normalized, content, true, true, specialConditions, specialConditions, false, false, false, equipmentEffects, summonEffects);
    }
    bool permanentStatEffect(ESM::RefId id)
    {
        return id == ESM::MagicEffect::DamageAttribute || id == ESM::MagicEffect::RestoreAttribute
            || id == ESM::MagicEffect::DamageSkill || id == ESM::MagicEffect::RestoreSkill;
    }
    int fortifyDynamicStat(ESM::RefId id)
    {
        if (id == ESM::MagicEffect::FortifyHealth) return 0;
        if (id == ESM::MagicEffect::FortifyMagicka) return 1;
        if (id == ESM::MagicEffect::FortifyFatigue) return 2;
        return -1;
    }
    void applyPermanentStatEffect(MWMechanics::NpcStats& target, const ESM::ENAMstruct& effect,
        float magnitude, const MWWorld::ESMStore& content)
    {
        const bool restore = effect.mEffectID == ESM::MagicEffect::RestoreAttribute
            || effect.mEffectID == ESM::MagicEffect::RestoreSkill;
        if (!effect.mAttribute.empty())
            MWMechanics::applyAttributeDamage(target, effect.mAttribute, restore ? -magnitude : magnitude,
                content.get<ESM::GameSetting>().find("fNPCbaseMagickaMult")->mValue.getFloat());
        else MWMechanics::applySkillDamage(target, effect.mSkill, restore ? -magnitude : magnitude);
    }
    bool movementEffect(ESM::RefId id)
    {
        return id == ESM::MagicEffect::WaterBreathing || id == ESM::MagicEffect::SwiftSwim
            || id == ESM::MagicEffect::WaterWalking || id == ESM::MagicEffect::Burden
            || id == ESM::MagicEffect::Feather || id == ESM::MagicEffect::Jump
            || id == ESM::MagicEffect::Levitate || id == ESM::MagicEffect::SlowFall;
    }
    bool aiDispositionEffect(ESM::RefId id)
    {
        return id == ESM::MagicEffect::Charm || id == ESM::MagicEffect::CalmHumanoid
            || id == ESM::MagicEffect::CalmCreature || id == ESM::MagicEffect::FrenzyHumanoid
            || id == ESM::MagicEffect::FrenzyCreature || id == ESM::MagicEffect::DemoralizeHumanoid
            || id == ESM::MagicEffect::DemoralizeCreature || id == ESM::MagicEffect::RallyHumanoid
            || id == ESM::MagicEffect::RallyCreature || id == ESM::MagicEffect::CommandHumanoid
            || id == ESM::MagicEffect::CommandCreature || id == ESM::MagicEffect::TurnUndead;
    }
    bool expandedCombatEffect(ESM::RefId id)
    {
        return permanentStatEffect(id) || fortifyDynamicStat(id) >= 0 || wholeSourceCure(id)
            || id == ESM::MagicEffect::ResistCommonDisease || id == ESM::MagicEffect::ResistBlightDisease
            || id == ESM::MagicEffect::ResistCorprusDisease
            || id == ESM::MagicEffect::WeaknessToCommonDisease || id == ESM::MagicEffect::WeaknessToBlightDisease
            || id == ESM::MagicEffect::WeaknessToCorprusDisease || id == ESM::MagicEffect::CureCorprusDisease
            || id == ESM::MagicEffect::SunDamage
            || id == ESM::MagicEffect::FortifyMaximumMagicka
            || id == ESM::MagicEffect::FireShield || id == ESM::MagicEffect::LightningShield
            || id == ESM::MagicEffect::FrostShield
            || id == ESM::MagicEffect::Reflect || id == ESM::MagicEffect::SpellAbsorption
            || id == ESM::MagicEffect::Paralyze || id == ESM::MagicEffect::ResistParalysis
            || id == ESM::MagicEffect::CurePoison || id == ESM::MagicEffect::CureParalyzation
            || id == ESM::MagicEffect::Dispel || id == ESM::MagicEffect::DrainHealth
            || id == ESM::MagicEffect::DrainMagicka || id == ESM::MagicEffect::DrainFatigue
            || id == ESM::MagicEffect::AbsorbHealth || id == ESM::MagicEffect::AbsorbMagicka
            || id == ESM::MagicEffect::AbsorbFatigue || id == ESM::MagicEffect::AbsorbAttribute
            || id == ESM::MagicEffect::AbsorbSkill
            || id == ESM::MagicEffect::Silence || id == ESM::MagicEffect::Sound
            || id == ESM::MagicEffect::StuntedMagicka
            || id == ESM::MagicEffect::DisintegrateWeapon
            || id == ESM::MagicEffect::DisintegrateArmor
            || id == ESM::MagicEffect::Invisibility || id == ESM::MagicEffect::Chameleon
            || id == ESM::MagicEffect::Light || id == ESM::MagicEffect::NightEye
            || id == ESM::MagicEffect::DetectAnimal || id == ESM::MagicEffect::DetectEnchantment
            || id == ESM::MagicEffect::DetectKey;
    }
    bool supportedCombatModifier(ESM::RefId effect)
    {
        return effect == ESM::MagicEffect::Shield || effect == ESM::MagicEffect::Sanctuary
            || effect == ESM::MagicEffect::FortifyAttack || effect == ESM::MagicEffect::Blind;
    }
    namespace
    {
        bool supportedInstantEffect(const ESM::ENAMstruct& effect, bool actorLifecycle,
            bool expandedEffects = false, bool persistentSpecial = false, bool specialConditions = false,
            bool movementEffects = false, bool objectMagic = false, bool playerTravel = false, bool equipmentEffects = false, bool summonEffects = false)
        {
            if (!specialConditions && (effect.mEffectID == ESM::MagicEffect::SunDamage
                || effect.mEffectID == ESM::MagicEffect::ResistCorprusDisease
                || effect.mEffectID == ESM::MagicEffect::WeaknessToCorprusDisease
                || effect.mEffectID == ESM::MagicEffect::CureCorprusDisease)) return false;
            const bool prior = effect.mEffectID == ESM::MagicEffect::RestoreHealth
                || effect.mEffectID == ESM::MagicEffect::RestoreMagicka
                || effect.mEffectID == ESM::MagicEffect::RestoreFatigue
                || effect.mEffectID == ESM::MagicEffect::DamageHealth
                || effect.mEffectID == ESM::MagicEffect::DamageMagicka
                || effect.mEffectID == ESM::MagicEffect::DamageFatigue
                || effect.mEffectID == ESM::MagicEffect::ResistMagicka;
            return prior || (actorLifecycle && (((equipmentEffects && MWMechanics::equipmentMagicEffect(effect.mEffectID))
                || (summonEffects && MWMechanics::isSummoningEffect(effect.mEffectID)))
                || (objectMagic && (effect.mEffectID == ESM::MagicEffect::Lock
                || effect.mEffectID == ESM::MagicEffect::Open
                || effect.mEffectID == ESM::MagicEffect::Telekinesis || effect.mEffectID == ESM::MagicEffect::Soultrap))
                || (playerTravel && (effect.mEffectID == ESM::MagicEffect::Mark
                    || effect.mEffectID == ESM::MagicEffect::Recall
                    || effect.mEffectID == ESM::MagicEffect::DivineIntervention
                    || effect.mEffectID == ESM::MagicEffect::AlmsiviIntervention))
                || (expandedEffects && expandedCombatEffect(effect.mEffectID))
                || (movementEffects && movementEffect(effect.mEffectID))
                || (movementEffects && aiDispositionEffect(effect.mEffectID))
                || (persistentSpecial && (effect.mEffectID == ESM::MagicEffect::Corprus
                    || effect.mEffectID == ESM::MagicEffect::Vampirism))
                || supportedCombatModifier(effect.mEffectID)
                || effect.mEffectID == ESM::MagicEffect::FortifyAttribute
                || effect.mEffectID == ESM::MagicEffect::FortifySkill
                || effect.mEffectID == ESM::MagicEffect::DrainAttribute
                || effect.mEffectID == ESM::MagicEffect::DrainSkill
                || effect.mEffectID == ESM::MagicEffect::FireDamage
                || effect.mEffectID == ESM::MagicEffect::ShockDamage
                || effect.mEffectID == ESM::MagicEffect::FrostDamage
                || effect.mEffectID == ESM::MagicEffect::Poison
                || effect.mEffectID == ESM::MagicEffect::ResistNormalWeapons
                || effect.mEffectID == ESM::MagicEffect::WeaknessToNormalWeapons
                || effect.mEffectID == ESM::MagicEffect::ResistFire
                || effect.mEffectID == ESM::MagicEffect::ResistFrost
                || effect.mEffectID == ESM::MagicEffect::ResistShock
                || effect.mEffectID == ESM::MagicEffect::ResistPoison
                || effect.mEffectID == ESM::MagicEffect::WeaknessToFire
                || effect.mEffectID == ESM::MagicEffect::WeaknessToFrost
                || effect.mEffectID == ESM::MagicEffect::WeaknessToShock
                || effect.mEffectID == ESM::MagicEffect::WeaknessToPoison
                || effect.mEffectID == ESM::MagicEffect::WeaknessToMagicka));
        }

        void applyInstantEffect(const ESM::ENAMstruct& effect, MWMechanics::CreatureStats& target,
            Misc::Rng::Generator* rng, const MWWorld::ESMStore* content, bool uncappedDamageFatigue)
        {
            if (effect.mDuration) return; // Composed actor lifecycle owns every timed mutation.
            if (effect.mMagnMin != effect.mMagnMax && !rng)
                throw std::invalid_argument("Variable native effect requires composed RNG");
            const float magnitude = float(effect.mMagnMin + (effect.mMagnMin == effect.mMagnMax ? 0
                : Misc::Rng::rollDice(effect.mMagnMax - effect.mMagnMin + 1, *rng)));
            if (effect.mEffectID == ESM::MagicEffect::RestoreHealth)
                MWMechanics::restoreHealth(target, magnitude);
            else if (effect.mEffectID == ESM::MagicEffect::RestoreMagicka)
                MWMechanics::restoreDynamicStat(target, 1, magnitude);
            else if (effect.mEffectID == ESM::MagicEffect::RestoreFatigue)
                MWMechanics::restoreDynamicStat(target, 2, magnitude);
            else if (effect.mEffectID == ESM::MagicEffect::DamageHealth
                || effect.mEffectID == ESM::MagicEffect::DamageMagicka
                || effect.mEffectID == ESM::MagicEffect::DamageFatigue)
            {
                if (!rng || !content)
                    throw std::invalid_argument("Resistible native effect requires composed RNG and content");
                const float resistance = MWMechanics::getEffectResistance(effect.mEffectID,
                    target, 100.f, target.getFatigueTerm(*content), false, *rng);
                const float loss = magnitude * (1.f - resistance / 100.f);
                const MWWorld::TimeStamp deathTime{}; // Composed campaign records the death tick.
                if (effect.mEffectID == ESM::MagicEffect::DamageHealth)
                    MWMechanics::adjustDynamicStatValue(target, 0, -loss, false, false, &deathTime);
                else if (effect.mEffectID == ESM::MagicEffect::DamageMagicka)
                    MWMechanics::adjustDynamicStatValue(target, 1, -loss);
                else
                    MWMechanics::adjustDynamicStatValue(target, 2, -loss, uncappedDamageFatigue);
            }
            else if (effect.mEffectID != ESM::MagicEffect::ResistMagicka
                && effect.mEffectID != ESM::MagicEffect::Mark
                && effect.mEffectID != ESM::MagicEffect::Recall)
                throw std::invalid_argument("Unsupported native effect");
        }
    }

    bool PreparedInstantEffects::onlyRange(int range) const noexcept
    {
        return std::all_of(effects.begin(), effects.end(),
            [range](const auto& effect) { return effect.mRange == range; });
    }

    bool PreparedInstantEffects::hasRange(int range) const noexcept
    {
        return std::any_of(effects.begin(), effects.end(),
            [range](const auto& effect) { return effect.mRange == range; });
    }

    namespace
    {
        std::optional<PreparedInstantEffects> preparePassiveEffects(const ESM::EffectList& effects,
            const MWWorld::ESMStore& content, bool expandedEffects, bool specialConditions, bool movementEffects,
            bool aiEffects, bool objectEffects, bool equipmentEffects, bool summonEffects)
        {
            if (effects.mList.empty() || effects.mList.size() > 8) return std::nullopt;
            PreparedInstantEffects result;
            for (const auto& entry : effects.mList)
            {
                auto effect = entry.mData;
                // Stock ActiveSpells::addEffects ignores authored duration/area for
                // constant Self effects. Vanilla bound-item enchantments use 1s.
                if (equipmentEffects) { effect.mDuration = 0; effect.mArea = 0; }
                const auto* magic = content.get<ESM::MagicEffect>().search(effect.mEffectID);
                const bool attribute = effect.mEffectID == ESM::MagicEffect::FortifyAttribute;
                const bool skill = effect.mEffectID == ESM::MagicEffect::FortifySkill;
                const bool resistance = effect.mEffectID == ESM::MagicEffect::ResistMagicka
                    || effect.mEffectID == ESM::MagicEffect::ResistNormalWeapons
                    || effect.mEffectID == ESM::MagicEffect::ResistFire
                    || effect.mEffectID == ESM::MagicEffect::ResistFrost
                    || effect.mEffectID == ESM::MagicEffect::ResistShock
                    || effect.mEffectID == ESM::MagicEffect::ResistPoison
                    || (expandedEffects && (effect.mEffectID == ESM::MagicEffect::ResistCommonDisease
                    || effect.mEffectID == ESM::MagicEffect::ResistBlightDisease
                    || (specialConditions && effect.mEffectID == ESM::MagicEffect::ResistCorprusDisease)
                    || effect.mEffectID == ESM::MagicEffect::ResistParalysis
                    || effect.mEffectID == ESM::MagicEffect::Reflect
                    || effect.mEffectID == ESM::MagicEffect::SpellAbsorption
                    || effect.mEffectID == ESM::MagicEffect::FireShield
                    || effect.mEffectID == ESM::MagicEffect::LightningShield
                    || effect.mEffectID == ESM::MagicEffect::FrostShield
                    || fortifyDynamicStat(effect.mEffectID) >= 0
                    || effect.mEffectID == ESM::MagicEffect::Invisibility
                    || effect.mEffectID == ESM::MagicEffect::Chameleon
                    || effect.mEffectID == ESM::MagicEffect::Light
                    || effect.mEffectID == ESM::MagicEffect::NightEye
                    || effect.mEffectID == ESM::MagicEffect::DetectAnimal
                    || effect.mEffectID == ESM::MagicEffect::DetectEnchantment
                    || effect.mEffectID == ESM::MagicEffect::DetectKey
                    || (objectEffects && effect.mEffectID == ESM::MagicEffect::Telekinesis)
                    || effect.mEffectID == ESM::MagicEffect::FortifyMaximumMagicka
                    || (movementEffects && movementEffect(effect.mEffectID))));
                const bool ai = aiEffects && aiDispositionEffect(effect.mEffectID);
                if (!magic || (!attribute && !skill && !resistance && !ai
                        && !((equipmentEffects && MWMechanics::equipmentMagicEffect(effect.mEffectID))
                    || (summonEffects && MWMechanics::isSummoningEffect(effect.mEffectID)))
                        && !supportedCombatModifier(effect.mEffectID))
                    || ((magic->mData.mFlags & ESM::MagicEffect::Harmful) && !ai
                        && !(movementEffects && movementEffect(effect.mEffectID)))
                    || ((magic->mData.mFlags & ESM::MagicEffect::NoMagnitude)
                        && effect.mEffectID != ESM::MagicEffect::Invisibility
                        && effect.mEffectID != ESM::MagicEffect::WaterBreathing
                        && effect.mEffectID != ESM::MagicEffect::WaterWalking
                        && !((equipmentEffects && MWMechanics::equipmentMagicEffect(effect.mEffectID))
                    || (summonEffects && MWMechanics::isSummoningEffect(effect.mEffectID))))
                    || effect.mRange != ESM::RT_Self || effect.mArea != 0 || effect.mDuration != 0
                    || effect.mMagnMin < 0 || effect.mMagnMin > effect.mMagnMax || effect.mMagnMax > 1000
                    || (attribute ? ESM::Attribute::refIdToIndex(effect.mAttribute) < 0 : !effect.mAttribute.empty())
                    || (skill ? ESM::Skill::refIdToIndex(effect.mSkill) < 0 : !effect.mSkill.empty()))
                    return std::nullopt;
                result.effects.push_back(effect);
            }
            return result;
        }

    }

    std::optional<PreparedInstantEffects> prepareConstantEffects(ESM::RefId id,
        const MWWorld::ESMStore& content, bool expandedEffects, bool specialConditions, bool movementEffects,
        bool aiEffects, bool objectEffects, bool equipmentEffects, bool summonEffects)
    {
        const auto* source = content.get<ESM::Enchantment>().search(id);
        if (!source || source->mData.mType != ESM::Enchantment::ConstantEffect) return std::nullopt;
        return preparePassiveEffects(source->mEffects, content, expandedEffects, specialConditions,
            movementEffects, aiEffects, objectEffects, equipmentEffects, summonEffects);
    }

    std::optional<PreparedInstantEffects> preparePassiveActorEffects(const ESM::Spell& spell,
        const MWWorld::ESMStore& content, bool movementEffects, bool aiEffects, bool objectEffects,
        bool equipmentEffects, bool summonEffects)
    {
        if (spell.mData.mType != ESM::Spell::ST_Ability) return std::nullopt;
        return preparePassiveEffects(spell.mEffects, content, true, false, movementEffects, aiEffects,
            objectEffects, equipmentEffects, summonEffects);
    }

    std::optional<PreparedInstantEffects> prepareInstantEffects(const ESM::EffectList& effects,
        const MWWorld::ESMStore& content, bool actorLifecycle, bool expandedEffects,
        bool persistentSpecial, bool specialConditions, bool movementEffects, bool objectMagic, bool playerTravel, bool equipmentEffects, bool summonEffects)
    {
        if (effects.mList.empty() || effects.mList.size() > 8) return std::nullopt;
        PreparedInstantEffects result;
        for (const auto& entry : effects.mList)
        {
            const auto& effect = entry.mData;
            const auto* magic = content.get<ESM::MagicEffect>().search(effect.mEffectID);
            const bool attribute = actorLifecycle && (effect.mEffectID == ESM::MagicEffect::FortifyAttribute
                || effect.mEffectID == ESM::MagicEffect::DrainAttribute
                || (expandedEffects && (effect.mEffectID == ESM::MagicEffect::AbsorbAttribute
                    || effect.mEffectID == ESM::MagicEffect::DamageAttribute
                    || effect.mEffectID == ESM::MagicEffect::RestoreAttribute)));
            const bool skill = actorLifecycle && (effect.mEffectID == ESM::MagicEffect::FortifySkill
                || effect.mEffectID == ESM::MagicEffect::DrainSkill
                || (expandedEffects && (effect.mEffectID == ESM::MagicEffect::AbsorbSkill
                    || effect.mEffectID == ESM::MagicEffect::DamageSkill
                    || effect.mEffectID == ESM::MagicEffect::RestoreSkill)));
            if (!magic || !supportedInstantEffect(effect, actorLifecycle, expandedEffects,
                    persistentSpecial, specialConditions, movementEffects, objectMagic, playerTravel, equipmentEffects, summonEffects)
                || (attribute ? ESM::Attribute::refIdToIndex(effect.mAttribute) < 0 : !effect.mAttribute.empty())
                || (skill ? ESM::Skill::refIdToIndex(effect.mSkill) < 0 : !effect.mSkill.empty())
                || (effect.mRange != ESM::RT_Self && effect.mRange != ESM::RT_Touch
                    && effect.mRange != ESM::RT_Target)
                || effect.mArea < 0 || effect.mArea > 64
                || effect.mMagnMin < (expandedEffects && magic && (magic->mData.mFlags & ESM::MagicEffect::NoMagnitude) ? 0 : 1)
                || effect.mMagnMin > effect.mMagnMax || effect.mMagnMax > 1000
                || (!actorLifecycle && effect.mEffectID == ESM::MagicEffect::ResistMagicka
                    && effect.mMagnMin != effect.mMagnMax)
                || (actorLifecycle ? (effect.mDuration < 0 || effect.mDuration > 3600
                        || (!expandedEffects && effect.mDuration == 0 && (!supportedInstantEffect(effect, false)
                            || effect.mEffectID == ESM::MagicEffect::ResistMagicka)))
                    : (effect.mEffectID == ESM::MagicEffect::ResistMagicka
                        ? effect.mDuration < 1 || effect.mDuration > 3600 : effect.mDuration != 0))
                || ((magic->mData.mFlags & ESM::MagicEffect::NoDuration) && !(objectMagic
                    && (effect.mEffectID == ESM::MagicEffect::Lock || effect.mEffectID == ESM::MagicEffect::Open)
                    || playerTravel && (effect.mEffectID == ESM::MagicEffect::Mark
                        || effect.mEffectID == ESM::MagicEffect::Recall
                    || effect.mEffectID == ESM::MagicEffect::DivineIntervention
                    || effect.mEffectID == ESM::MagicEffect::AlmsiviIntervention)
                    || expandedEffects && (effect.mEffectID == ESM::MagicEffect::Dispel
                    || wholeSourceCure(effect.mEffectID) || !MWMechanics::curedEffect(effect.mEffectID).empty())
                    || (persistentSpecial && (effect.mEffectID == ESM::MagicEffect::Corprus
                        || effect.mEffectID == ESM::MagicEffect::Vampirism))))
                || (!actorLifecycle && effect.mEffectID != ESM::MagicEffect::ResistMagicka
                    && (magic->mData.mFlags & ESM::MagicEffect::AppliedOnce)))
                return std::nullopt;
            result.effects.push_back(effect);
        }
        return result;
    }

    std::optional<PreparedEnchantmentCast> prepareEnchantmentCast(const ESM::Enchantment& enchantment,
        const MWMechanics::NpcStats& caster, float charge, const MWWorld::ESMStore& content,
        bool actorLifecycle, bool expandedEffects, bool specialConditions, bool movementEffects, bool objectMagic,
        bool playerTravel, bool equipmentEffects, bool summonEffects)
    {
        if (enchantment.mData.mType != ESM::Enchantment::WhenUsed
            && enchantment.mData.mType != ESM::Enchantment::WhenStrikes
            && enchantment.mData.mType != ESM::Enchantment::CastOnce) return std::nullopt;
        auto effects = prepareInstantEffects(enchantment.mEffects, content, actorLifecycle, expandedEffects,
            false, specialConditions, movementEffects, objectMagic, playerTravel, equipmentEffects, summonEffects);
        if (!effects || !std::isfinite(charge) || (charge < 0 && charge != -1.f)) return std::nullopt;
        if (enchantment.mData.mType == ESM::Enchantment::CastOnce)
            return PreparedEnchantmentCast{std::move(*effects), charge, true, true};
        const float baseCost = MWMechanics::getEnchantmentCastCost(enchantment, content);
        const float skill = caster.getSkill(ESM::Skill::Enchant).getModified();
        if (!std::isfinite(baseCost) || baseCost < 0 || baseCost > 1'000'000
            || !std::isfinite(skill) || skill < 0 || skill > 1'000'000) return std::nullopt;
        const int cost = MWMechanics::getEffectiveEnchantmentCastCost(baseCost, skill);
        const int maximum = MWMechanics::getEnchantmentCharge(enchantment, content);
        const float available = charge == -1.f ? float(maximum) : charge;
        if (cost < 1 || maximum < 1 || maximum > 1'000'000 || available > maximum) return std::nullopt;
        const bool affordable = available >= cost;
        return PreparedEnchantmentCast{std::move(*effects), affordable ? available - cost : charge,
            affordable, false};
    }

    std::optional<PreparedInstantSpell> prepareInstantSpell(const ESM::Spell& spell,
        const MWWorld::ESMStore& content, bool actorLifecycle, bool expandedEffects, bool specialConditions,
        bool movementEffects, bool objectMagic, bool playerTravel, bool equipmentEffects, bool summonEffects)
    {
        if (spell.mData.mType != ESM::Spell::ST_Spell) return std::nullopt;
        auto effects = prepareInstantEffects(spell.mEffects, content, actorLifecycle, expandedEffects,
            false, specialConditions, movementEffects, objectMagic, playerTravel, equipmentEffects, summonEffects);
        if (!effects) return std::nullopt;
        PreparedInstantSpell result;
        result.effects = std::move(*effects);
        result.cost = MWMechanics::calcSpellCost(spell, content);
        if (result.cost < 0 || result.cost > 1000000) return std::nullopt;
        result.source = &spell;
        return result;
    }

    InstantSpellResult applyInstantEffects(const PreparedInstantEffects& effects, int range,
        MWMechanics::CreatureStats& target, Misc::Rng::Generator* rng, const MWWorld::ESMStore* content,
        bool uncappedDamageFatigue)
    {
        if (range != ESM::RT_Self && range != ESM::RT_Touch && range != ESM::RT_Target)
            throw std::invalid_argument("Native instant effect range invalid");
        for (const auto& effect : effects.effects)
        {
            if (effect.mRange != range) continue;
            if (effect.mMagnMin != effect.mMagnMax && !rng)
                throw std::invalid_argument("Variable native effect requires composed RNG");
            if ((effect.mEffectID == ESM::MagicEffect::DamageHealth
                    || effect.mEffectID == ESM::MagicEffect::DamageMagicka
                    || effect.mEffectID == ESM::MagicEffect::DamageFatigue) && (!rng || !content))
                throw std::invalid_argument("Resistible native effect requires composed RNG and content");
        }
        const float beforeHealth = target.getHealth().getCurrent();
        const float beforeMagicka = target.getMagicka().getCurrent();
        const float beforeFatigue = target.getFatigue().getCurrent();
        for (const auto& effect : effects.effects)
            if (effect.mRange == range) applyInstantEffect(effect, target, rng, content, uncappedDamageFatigue);
        return {target.getHealth().getCurrent() - beforeHealth,
            target.getMagicka().getCurrent() - beforeMagicka,
            target.getFatigue().getCurrent() - beforeFatigue};
    }

    InstantSpellLaunch launchInstantSpell(const PreparedInstantSpell& spell,
        MWMechanics::NpcStats& caster, const MWWorld::ESMStore& content, Misc::Rng::Generator& rng,
        bool uncappedDamageFatigue, bool resolveSelf)
    {
        if (!spell.source || caster.getHealth().getCurrent() <= 0
            || caster.getMagicka().getCurrent() < spell.cost)
            throw std::invalid_argument("Native spell became stale before tick composition");
        // Use the release-time overlay, including Silence/Sound acquired during
        // wind-up. Stock spell failure still pays; incapacitation is cancelled
        // independently by the composed tick before reaching this launch.
        const bool succeeded = Misc::Rng::roll0to99(rng)
            < MWMechanics::getSpellSuccessChance(*spell.source, caster, content, true, false);
        auto magicka = caster.getMagicka();
        magicka.setCurrent(magicka.getCurrent() - spell.cost);
        caster.setMagicka(magicka);
        auto result = succeeded && resolveSelf ? applyInstantEffects(spell.effects, ESM::RT_Self, caster, &rng, &content,
            uncappedDamageFatigue)
            : InstantSpellResult{};
        result.magicka -= float(spell.cost);
        return {succeeded, result};
    }

}
