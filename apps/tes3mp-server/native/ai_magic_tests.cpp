#include "ai_magic.hpp"
#include <apps/openmw/mwmechanics/spells.hpp>
#include "loadout.hpp"
#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/spellresistance.hpp>
#include <apps/openmw/mwmechanics/spelleffects.hpp>
#include <apps/openmw/mwmechanics/weaponpriority.hpp>
#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <components/esm3/loadweap.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/spellpriority.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/misc/rng.hpp>
#include <apps/openmw/mwworld/timestamp.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace TES3MP::Native;
    void require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }
    void near(float value, float expected, const char* message)
    { require(std::isfinite(value) && std::abs(value - expected) < .0001f, message); }
    ESM::RefId id(std::string_view name) { return ESM::RefId::stringRefId(name); }
    void setting(MWWorld::ESMStore& store, const char* name, float value)
    {
        ESM::GameSetting record;
        record.blank(); record.mId = id(name); record.mValue.setType(ESM::VT_Float);
        record.mValue.setFloat(value);
        if (store.get<ESM::GameSetting>().search(record.mId)) store.overrideRecord(record);
        else store.insertStatic(record);
    }
    void content(MWWorld::ESMStore& store)
    {
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            ESM::Attribute attribute;
            attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
            store.insertStatic(attribute);
        }
        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            ESM::Skill skill;
            skill.blank(); skill.mId = *ESM::Skill::indexToRefId(i).getIf<ESM::StringRefId>();
            store.insertStatic(skill);
        }
        setting(store, "fFatigueBase", 1.25f); setting(store, "fFatigueMult", .5f);
        setting(store, "fEffectCostMult", 1.f);
        setting(store, "fAIMagicSpellMult", 2.f); setting(store, "fAIRangeMagicSpellMult", 3.f);
        for (const auto effect : {ESM::MagicEffect::DamageHealth, ESM::MagicEffect::RestoreHealth,
                ESM::MagicEffect::RestoreMagicka, ESM::MagicEffect::RestoreFatigue,
                ESM::MagicEffect::FireDamage, ESM::MagicEffect::FrostDamage,
                ESM::MagicEffect::ResistMagicka, ESM::MagicEffect::WeaknessToFire,
                ESM::MagicEffect::Corprus, ESM::MagicEffect::Vampirism,
                ESM::MagicEffect::CureCorprusDisease, ESM::MagicEffect::SunDamage,
                ESM::MagicEffect::ResistCorprusDisease, ESM::MagicEffect::WeaknessToCorprusDisease})
        {
            ESM::MagicEffect record;
            record.blank(); record.mId = effect; record.mData.mSchool = ESM::Skill::Destruction;
            record.mData.mBaseCost = 10.f;
            record.mData.mFlags = effect == ESM::MagicEffect::DamageHealth
                    || effect == ESM::MagicEffect::FireDamage || effect == ESM::MagicEffect::FrostDamage
                    || effect == ESM::MagicEffect::WeaknessToFire ? ESM::MagicEffect::Harmful : 0;
            if (effect == ESM::MagicEffect::CureCorprusDisease)
                record.mData.mFlags = ESM::MagicEffect::NoMagnitude | ESM::MagicEffect::NoDuration;
            store.insertStatic(record);
        }
    }
    void initialize(MWMechanics::NpcStats& stats)
    {
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            MWMechanics::AttributeValue value; value.setBase(40.f);
            stats.setAttribute(ESM::Attribute::indexToRefId(i), value, 1.f);
        }
        for (int i = 0; i < ESM::Skill::Length; ++i)
            stats.getSkill(ESM::Skill::indexToRefId(i)).setBase(100.f);
        stats.setHealth(MWMechanics::DynamicStat<float>(100.f));
        stats.setMagicka(MWMechanics::DynamicStat<float>(100.f));
        stats.setFatigue(MWMechanics::DynamicStat<float>(100.f));
    }
    ESM::Spell spell(std::string_view name, ESM::RefId effect, int range, int magnitude = 10)
    {
        ESM::Spell result; result.blank(); result.mId = id(name);
        result.mData.mType = ESM::Spell::ST_Spell; result.mData.mCost = 5;
        result.mData.mFlags = ESM::Spell::F_Always;
        ESM::IndexedENAMstruct entry{};
        entry.mData.mEffectID = effect; entry.mData.mRange = range;
        entry.mData.mDuration = 1; entry.mData.mMagnMin = entry.mData.mMagnMax = magnitude;
        result.mEffects.mList.push_back(entry);
        return result;
    }
    void conditionRules()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), victim(store); initialize(caster); initialize(victim);
        for (bool poison : {true, false})
        {
            const auto cure = poison ? ESM::MagicEffect::CurePoison : ESM::MagicEffect::CureParalyzation;
            require(MWMechanics::curedEffect(cure) == (poison ? ESM::MagicEffect::Poison : ESM::MagicEffect::Paralyze),
                "Cure mapping changed");
            ESM::MagicEffect magic; magic.blank(); magic.mId = cure;
            magic.mData.mSchool = ESM::Skill::Restoration; magic.mData.mBaseCost = 1.f;
            magic.mData.mFlags = ESM::MagicEffect::NoDuration | ESM::MagicEffect::NoMagnitude;
            store.insertStatic(magic);
            auto source = spell("cure", cure, ESM::RT_Self, 0);
            source.mEffects.mList.front().mData.mDuration = 0;
            require(!prepareInstantSpell(source, store, true) && bool(prepareInstantSpell(source, store, true, true)),
                "Cure admission crossed campaign boundary or rejected stock zero magnitude/duration");
            AiMagicContext context{caster, &victim}; context.expandedEffects = true;
            const std::array sources{AiMagicSpell{&source}};
            require(!prepareAiMagicCast(context, sources, {}, store), "AI selected unnecessary cure");
            context.selfCures[poison ? 0 : 1] = 2;
            require(bool(prepareAiMagicCast(context, sources, {}, store)), "AI did not select useful cure");
            near(MWMechanics::rateCureEffect(2), 2002.f, "Stock cure priority changed");
        }
        require(MWMechanics::curedEffect(ESM::MagicEffect::Dispel).empty(), "Dispel treated as effect cure");
        for (int family = 0; family < 3; ++family)
        {
            auto disease = spell("disease", ESM::MagicEffect::DamageHealth, ESM::RT_Self);
            disease.mData.mType = family == 0 ? ESM::Spell::ST_Disease : ESM::Spell::ST_Blight;
            if (family == 2) disease.mEffects.mList.front().mData.mEffectID = ESM::MagicEffect::Corprus;
            const auto resistance = family == 0 ? ESM::MagicEffect::ResistCommonDisease
                : family == 1 ? ESM::MagicEffect::ResistBlightDisease : ESM::MagicEffect::ResistCorprusDisease;
            const auto weakness = family == 0 ? ESM::MagicEffect::WeaknessToCommonDisease
                : family == 1 ? ESM::MagicEffect::WeaknessToBlightDisease : ESM::MagicEffect::WeaknessToCorprusDisease;
            MWMechanics::MagicEffects effects;
            effects.add(MWMechanics::EffectKey(ESM::MagicEffect::ResistMagicka), MWMechanics::EffectParam(100.f));
            near(*MWMechanics::getDiseaseContactMultiplier(disease, effects), 1.f, "General magic resistance affected disease contact");
            effects.add(MWMechanics::EffectKey(resistance), MWMechanics::EffectParam(100.f));
            near(*MWMechanics::getDiseaseContactMultiplier(disease, effects), 0.f, "Disease immunity failed");
            effects.add(MWMechanics::EffectKey(weakness), MWMechanics::EffectParam(50.f));
            near(*MWMechanics::getDiseaseContactMultiplier(disease, effects), .5f, "Disease weakness did not offset resistance");
            effects.add(MWMechanics::EffectKey(weakness), MWMechanics::EffectParam(200.f));
            near(*MWMechanics::getDiseaseContactMultiplier(disease, effects), 2.5f, "Stock disease susceptibility was clamped");
            if (family == 2)
            {
                disease.mData.mType = ESM::Spell::ST_Disease;
                near(*MWMechanics::getDiseaseContactMultiplier(disease, effects), 2.5f, "Corprus did not override common disease type");
            }
            effects.add(MWMechanics::EffectKey(resistance), MWMechanics::EffectParam(300.f));
            near(*MWMechanics::getDiseaseContactMultiplier(disease, effects), -.5f, "Stock disease over-resistance was clamped");
            require(!prepareInstantSpell(disease, store, true, true), "Unowned disease lifecycle admitted as a cast");
        }
        for (const auto [type, cure] : {std::pair{ESM::Spell::ST_Disease, ESM::MagicEffect::CureCommonDisease},
                {ESM::Spell::ST_Blight, ESM::MagicEffect::CureBlightDisease}, {ESM::Spell::ST_Curse, ESM::MagicEffect::RemoveCurse}})
        {
            auto condition = spell("persistent", ESM::MagicEffect::DamageHealth, ESM::RT_Target);
            condition.mData.mType = type;
            require(MWMechanics::Spells::isRemovedByCure(condition, cure), "Whole-source cure rejected matching type");
            require(!MWMechanics::Spells::isRemovedByCure(condition, ESM::MagicEffect::CurePoison), "Selective cure removed source");
            const auto plan = preparePersistentEffects(condition, store);
            require(plan && plan->hasRange(ESM::RT_Target), "Persistent source lost range/ordinal filtering");
            condition.mEffects.mList.front().mData.mEffectID = ESM::MagicEffect::Corprus;
            require(!preparePersistentEffects(condition, store), "Corprus admitted without special lifecycle");
            condition.mEffects.mList.front().mData.mRange = ESM::RT_Self;
            require(bool(preparePersistentEffects(condition, store, true)), "Special Corprus source rejected");
            if (type == ESM::Spell::ST_Blight)
                require(!MWMechanics::Spells::isRemovedByCure(condition, cure), "Blight cure removed Corprus membership");
            condition.mEffects.mList.front().mData.mEffectID = ESM::MagicEffect::Vampirism;
            require(!preparePersistentEffects(condition, store), "Vampirism admitted without special lifecycle");
            require(bool(preparePersistentEffects(condition, store, true)), "Special Vampirism source rejected");
            condition.mEffects.mList.resize(9);
            require(!preparePersistentEffects(condition, store), "Oversized persistent source admitted");
        }
        const auto ordinary = spell("ordinary", ESM::MagicEffect::DamageHealth, ESM::RT_Self);
        require(!MWMechanics::getDiseaseContactMultiplier(ordinary, caster.getMagicEffects()), "Ordinary spell became contagious");
        auto cure = ordinary; cure.mEffects.mList.front().mData.mEffectID = ESM::MagicEffect::CureCorprusDisease;
        cure.mEffects.mList.front().mData.mMagnMin = cure.mEffects.mList.front().mData.mMagnMax = 0;
        cure.mEffects.mList.front().mData.mDuration = 0;
        require(!prepareInstantSpell(cure, store, true, true)
            && bool(prepareInstantSpell(cure, store, true, true, true)),
            "Corprus cure crossed the V55 admission boundary");
        for (const auto effect : {ESM::MagicEffect::ResistCorprusDisease,
                ESM::MagicEffect::WeaknessToCorprusDisease, ESM::MagicEffect::SunDamage})
        {
            auto cast = spell("special", effect, ESM::RT_Self);
            require(!prepareInstantSpell(cast, store, true, true)
                && bool(prepareInstantSpell(cast, store, true, true, true)),
                "Special condition effect crossed the V55 admission boundary");
        }
    }

    void expandedEffects()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), victim(store); initialize(caster); initialize(victim);
        for (const auto id : {ESM::MagicEffect::Reflect, ESM::MagicEffect::SpellAbsorption,
                ESM::MagicEffect::Paralyze, ESM::MagicEffect::ResistParalysis, ESM::MagicEffect::Dispel,
                ESM::MagicEffect::DrainHealth, ESM::MagicEffect::DrainMagicka, ESM::MagicEffect::DrainFatigue,
                ESM::MagicEffect::AbsorbHealth, ESM::MagicEffect::AbsorbMagicka, ESM::MagicEffect::AbsorbFatigue,
                ESM::MagicEffect::AbsorbAttribute, ESM::MagicEffect::AbsorbSkill})
        {
            ESM::MagicEffect effect; effect.blank(); effect.mId = id;
            effect.mData.mSchool = ESM::Skill::Mysticism; effect.mData.mBaseCost = 1.f;
            const bool defense = id == ESM::MagicEffect::Reflect || id == ESM::MagicEffect::SpellAbsorption
                || id == ESM::MagicEffect::ResistParalysis || id == ESM::MagicEffect::Dispel;
            effect.mData.mFlags = defense ? 0 : ESM::MagicEffect::Harmful;
            if (id == ESM::MagicEffect::Paralyze) effect.mData.mFlags |= ESM::MagicEffect::NoMagnitude;
            if (id == ESM::MagicEffect::Dispel) effect.mData.mFlags |= ESM::MagicEffect::NoDuration;
            if (id == ESM::MagicEffect::AbsorbAttribute) effect.mData.mFlags |= ESM::MagicEffect::TargetAttribute;
            if (id == ESM::MagicEffect::AbsorbSkill) effect.mData.mFlags |= ESM::MagicEffect::TargetSkill;
            store.insertStatic(effect);
            auto record = spell("expanded", id, defense ? ESM::RT_Self : ESM::RT_Touch, id == ESM::MagicEffect::Paralyze ? 0 : 10);
            auto& entry = record.mEffects.mList[0].mData;
            if (id == ESM::MagicEffect::Dispel) entry.mDuration = 0;
            if (id == ESM::MagicEffect::AbsorbAttribute) entry.mAttribute = ESM::Attribute::Strength;
            if (id == ESM::MagicEffect::AbsorbSkill) entry.mSkill = ESM::Skill::ShortBlade;
            require(!prepareInstantSpell(record, store, true), "Expanded effect leaked into older campaign");
            require(bool(prepareInstantSpell(record, store, true, true)), "Expanded spell not prepared");
            AiMagicContext context{caster, &victim}; context.expandedEffects = true;
            context.selfDispel = {0, 3};
            const std::array spells{AiMagicSpell{&record}};
            require(bool(prepareAiMagicCast(context, spells, {}, store)) == (!defense || id == ESM::MagicEffect::Dispel),
                ("Expanded NPC rating differs from stock: " + id.toDebugString()).c_str());
            if (id == ESM::MagicEffect::Paralyze)
            {
                caster.getMagicEffects().add(MWMechanics::EffectKey(id), MWMechanics::EffectParam(1.f));
                require(!prepareAiMagicCast(context, spells, {}, store), "Paralyzed NPC selected a spell");
                caster.getMagicEffects().add(MWMechanics::EffectKey(id), MWMechanics::EffectParam(-1.f));
            }
        }
        Misc::Rng::Generator rng{17}, unchanged{17};
        require(MWMechanics::rollEffectProtection(ESM::MagicEffect::Reflect, 100.f, false, false, rng)
            == MWMechanics::EffectProtection::None && Misc::Rng::serialize(rng) == Misc::Rng::serialize(unchanged),
            "Disabled protection consumed RNG");
        require(MWMechanics::rollEffectProtection(ESM::MagicEffect::Reflect, 100.f, true, true, rng)
            == MWMechanics::EffectProtection::Reflect, "Full reflection failed");
        require(MWMechanics::rollEffectProtection(ESM::MagicEffect::SpellAbsorption, 100.f, true, true, rng)
            == MWMechanics::EffectProtection::Absorb, "Full absorption failed");
        require(!MWMechanics::rollDispel(0.f, rng) && MWMechanics::rollDispel(100.f, rng), "Dispel boundary changed");
        for (int stat = 0; stat < 3; ++stat)
        {
            auto value = victim.getDynamic(stat); value.setCurrent(2.f); victim.setDynamic(stat, value);
            value = caster.getDynamic(stat); value.setCurrent(50.f); caster.setDynamic(stat, value);
            const MWWorld::TimeStamp deathTime{};
            MWMechanics::absorbDynamicStat(victim, &caster, stat, 10.f, &deathTime);
            near(victim.getDynamic(stat).getCurrent(), 0.f, "Absorb exceeded target floor");
            near(caster.getDynamic(stat).getCurrent(), 60.f, "Absorb did not credit full stock magnitude");
        }
    }

    void statDrains()
    {
        MWWorld::ESMStore store; content(store);
        setting(store, "fNPCbaseMagickaMult", 2.f);
        MWMechanics::NpcStats caster(store), victim(store); initialize(caster); initialize(victim);
        for (const bool attribute : {true, false})
        {
            const auto effect = attribute ? ESM::MagicEffect::DrainAttribute : ESM::MagicEffect::DrainSkill;
            ESM::MagicEffect record; record.blank(); record.mId = effect;
            record.mData.mFlags = ESM::MagicEffect::Harmful | ESM::MagicEffect::AppliedOnce;
            record.mData.mSchool = ESM::Skill::Destruction; record.mData.mBaseCost = 1.f;
            store.insertStatic(record);
            auto source = spell("drain", effect, ESM::RT_Touch, 30);
            auto& entry = source.mEffects.mList.front().mData;
            if (attribute) entry.mAttribute = ESM::Attribute::Strength;
            else entry.mSkill = ESM::Skill::ShortBlade;
            require(bool(prepareInstantSpell(source, store, true)), "Timed drain rejected");
            require(!prepareInstantSpell(source, store, false), "Drain entered legacy instant resolver");
            const std::array spells{AiMagicSpell{&source}};
            require(bool(prepareAiMagicCast({caster, &victim}, spells, {}, store)), "NPC failed to select drain");
            ESM::Enchantment enchantment; enchantment.blank(); enchantment.mId = effect;
            enchantment.mData.mCost = 1; enchantment.mData.mCharge = 20;
            enchantment.mEffects = source.mEffects;
            for (const int type : {ESM::Enchantment::WhenUsed, ESM::Enchantment::WhenStrikes, ESM::Enchantment::CastOnce})
            {
                enchantment.mData.mType = type;
                require(bool(prepareEnchantmentCast(enchantment, caster, -1.f, store, true)), "Drain enchantment rejected");
            }
            enchantment.mData.mType = ESM::Enchantment::WhenUsed; store.insertStatic(enchantment);
            const std::array items{AiMagicItem{{1, -1}, enchantment.mId, -1.f, true}};
            require(bool(prepareAiMagicCast({caster, &victim}, {}, items, store)), "NPC failed to select drain item");
            entry.mDuration = 0;
            require(!prepareInstantSpell(source, store, true), "Zero-duration drain admitted without lifecycle");
            entry.mDuration = 1;
            entry.mAttribute = {}; entry.mSkill = {};
            require(!prepareInstantSpell(source, store, true), "Drain with missing argument accepted");
            if (attribute) entry.mSkill = ESM::Skill::ShortBlade;
            else entry.mAttribute = ESM::Attribute::Strength;
            require(!prepareInstantSpell(source, store, true), "Drain with wrong argument kind accepted");
        }
        // Drain may exceed a stat's modified value; expiry must retain earlier damage.
        MWMechanics::modifyAttributeDamage(victim, ESM::Attribute::Strength, 7.f, 2.f);
        MWMechanics::modifyAttributeDamage(victim, ESM::Attribute::Strength, 90.f, 2.f);
        near(victim.getAttribute(ESM::Attribute::Strength).getModified(), 0, "Drain did not floor attribute");
        MWMechanics::modifyAttributeDamage(victim, ESM::Attribute::Strength, -90.f, 2.f);
        near(victim.getAttribute(ESM::Attribute::Strength).getModified(), 33, "Drain expiry erased attribute damage");
        MWMechanics::modifySkillDamage(victim, ESM::Skill::ShortBlade, 9.f);
        MWMechanics::modifySkillDamage(victim, ESM::Skill::ShortBlade, 120.f);
        near(victim.getSkill(ESM::Skill::ShortBlade).getModified(), 0, "Drain did not floor skill");
        MWMechanics::modifySkillDamage(victim, ESM::Skill::ShortBlade, -120.f);
        near(victim.getSkill(ESM::Skill::ShortBlade).getModified(), 91, "Drain expiry erased skill damage");
        ESM::ENAMstruct entry{}; entry.mEffectID = ESM::MagicEffect::DrainAttribute;
        entry.mAttribute = ESM::Attribute::Intelligence;
        near(*MWMechanics::rateStatDamageEffect(entry, &victim, &victim), .5f, "Stock attribute priority changed");
        entry.mEffectID = ESM::MagicEffect::DrainSkill; entry.mAttribute = {}; entry.mSkill = ESM::Skill::ShortBlade;
        near(*MWMechanics::rateStatDamageEffect(entry, &victim, nullptr), 0, "Creature rated Drain Skill");
    }

    void combatModifiers()
    {
        MWWorld::ESMStore store; content(store); setting(store, "fCombatInvisoMult", 1.f);
        MWMechanics::NpcStats caster(store), victim(store); initialize(caster); initialize(victim);
        const float ordinary = MWMechanics::getHitChance(store, caster, victim, 40, false, false);
        for (auto effect : {ESM::MagicEffect::Shield, ESM::MagicEffect::Sanctuary,
                ESM::MagicEffect::FortifyAttack, ESM::MagicEffect::Blind})
        {
            ESM::MagicEffect record; record.blank(); record.mId = effect;
            record.mData.mFlags = effect == ESM::MagicEffect::Blind ? ESM::MagicEffect::Harmful : 0;
            store.insertStatic(record);
            auto source = spell("modifier", effect, ESM::RT_Self, 17);
            require(bool(prepareInstantSpell(source, store, true)), "Timed combat modifier rejected");
            ESM::Enchantment constant; constant.blank(); constant.mId = effect;
            constant.mData.mType = ESM::Enchantment::ConstantEffect;
            constant.mEffects = source.mEffects; constant.mEffects.mList.front().mData.mDuration = 0;
            store.insertStatic(constant);
            require(bool(prepareConstantEffects(effect, store)) == (effect != ESM::MagicEffect::Blind),
                "Passive combat modifier eligibility changed");
            source.mEffects.mList.front().mData.mDuration = 0;
            require(!prepareInstantSpell(source, store, true), "Instant modifier silently lost its duration");
        }
        caster.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::FortifyAttack), MWMechanics::EffectParam(17));
        near(MWMechanics::getHitChance(store, caster, victim, 40, false, false), ordinary + 17, "Fortify Attack lost hit modifier");
        caster.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Blind), MWMechanics::EffectParam(5));
        victim.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Sanctuary), MWMechanics::EffectParam(7));
        near(MWMechanics::getHitChance(store, caster, victim, 40, false, false), ordinary + 5,
            "Blind and Sanctuary failed to compose with Fortify Attack");
    }

    void weapons()
    {
        MWWorld::ESMStore store; content(store);
        setting(store, "fDamageStrengthBase", .5f); setting(store, "fDamageStrengthMult", .01f);
        setting(store, "fAIMeleeWeaponMult", 1.f); setting(store, "fCombatInvisoMult", 1.f);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        ESM::Weapon weapon; weapon.blank(); weapon.mId = id("synthetic sword");
        weapon.mData.mType = ESM::Weapon::ShortBladeOneHand;
        weapon.mData.mHealth = 100; weapon.mData.mSpeed = 1.f;
        for (auto* range : {&weapon.mData.mChop, &weapon.mData.mSlash, &weapon.mData.mThrust})
            (*range)[0] = (*range)[1] = 10;
        near(MWMechanics::weaponRatingBaseDamage(weapon), 10.f, "Shared melee rating arithmetic changed");
        near(MWMechanics::weaponRatingScore(weapon, 10.f, 50.f, 2.f), 10.f, "Shared hit/speed scaling changed");
        AiMagicContext context{caster, &enemy};
        const auto healthy = rateAiWeapon(context, weapon, 100, -1.f, true, store);
        require(healthy && *healthy > 0.f, "Equipped weapon not rated");
        const auto worn = rateAiWeapon(context, weapon, 50, -1.f, true, store);
        require(worn && *worn < *healthy, "AI ignored weapon condition");
        near(*rateAiWeapon(context, weapon, 0, -1.f, true, store), 0.f, "Broken weapon competed");
        enemy.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::ResistNormalWeapons), MWMechanics::EffectParam(100.f));
        near(*rateAiWeapon(context, weapon, 100, -1.f, true, store), 0.f, "AI ignored normal resistance");
        enemy.getMagicEffects() = {};
        caster.getSkill(ESM::Skill::ShortBlade).setBase(0.f);
        near(*rateAiWeapon(context, weapon, 100, -1.f, true, store), 0.f, "Zero-skill weapon competed");
        caster.getSkill(ESM::Skill::ShortBlade).setBase(100.f);
        auto fire = spell("competing fire", ESM::MagicEffect::FireDamage, ESM::RT_Target);
        std::array<AiMagicSpell, 1> sources{{{&fire}}};
        const auto cast = prepareAiMagicCast(context, sources, {}, store);
        require(bool(cast), "Competition fixture has no cast");
        context.weaponRating = cast->rating;
        require(!prepareAiMagicCast(context, sources, {}, store), "Spell won a weapon tie");
        context.weaponRating -= .01f;
        require(bool(prepareAiMagicCast(context, sources, {}, store)), "Stronger spell lost to weapon");
        context.weaponRating = cast->rating + .01f;
        require(!prepareAiMagicCast(context, sources, {}, store), "Stronger weapon lost to spell");
        ESM::Enchantment enchantment; enchantment.blank(); enchantment.mId = id("competing item");
        enchantment.mData.mType = ESM::Enchantment::WhenUsed;
        enchantment.mData.mCost = 10; enchantment.mData.mCharge = 100; enchantment.mEffects = fire.mEffects;
        store.insertStatic(enchantment);
        std::array<AiMagicItem, 1> items{{{{41, -1}, enchantment.mId, -1.f, true}}};
        context.weaponRating = 0.f;
        const auto item = prepareAiMagicCast(context, sources, items, store);
        require(item && item->payment, "Item did not compete");
        context.weaponRating = item->rating;
        require(prepareAiMagicCast(context, sources, items, store)->payment.has_value(), "Weapon won an item tie");
        context.weaponRating += .01f;
        require(!prepareAiMagicCast(context, sources, items, store), "Weaker item beat weapon");
        enchantment.mData.mType = ESM::Enchantment::WhenStrikes; store.overrideRecord(enchantment);
        weapon.mEnchant = enchantment.mId;
        const auto charged = rateAiWeapon(context, weapon, 100, -1.f, true, store);
        const auto depleted = rateAiWeapon(context, weapon, 100, 0.f, true, store);
        require(charged && depleted && *charged > *depleted, "AI ignored usable strike charge");
        enchantment.mEffects.mList.back().mData.mEffectID = ESM::MagicEffect::Levitate;
        store.overrideRecord(enchantment);
        require(!rateAiWeapon(context, weapon, 100, -1.f, true, store), "Unsupported strike rated partially");
        weapon.mEnchant = {};
        weapon.mData.mType = ESM::Weapon::MarksmanBow;
        near(*rateAiWeapon(context, weapon, 100, -1.f, true, store), 0.f, "Bow without ammo competed");
        const auto bow = rateAiWeapon(context, weapon, 100, -1.f, true, store, 10.f);
        require(bow && *bow > *healthy, "Bow did not include arrow rating");
        near(*rateAiWeapon(context, weapon, 100, -1.f, true, store, 0.f, 10.f), 0.f,
            "Bow accepted bolts");
        weapon.mData.mType = ESM::Weapon::MarksmanCrossbow;
        near(*rateAiWeapon(context, weapon, 100, -1.f, true, store, 10.f), 0.f, "Crossbow accepted arrows");
        near(*rateAiWeapon(context, weapon, 100, -1.f, true, store, 0.f, 10.f), *bow, "Crossbow omitted bolts");
        weapon.mData.mType = ESM::Weapon::MarksmanThrown;
        near(*rateAiWeapon(context, weapon, 0, -1.f, true, store), *healthy * 2.f, "Thrown damage/condition changed");
        context.enemyUnderwater = true;
        near(*rateAiWeapon(context, weapon, 0, -1.f, true, store), 0.f, "Ranged targeted underwater enemy");
        context.enemyUnderwater = false;
        context.outsideEnemyReach = true;
        setting(store, "fAIRangeMeleeWeaponMult", 3.f);
        near(*rateAiWeapon(context, weapon, 0, -1.f, true, store), *healthy * 6.f, "Ranged distance multiplier omitted");
        context.outsideEnemyReach = false;
        weapon.mData.mType = ESM::Weapon::Arrow;
        weapon.mData.mSpeed = 10.f;
        near(*rateAiWeapon(context, weapon, 0, -1.f, true, store), *healthy, "Ammo speed/condition changed rating");
        weapon.mData.mSpeed = 1.f;
        weapon.mData.mType = ESM::Weapon::ShortBladeOneHand;
        for (auto* range : {&weapon.mData.mChop, &weapon.mData.mSlash, &weapon.mData.mThrust}) (*range)[0] = (*range)[1] = 0;
        near(MWMechanics::weaponRatingBaseDamage(weapon), 0.f, "Zero-damage weapon rating is not finite");
        setting(store, "fAIMeleeWeaponMult", 2.f);
        for (auto* range : {&weapon.mData.mChop, &weapon.mData.mSlash, &weapon.mData.mThrust}) (*range)[0] = (*range)[1] = 10;
        near(*rateAiWeapon(context, weapon, 100, -1.f, true, store), *healthy * 2.f, "AI cached another content rating");
        for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            context.weaponRating = invalid;
            bool rejected = false;
            try { prepareAiMagicCast(context, sources, items, store); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Malformed competition rating accepted");
        }
        near(caster.getMagicka().getCurrent(), 100.f, "Competition spent magicka");
        near(items[0].charge, -1.f, "Competition spent charge");
    }

    void selection()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto fire = spell("synthetic fire", ESM::MagicEffect::FireDamage, ESM::RT_Target);
        auto frost = spell("synthetic frost", ESM::MagicEffect::FrostDamage, ESM::RT_Target);
        auto heal = spell("synthetic heal", ESM::MagicEffect::RestoreHealth, ESM::RT_Self);
        auto ward = spell("synthetic ward", ESM::MagicEffect::ResistMagicka, ESM::RT_Self);
        std::array<AiMagicSpell, 4> spells{{{&fire}, {&frost}, {&heal}, {&ward}}};
        AiMagicContext context{caster, &enemy};
        auto selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == fire.mId, "Stable spell ordering changed");
        near(selected->rating, 30.f, "Target effect cost or AI GMST multiplier changed");
        MWMechanics::MagicEffects resistance;
        resistance.add(MWMechanics::EffectKey(ESM::MagicEffect::ResistFire), MWMechanics::EffectParam(100.f));
        enemy.getMagicEffects() = resistance;
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == frost.mId, "AI ignored target elemental resistance");
        spells[1].racialPower = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI selected a racial spell or useless ward/heal");
        spells[1].racialPower = false; spells[1].activeOnEnemy = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI refreshed an active target spell");
        spells[1].activeOnEnemy = false;
        context.casterUnderwater = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI launched Target magic underwater");
        context.casterUnderwater = false; context.enemyUnderwater = true;
        require(!prepareAiMagicCast(context, spells, {}, store), "AI targeted an underwater enemy");
        context.enemyUnderwater = false;
        auto health = caster.getHealth(); health.setCurrent(10.f); caster.setHealth(health);
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == heal.mId, "Injured AI did not prefer restoration");
        spells[2].activeOnSelf = true;
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == frost.mId, "Active self restoration did not suppress recast");
        spells[2].activeOnSelf = false;
        context.enemy = nullptr;
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == heal.mId, "Self restoration required an enemy");
        caster.setKnockedDown(true);
        require(!prepareAiMagicCast(context, spells, {}, store), "Knocked AI selected a cast");
        caster.setKnockedDown(false);

        ESM::Enchantment enchantment; enchantment.blank(); enchantment.mId = id("synthetic used");
        enchantment.mData.mType = ESM::Enchantment::WhenUsed;
        enchantment.mData.mCost = 10; enchantment.mData.mCharge = 100;
        enchantment.mEffects = heal.mEffects; store.insertStatic(enchantment);
        std::array<AiMagicItem, 2> items{{{{41, -1}, enchantment.mId, -1.f, true},
            {{42, -1}, enchantment.mId, -1.f, true}}};
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && selected->payment && selected->payment->instance == items[0].instance,
            "AI lost item preference, stable ordering or source instance");
        const auto payment = prepareEnchantmentCast(enchantment, caster, -1.f, store, true);
        near(selected->payment->after, payment->chargeAfter, "AI charge diverged from player preparation");
        near(items[0].charge, -1.f, "Selection mutated item charge");
        near(caster.getHealth().getCurrent(), 10.f, "Selection applied effects");
        near(caster.getMagicka().getCurrent(), 100.f, "Selection spent magicka");
        items[0].equipped = false; items[1].charge = 0.f;
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && !selected->payment, "Unequipped or depleted WhenUsed item selected");
        items[0].equipped = true; items[0].activeOnSelf = true;
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && !selected->payment, "AI refreshed active item self effects");
        items[0].activeOnSelf = false;
        MWMechanics::MagicEffects silence;
        silence.add(MWMechanics::EffectKey(ESM::MagicEffect::Silence), MWMechanics::EffectParam(1.f)); caster.getMagicEffects() = silence;
        selected = prepareAiMagicCast(context, spells, items, store);
        require(selected && selected->payment, "Silence incorrectly blocked enchanted item use");
        require(!prepareAiMagicCast(context, spells, {}, store), "Silenced AI selected a spell");
        caster.getMagicEffects() = {};

        // Changing content must change ratings; no function-static GMST cache.
        context.enemy = &enemy;
        spells[2].activeOnSelf = true;
        setting(store, "fAIRangeMagicSpellMult", 7.f);
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == frost.mId, "Changed content lost viable source");
        near(selected->rating, 70.f, "AI retained another content context's GMST");
    }
    void rejection()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto valid = spell("valid", ESM::MagicEffect::DamageHealth, ESM::RT_Target);
        auto malformed = valid; malformed.mId = id("mixed unsupported");
        auto unsupported = valid.mEffects.mList.front(); unsupported.mData.mEffectID = ESM::MagicEffect::Mark;
        malformed.mEffects.mList.push_back(unsupported);
        std::array<AiMagicSpell, 2> spells{{{&malformed}, {&valid}}};
        const AiMagicContext context{caster, &enemy};
        auto selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == valid.mId && selected->spell.effects.effects.size() == 1,
            "Unsupported mixed record was partially selected");
        malformed.mEffects = valid.mEffects;
        malformed.mData.mFlags = ESM::Spell::F_Autocalc;
        malformed.mEffects.mList.front().mData.mMagnMin = std::numeric_limits<int>::max();
        malformed.mEffects.mList.front().mData.mMagnMax = std::numeric_limits<int>::max();
        selected = prepareAiMagicCast(context, spells, {}, store);
        require(selected && selected->effectSource == valid.mId,
            "Malformed autocalc source bypassed effect bounds during restoration rating");
        auto magicka = caster.getMagicka(); magicka.setCurrent(4.f); caster.setMagicka(magicka);
        require(!prepareAiMagicCast(context, spells, {}, store), "Unaffordable spell selected");
        magicka.setCurrent(100.f); caster.setMagicka(magicka);
        const auto rejects = [&](auto candidates, auto items) {
            bool rejected = false;
            try { (void)prepareAiMagicCast(context, candidates, items, store); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Malformed AI source domain accepted");
        };
        std::array<AiMagicItem, 2> duplicate{{{{1, -1}, id("unused")}, {{1, -1}, id("unused")}}};
        rejects(std::span(spells), std::span(duplicate));
        duplicate[1].instance = {2, -1}; duplicate[1].charge = std::numeric_limits<float>::quiet_NaN();
        rejects(std::span(spells), std::span(duplicate));
        spells[1].record = nullptr;
        rejects(std::span(spells), std::span<const AiMagicItem>{});
        std::vector<AiMagicSpell> oversized(MaximumAiMagicSources + 1, AiMagicSpell{&valid});
        rejects(std::span(oversized), std::span<const AiMagicItem>{});
        near(caster.getMagicka().getCurrent(), 100.f, "Rejected selection mutated caster");
    }
    void effectFamilyRules()
    {
        MWWorld::ESMStore store; content(store);
        setting(store, "fElementalShieldMult", .1f); setting(store, "fNPCbaseMagickaMult", 1.f);
        MWMechanics::NpcStats attacker(store), victim(store); initialize(attacker); initialize(victim);
        const std::array shields{ESM::MagicEffect::FireShield, ESM::MagicEffect::LightningShield, ESM::MagicEffect::FrostShield};
        const std::array elements{ESM::MagicEffect::FireDamage, ESM::MagicEffect::ShockDamage, ESM::MagicEffect::FrostDamage};
        const std::array resist{ESM::MagicEffect::ResistFire, ESM::MagicEffect::ResistShock, ESM::MagicEffect::ResistFrost};
        const std::array weak{ESM::MagicEffect::WeaknessToFire, ESM::MagicEffect::WeaknessToShock, ESM::MagicEffect::WeaknessToFrost};
        for (size_t i = 0; i < shields.size(); ++i)
        {
            ESM::MagicEffect record; record.blank(); record.mId = shields[i]; store.insertStatic(record);
            auto cast = spell("shield", shields[i], ESM::RT_Self, 20);
            require(prepareInstantSpell(cast, store, true, true).has_value()
                && !prepareInstantSpell(cast, store, true, false), "Shield admission bypassed expanded domain");
            ESM::Enchantment constant; constant.blank(); constant.mId = id("shield_constant");
            constant.mData.mType = ESM::Enchantment::ConstantEffect; constant.mEffects = cast.mEffects;
            constant.mEffects.mList.front().mData.mDuration = 0;
            if (i) store.overrideRecord(constant); else store.insertStatic(constant);
            require(prepareConstantEffects(constant.mId, store, true).has_value(), "Shield constant rejected");
            victim.getMagicEffects() = {}; attacker.getMagicEffects() = {};
            victim.getMagicEffects().add(MWMechanics::EffectKey(shields[i]), MWMechanics::EffectParam(20.f));
            victim.getMagicEffects().add(MWMechanics::EffectKey(shields[i]), MWMechanics::EffectParam(30.f));
            near(MWMechanics::getEffectResistanceAttribute(elements[i], &victim.getMagicEffects()), 50.f, "Shield resistance failed to stack");
            for (float resistance : {-50.f, 0.f, 50.f, 150.f})
            {
                attacker.getMagicEffects() = {};
                attacker.getMagicEffects().add(MWMechanics::EffectKey(resistance < 0 ? weak[i] : resist[i]),
                    MWMechanics::EffectParam(std::abs(resistance)));
                Misc::Rng::Generator rng{123}, expectedRng{123};
                const float save = 100.f + .2f * 40.f + .1f * 40.f;
                const float protection = std::min(100.f, std::max(0.f, save * 1.25f - Misc::Rng::roll0to99(expectedRng)) + resistance);
                const auto damage = MWMechanics::elementalShieldDamage(store, attacker, 100.f, victim, rng);
                near(damage[i].value(), 5.f * (1.f - .01f * protection), "Shield save/resistance formula changed");
                require(Misc::Rng::serialize(rng) == Misc::Rng::serialize(expectedRng), "Shield consumed wrong RNG count");
                for (size_t j = 0; j < shields.size(); ++j) if (j != i) require(!damage[j], "Shield damaged unrelated element");
            }
        }
        victim.getMagicEffects() = {};
        Misc::Rng::Generator rng{123}; const auto before = Misc::Rng::serialize(rng);
        MWMechanics::elementalShieldDamage(store, attacker, 0.f, victim, rng);
        require(Misc::Rng::serialize(rng) == before, "Absent shields rolled RNG");
        MWMechanics::applyAttributeDamage(victim, ESM::Attribute::Intelligence, 100.f, 1.f);
        near(victim.getAttribute(ESM::Attribute::Intelligence).getDamage(), 40.f, "Attribute damage exceeded modified value");
        MWMechanics::applyAttributeDamage(victim, ESM::Attribute::Intelligence, -100.f, 1.f);
        near(victim.getAttribute(ESM::Attribute::Intelligence).getDamage(), 0.f, "Attribute restore exceeded damage");
        MWMechanics::applySkillDamage(victim, ESM::Skill::ShortBlade, 200.f);
        near(victim.getSkill(ESM::Skill::ShortBlade).getDamage(), 100.f, "Skill damage exceeded modified value");
        MWMechanics::applySkillDamage(victim, ESM::Skill::ShortBlade, -200.f);
        near(victim.getSkill(ESM::Skill::ShortBlade).getDamage(), 0.f, "Skill restore exceeded damage");
    }
    void interference()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store); initialize(caster);
        auto ordinary = spell("ordinary", ESM::MagicEffect::RestoreHealth, ESM::RT_Self);
        ordinary.mData.mFlags = 0;
        auto always = ordinary; always.mData.mFlags = ESM::Spell::F_Always;
        for (auto effect : {ESM::MagicEffect::Silence, ESM::MagicEffect::Sound})
        {
            ESM::MagicEffect record; record.blank(); record.mId = effect;
            record.mData.mSchool = ESM::Skill::Illusion; record.mData.mBaseCost = 10.f;
            record.mData.mFlags = ESM::MagicEffect::Harmful;
            if (effect == ESM::MagicEffect::Silence) record.mData.mFlags |= ESM::MagicEffect::NoMagnitude;
            store.insertStatic(record);
            auto interference = spell("interference", effect, ESM::RT_Touch,
                effect == ESM::MagicEffect::Silence ? 0 : 1000);
            require(!prepareInstantSpell(interference, store, true)
                && prepareInstantSpell(interference, store, true, true), "Interference admission lost version/NoMagnitude rules");
            caster.getMagicEffects() = {};
            near(MWMechanics::getSpellSuccessChance(ordinary, caster, store), 100.f, "Control spell is not certain");
            caster.getMagicEffects().add(MWMechanics::EffectKey(effect),
                MWMechanics::EffectParam(effect == ESM::MagicEffect::Silence ? 1.f : 1000.f));
            near(MWMechanics::getSpellSuccessChance(ordinary, caster, store), 0.f, "Interference did not prevent ordinary success");
            near(MWMechanics::getSpellSuccessChance(always, caster, store),
                effect == ESM::MagicEffect::Silence ? 0.f : 100.f, "Always flag ignored stock interference precedence");
            auto prepared = prepareInstantSpell(ordinary, store, true, true);
            Misc::Rng::Generator rng{17}, expected{17};
            Misc::Rng::roll0to99(expected);
            const auto before = caster.getMagicka().getCurrent();
            const auto result = launchInstantSpell(*prepared, caster, store, rng, false, false);
            require(!result.succeeded && Misc::Rng::serialize(rng) == Misc::Rng::serialize(expected),
                "Failed spell changed stock success-roll consumption");
            near(caster.getMagicka().getCurrent(), before - prepared->cost, "Failed spell was not charged once");
            ESM::Enchantment item; item.blank(); item.mEffects = ordinary.mEffects;
            item.mData.mType = ESM::Enchantment::WhenUsed; item.mData.mCost = 5; item.mData.mCharge = 50;
            require(prepareEnchantmentCast(item, caster, -1.f, store, true, true)->affordable,
                "Interference blocked enchanted item");
            caster.getMagicEffects() = {};
            MWMechanics::NpcStats enemy(store); initialize(enemy);
            const std::array spells{AiMagicSpell{&interference}};
            AiMagicContext context{caster, &enemy}; context.expandedEffects = true;
            require(!prepareAiMagicCast(context, spells, {}, store), "AI targeted a non-caster with interference");
            enemy.setDrawState(MWMechanics::DrawState::Spell);
            require(bool(prepareAiMagicCast(context, spells, {}, store)), "AI skipped interference against a caster");
            enemy.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Silence), MWMechanics::EffectParam(1.f));
            near(*MWMechanics::rateCastingInterferenceEffect(interference.mEffects.mList[0].mData, &enemy, false),
                effect == ESM::MagicEffect::Silence ? 1.f : 0.f, "AI ignored stock Sound redundancy rule");
            require(!prepareAiMagicCast(context, spells, {}, store), "AI ignored redundant Sound or active NoMagnitude effect");
            enemy.getMagicEffects() = {};
            enemy.setKnockedDown(true);
            require(!prepareAiMagicCast(context, spells, {}, store), "AI targeted knocked-down caster with interference");
            enemy.setKnockedDown(false);
            enemy.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Paralyze), MWMechanics::EffectParam(1.f));
            require(!prepareAiMagicCast(context, spells, {}, store), "AI targeted paralyzed caster with interference");
            context.enemy = nullptr;
            require(!prepareAiMagicCast(context, spells, {}, store), "AI interference invented an enemy");
        }
        // Sound is subtracted before the fatigue multiplier and final cap.
        caster.getMagicEffects() = {};
        caster.getSkill(ESM::Skill::Destruction).setBase(25.f);
        auto fatigue = caster.getFatigue(); fatigue.setCurrent(50.f); caster.setFatigue(fatigue);
        const float before = MWMechanics::getSpellSuccessChance(ordinary, caster, store, false, false);
        caster.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Sound), MWMechanics::EffectParam(10.f));
        caster.getMagicEffects().add(MWMechanics::EffectKey(ESM::MagicEffect::Sound), MWMechanics::EffectParam(5.f));
        near(MWMechanics::getSpellSuccessChance(ordinary, caster, store, false, false),
            before - 15.f * caster.getFatigueTerm(store), "Stacked Sound lost stock fatigue scaling");
    }

    void launch()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto source = spell("launch source", ESM::MagicEffect::DamageHealth, ESM::RT_Target);
        const std::array spells{AiMagicSpell{&source}};
        const auto selected = prepareAiMagicCast({caster, &enemy}, spells, {}, store);
        require(bool(selected), "AI launch plan absent");
        Misc::Rng::Generator rng{17};
        const auto before = Misc::Rng::serialize(rng);
        const auto first = launchInstantSpell(selected->spell, caster, store, rng, false, false);
        require(first.succeeded, "Always-success AI source failed shared launch");
        near(caster.getMagicka().getCurrent(), 95.f, "AI launch did not spend player spell cost");
        near(enemy.getHealth().getCurrent(), 100.f, "AI launch applied Target effects before contact");
        require(Misc::Rng::serialize(rng) != before, "AI launch did not use explicit RNG");
        source.mData.mFlags = 0;
        caster.getSkill(ESM::Skill::Destruction).setBase(0.f);
        source.mData.mCost = 100;
        auto magicka = caster.getMagicka(); magicka.setCurrent(100.f); caster.setMagicka(magicka);
        const auto prepared = prepareInstantSpell(source, store, true);
        const auto failed = launchInstantSpell(*prepared, caster, store, rng, false, false);
        require(!failed.succeeded, "Impossible spell chance unexpectedly succeeded");
        near(caster.getMagicka().getCurrent(), 0.f, "Failed cast did not retain shared prescribed cost");
    }

    void items()
    {
        MWWorld::ESMStore store; content(store);
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto attack = spell("mixed source", ESM::MagicEffect::FireDamage, ESM::RT_Target);
        auto weakness = spell("weakness", ESM::MagicEffect::WeaknessToFire, ESM::RT_Target);
        attack.mEffects.mList.insert(attack.mEffects.mList.begin(), weakness.mEffects.mList.front());
        ESM::Enchantment enchantment; enchantment.blank(); enchantment.mId = id("ordered enchanted source");
        enchantment.mData.mType = ESM::Enchantment::WhenUsed;
        enchantment.mData.mCost = 10; enchantment.mData.mCharge = 100; enchantment.mEffects = attack.mEffects;
        store.insertStatic(enchantment);
        std::array sources{AiMagicSpell{&attack}};
        std::array items{AiMagicItem{{7, -1}, enchantment.mId, -1.f, true}};
        const AiMagicContext context{caster, &enemy};
        auto selected = prepareAiMagicCast(context, sources, items, store);
        require(selected && selected->payment, "Target WhenUsed did not beat the equivalent spell");
        require(selected->spell.effects.effects.size() == 2
                && selected->spell.effects.effects[0].mEffectID == ESM::MagicEffect::WeaknessToFire
                && selected->spell.effects.effects[1].mEffectID == ESM::MagicEffect::FireDamage,
            "AI preparation reordered or merged weakness and damage");
        items[0].enemyDuration = 3.01f;
        selected = prepareAiMagicCast(context, sources, items, store);
        require(selected && !selected->payment, "AI refreshed a long-running item target effect");
        items[0].enemyDuration = 3.f;
        selected = prepareAiMagicCast(context, sources, items, store);
        require(selected && selected->payment, "AI lost stock three-second item refresh boundary");
        sources[0].activeOnEnemy = true;
        items[0].enemyDuration = 4.f;
        require(!prepareAiMagicCast(context, sources, items, store), "Both active sources remained selectable");
        sources[0].activeOnEnemy = false; items[0].enemyDuration = 0.f;
        caster.getSkill(ESM::Skill::Enchant).setBase(0.f);
        const auto unskilled = prepareAiMagicCast(context, {}, items, store);
        caster.getSkill(ESM::Skill::Enchant).setBase(100.f);
        const auto skilled = prepareAiMagicCast(context, {}, items, store);
        require(unskilled && skilled && unskilled->payment->after < skilled->payment->after,
            "Item preparation ignored the selected NPC's Enchant skill");
        // Charge is a proposed payment. Re-preparation cannot spend it, including
        // when a caller discards a candidate after a failed persistence attempt.
        const auto retry = prepareAiMagicCast(context, {}, items, store);
        require(retry && retry->payment->before == skilled->payment->before
                && retry->payment->after == skilled->payment->after,
            "Discard/retry changed the selected item's proposed payment");
        auto strike = enchantment; strike.mData.mType = ESM::Enchantment::WhenStrikes;
        store.overrideRecord(strike);
        require(!prepareAiMagicCast(context, {}, items, store), "WhenStrikes item entered AI use selection");
        auto once = enchantment; once.mData.mType = ESM::Enchantment::CastOnce;
        store.overrideRecord(once);
        require(!prepareAiMagicCast(context, {}, items, store), "Unimplemented AI CastOnce selection was enabled");
    }

    void records(const char* directory)
    {
        const std::string config = std::string("--config=") + directory;
        const char* args[]{"native-ai-records", config.c_str()};
        Loadout loadout(readLoadoutOptions(2, args));
        const auto& store = loadout.store();
        MWMechanics::NpcStats caster(store), enemy(store); initialize(caster); initialize(enemy);
        auto health = caster.getHealth(); health.setCurrent(10.f); caster.setHealth(health);
        caster.setMagicka(MWMechanics::DynamicStat<float>(100000.f));
        const AiMagicContext context{caster, &enemy};
        size_t spells = 0, items = 0, trSpells = 0, trItems = 0, mixed = 0;
        for (const auto& source : store.get<ESM::Spell>())
        {
            const std::array candidates{AiMagicSpell{&source}};
            const auto selected = prepareAiMagicCast(context, candidates, {}, store);
            if (!selected) continue;
            require(!selected->payment && selected->spell.source == &source
                    && selected->spell.effects.effects.size() == source.mEffects.mList.size(),
                "Real spell selection lost source or effects");
            near(float(selected->spell.cost), float(MWMechanics::calcSpellCost(source, store)),
                "Real spell preparation diverged from engine cost");
            ++spells;
            if (source.mId.is<ESM::StringRefId>() && source.mId.getRefIdString().starts_with("T_")) ++trSpells;
            if (source.mEffects.mList.size() > 1) ++mixed;
        }
        for (const auto& source : store.get<ESM::Enchantment>())
        {
            if (source.mData.mType != ESM::Enchantment::WhenUsed) continue;
            const std::array candidates{AiMagicItem{{1, -1}, source.mId, -1.f, true}};
            const auto selected = prepareAiMagicCast(context, {}, candidates, store);
            if (!selected) continue;
            const auto payment = prepareEnchantmentCast(source, caster, -1.f, store, true);
            require(payment && selected->payment && selected->effectSource == source.mId
                    && selected->spell.effects.effects.size() == source.mEffects.mList.size(),
                "Real WhenUsed preparation lost source or effects");
            near(selected->payment->after, payment->chargeAfter, "Real item preparation diverged from player charge");
            ++items;
            if (source.mId.is<ESM::StringRefId>() && source.mId.getRefIdString().starts_with("T_")) ++trItems;
            if (source.mEffects.mList.size() > 1) ++mixed;
        }
        require(spells && items && mixed, "Real loadout lacks varied supported AI selections");
        std::cout << "Real records; synthetic caster/equipment/activity: spells=" << spells << " WhenUsed=" << items
            << " TR spells=" << trSpells << " TR WhenUsed=" << trItems << " mixed=" << mixed
            << "; selection/preparation only, no live network outcomes\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "records")
        { records(argv[2]); std::cout << "PASS records\n"; return 0; }
        if (argc != 2) throw std::invalid_argument("Select weapons, selection, rejection, launch, items or records <config>");
        const std::string_view filter = argv[1];
        if (filter == "condition-rules") conditionRules();
        else if (filter == "effect-family-rules") effectFamilyRules();
        else if (filter == "interference") interference();
        else if (filter == "weapons") weapons();
        else if (filter == "expanded-effects") expandedEffects();
        else if (filter == "stat-drains") statDrains();
        else if (filter == "combat-modifiers") combatModifiers();
        else if (filter == "selection") selection();
        else if (filter == "rejection") rejection();
        else if (filter == "launch") launch();
        else if (filter == "items") items();
        else throw std::invalid_argument("Unknown AI magic filter");
        std::cout << "PASS " << filter << " (synthetic records; shared OpenMW rules; no Environment)\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << "FAIL native AI magic: " << error.what() << '\n'; return 1; }
}
