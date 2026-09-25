#include "spellutil.hpp"

#include <limits>

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "actorutil.hpp"
#include "creaturestats.hpp"
#include "npcstats.hpp"

namespace MWMechanics
{
    namespace
    {
        template <class Skill>
        float spellBaseChance(const ESM::Spell& spell, const MWWorld::ESMStore& store,
            const CreatureStats& stats, Skill skill, ESM::RefId* effectiveSchool)
        {
            float y = std::numeric_limits<float>::max();
            float lowestSkill = 0;
            for (const auto& effect : spell.mEffects.mList)
            {
                const auto* magic = store.get<ESM::MagicEffect>().find(effect.mData.mEffectID);
                float x = float(effect.mData.mDuration);
                if (!(magic->mData.mFlags & ESM::MagicEffect::AppliedOnce)) x = std::max(1.f, x);
                x *= 0.1f * magic->mData.mBaseCost;
                x *= 0.5f * (effect.mData.mMagnMin + effect.mData.mMagnMax);
                x += effect.mData.mArea * 0.05f * magic->mData.mBaseCost;
                if (effect.mData.mRange == ESM::RT_Target) x *= 1.5f;
                x *= store.get<ESM::GameSetting>().find("fEffectCostMult")->mValue.getFloat();
                const float s = 2.f * skill(magic->mData.mSchool);
                if (s - x < y)
                {
                    y = s - x;
                    if (effectiveSchool) *effectiveSchool = magic->mData.mSchool;
                    lowestSkill = s;
                }
            }
            return lowestSkill - calcSpellCost(spell, store)
                + 0.2f * stats.getAttribute(ESM::Attribute::Willpower).getModified()
                + 0.1f * stats.getAttribute(ESM::Attribute::Luck).getModified();
        }

        float spellChance(const ESM::Spell& spell, const MWWorld::ESMStore& store,
            const CreatureStats& stats, float base, bool cap, bool checkMagicka)
        {
            if (stats.getMagicEffects().getOrDefault(ESM::MagicEffect::Silence).getMagnitude()) return 0.f;
            if (spell.mData.mType != ESM::Spell::ST_Spell) return 100.f;
            if (checkMagicka && calcSpellCost(spell, store) > 0
                && stats.getMagicka().getCurrent() < calcSpellCost(spell, store)) return 0.f;
            if (spell.mData.mFlags & ESM::Spell::F_Always) return 100.f;
            const float chance = (base - stats.getMagicEffects()
                .getOrDefault(ESM::MagicEffect::Sound).getMagnitude()) * stats.getFatigueTerm(store);
            return cap ? std::clamp(chance, 0.f, 100.f) : std::max(chance, 0.f);
        }

        float getTotalCost(const ESM::EffectList& list, const EffectCostMethod method = EffectCostMethod::GameSpell)
        {
            return MWMechanics::getTotalCost(list, *MWBase::Environment::get().getESMStore(), method);
        }
    }

    float getTotalCost(const ESM::EffectList& list, const MWWorld::ESMStore& store, const EffectCostMethod method)
    {
        float cost = 0;

        for (const ESM::IndexedENAMstruct& effect : list.mList)
        {
            float effectCost = std::max(0.f, MWMechanics::calcEffectCost(effect.mData, store, nullptr, method));

            // This is applied to the whole spell cost for each effect when
            // creating spells, but is only applied on the effect itself in TES:CS.
            if (effect.mData.mRange == ESM::RT_Target)
                effectCost *= 1.5;

            cost += effectCost;
        }
        return cost;
    }

    float calcEffectCost(
        const ESM::ENAMstruct& effect, const ESM::MagicEffect* magicEffect, const EffectCostMethod method)
    {
        return calcEffectCost(effect, *MWBase::Environment::get().getESMStore(), magicEffect, method);
    }

    float calcEffectCost(const ESM::ENAMstruct& effect, const MWWorld::ESMStore& store,
        const ESM::MagicEffect* magicEffect, const EffectCostMethod method)
    {
        if (!magicEffect)
            magicEffect = store.get<ESM::MagicEffect>().find(effect.mEffectID);
        bool hasMagnitude = !(magicEffect->mData.mFlags & ESM::MagicEffect::NoMagnitude);
        bool hasDuration = !(magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration);
        bool appliedOnce = magicEffect->mData.mFlags & ESM::MagicEffect::AppliedOnce;
        int minMagn = hasMagnitude ? effect.mMagnMin : 1;
        int maxMagn = hasMagnitude ? effect.mMagnMax : 1;
        if (method == EffectCostMethod::PlayerSpell || method == EffectCostMethod::GameSpell)
        {
            minMagn = std::max(1, minMagn);
            maxMagn = std::max(1, maxMagn);
        }
        int duration = hasDuration ? effect.mDuration : 1;
        if (!appliedOnce)
            duration = std::max(1, duration);
        int durationOffset = 0;
        int minArea = 0;
        float costMult = store.get<ESM::GameSetting>()
                             .find(method == EffectCostMethod::GamePotion ? "iAlchemyMod" : "fEffectCostMult")
                             ->mValue.getFloat();
        if (method == EffectCostMethod::PlayerSpell)
        {
            durationOffset = 1;
            minArea = 1;
        }
        else if (method == EffectCostMethod::GamePotion)
        {
            minArea = 1;
        }

        float x = 0.5f * (minMagn + maxMagn);
        x *= 0.1f * magicEffect->mData.mBaseCost;
        x *= durationOffset + duration;
        x += 0.05f * std::max(minArea, effect.mArea) * magicEffect->mData.mBaseCost;

        return x * costMult;
    }

    int calcSpellCost(const ESM::Spell& spell)
    {
        // Fixed-cost records never needed an Environment/content service.
        if (!(spell.mData.mFlags & ESM::Spell::F_Autocalc))
            return spell.mData.mCost;
        return calcSpellCost(spell, *MWBase::Environment::get().getESMStore());
    }

    int calcSpellCost(const ESM::Spell& spell, const MWWorld::ESMStore& store)
    {
        if (!(spell.mData.mFlags & ESM::Spell::F_Autocalc))
            return spell.mData.mCost;

        float cost = getTotalCost(spell.mEffects, store);

        return static_cast<int>(std::round(cost));
    }

    int getEffectiveEnchantmentCastCost(float castCost, const MWWorld::Ptr& actor)
    {
        return getEffectiveEnchantmentCastCost(castCost,
            static_cast<float>(actor.getClass().getSkill(actor, ESM::Skill::Enchant)));
    }

    int getEffectiveEnchantmentCastCost(float castCost, float enchantSkill)
    {
        /*
         * Each point of enchant skill above/under 10 subtracts/adds
         * one percent of enchantment cost while minimum is 1.
         */
        const float result = castCost - (castCost / 100) * (enchantSkill - 10);

        return static_cast<int>((result < 1) ? 1 : result);
    }

    int getEffectiveEnchantmentCastCost(const ESM::Enchantment& enchantment, const MWWorld::Ptr& actor)
    {
        float castCost;
        if (enchantment.mData.mFlags & ESM::Enchantment::Autocalc)
            castCost = getEnchantmentCastCost(enchantment, *MWBase::Environment::get().getESMStore());
        else
            castCost = static_cast<float>(enchantment.mData.mCost);
        return getEffectiveEnchantmentCastCost(castCost, actor);
    }

    int getEnchantmentCharge(const ESM::Enchantment& enchantment)
    {
        if (!(enchantment.mData.mFlags & ESM::Enchantment::Autocalc))
            return enchantment.mData.mCharge;
        return getEnchantmentCharge(enchantment, *MWBase::Environment::get().getESMStore());
    }

    float getEnchantmentCastCost(const ESM::Enchantment& enchantment, const MWWorld::ESMStore& store)
    {
        if (enchantment.mData.mFlags & ESM::Enchantment::Autocalc)
            return getTotalCost(enchantment.mEffects, store, EffectCostMethod::GameEnchantment);
        return static_cast<float>(enchantment.mData.mCost);
    }

    int getEnchantmentCharge(const ESM::Enchantment& enchantment, const MWWorld::ESMStore& content)
    {
        if (enchantment.mData.mFlags & ESM::Enchantment::Autocalc)
        {
            int charge = static_cast<int>(std::round(getEnchantmentCastCost(enchantment, content)));
            const auto& store = content.get<ESM::GameSetting>();
            switch (enchantment.mData.mType)
            {
                case ESM::Enchantment::CastOnce:
                {
                    const int iMagicItemChargeOnce = store.find("iMagicItemChargeOnce")->mValue.getInteger();
                    return charge * iMagicItemChargeOnce;
                }
                case ESM::Enchantment::WhenStrikes:
                {
                    const int iMagicItemChargeStrike = store.find("iMagicItemChargeStrike")->mValue.getInteger();
                    return charge * iMagicItemChargeStrike;
                }
                case ESM::Enchantment::WhenUsed:
                {
                    const int iMagicItemChargeUse = store.find("iMagicItemChargeUse")->mValue.getInteger();
                    return charge * iMagicItemChargeUse;
                }
                case ESM::Enchantment::ConstantEffect:
                {
                    const int iMagicItemChargeConst = store.find("iMagicItemChargeConst")->mValue.getInteger();
                    return charge * iMagicItemChargeConst;
                }
            }
        }
        return enchantment.mData.mCharge;
    }

    int getPotionValue(const ESM::Potion& potion)
    {
        if (potion.mData.mFlags & ESM::Potion::Autocalc)
        {
            float cost = getTotalCost(potion.mEffects, EffectCostMethod::GamePotion);
            return static_cast<int>(std::round(cost));
        }
        return potion.mData.mValue;
    }

    std::optional<ESM::EffectList> rollIngredientEffect(
        MWWorld::Ptr caster, const ESM::Ingredient* ingredient, uint32_t index)
    {
        if (index >= 4)
            throw std::range_error("Index out of range");

        ESM::ENAMstruct effect;
        effect.mEffectID = ingredient->mData.mEffectID[index];
        effect.mSkill = ingredient->mData.mSkills[index];
        effect.mAttribute = ingredient->mData.mAttributes[index];
        effect.mRange = ESM::RT_Self;
        effect.mArea = 0;

        if (effect.mEffectID.empty())
            return std::nullopt;

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const auto magicEffect = store.get<ESM::MagicEffect>().find(effect.mEffectID);
        const MWMechanics::CreatureStats& creatureStats = caster.getClass().getCreatureStats(caster);

        float x = (caster.getClass().getSkill(caster, ESM::Skill::Alchemy)
                      + 0.2f * creatureStats.getAttribute(ESM::Attribute::Intelligence).getModified()
                      + 0.1f * creatureStats.getAttribute(ESM::Attribute::Luck).getModified())
            * creatureStats.getFatigueTerm();

        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        int roll = Misc::Rng::roll0to99(prng);
        if (roll > x)
        {
            return std::nullopt;
        }

        float magnitude = 0;
        float y = roll / std::min(x, 100.f);
        y *= 0.25f * x;
        if (magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration)
            effect.mDuration = 1;
        else
            effect.mDuration = static_cast<int>(y);
        if (!(magicEffect->mData.mFlags & ESM::MagicEffect::NoMagnitude))
        {
            if (!(magicEffect->mData.mFlags & ESM::MagicEffect::NoDuration))
                magnitude = floor((0.05f * y) / (0.1f * magicEffect->mData.mBaseCost));
            else
                magnitude = floor(y / (0.1f * magicEffect->mData.mBaseCost));
            magnitude = std::max(1.f, magnitude);
        }
        else
            magnitude = 1;

        effect.mMagnMax = static_cast<int>(magnitude);
        effect.mMagnMin = static_cast<int>(magnitude);

        ESM::EffectList effects;
        effects.mList.push_back({ effect, index });
        return effects;
    }

    float calcSpellBaseSuccessChance(const ESM::Spell* spell, const MWWorld::Ptr& actor, ESM::RefId* effectiveSchool)
    {
        return spellBaseChance(*spell, *MWBase::Environment::get().getESMStore(),
            actor.getClass().getCreatureStats(actor),
            [&](ESM::RefId school) { return actor.getClass().getSkill(actor, school); }, effectiveSchool);
    }

    float getSpellSuccessChance(
        const ESM::Spell* spell, const MWWorld::Ptr& actor, ESM::RefId* effectiveSchool, bool cap, bool checkMagicka)
    {
        // NB: Base chance is calculated here because the effective school pointer must be filled
        float baseChance = calcSpellBaseSuccessChance(spell, actor, effectiveSchool);

        bool godmode = actor == getPlayer() && MWBase::Environment::get().getWorld()->getGodModeState();

        CreatureStats& stats = actor.getClass().getCreatureStats(actor);

        if (spell->mData.mType == ESM::Spell::ST_Power)
            return stats.getSpells().canUsePower(spell) ? 100.f : 0.f;

        if (godmode)
            return 100.f;

        return spellChance(*spell, *MWBase::Environment::get().getESMStore(),
            stats, baseChance, cap, checkMagicka);
    }

    float getSpellSuccessChance(const ESM::Spell& spell, const NpcStats& actor,
        const MWWorld::ESMStore& store, bool cap, bool checkMagicka)
    {
        const float base = spellBaseChance(spell, store, actor,
            [&](ESM::RefId school) { return actor.getSkill(school).getModified(); }, nullptr);
        return spellChance(spell, store, actor, base, cap, checkMagicka);
    }

    float getSpellSuccessChance(
        const ESM::RefId& spellId, const MWWorld::Ptr& actor, ESM::RefId* effectiveSchool, bool cap, bool checkMagicka)
    {
        if (const auto spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spellId))
            return getSpellSuccessChance(spell, actor, effectiveSchool, cap, checkMagicka);
        return 0.f;
    }

    ESM::RefId getSpellSchool(const ESM::RefId& spellId, const MWWorld::Ptr& actor)
    {
        ESM::RefId school;
        getSpellSuccessChance(spellId, actor, &school);
        return school;
    }

    ESM::RefId getSpellSchool(const ESM::Spell* spell, const MWWorld::Ptr& actor)
    {
        ESM::RefId school;
        getSpellSuccessChance(spell, actor, &school);
        return school;
    }

    bool spellIncreasesSkill(const ESM::Spell* spell)
    {
        return spell->mData.mType == ESM::Spell::ST_Spell && !(spell->mData.mFlags & ESM::Spell::F_Always);
    }

    bool spellIncreasesSkill(const ESM::RefId& spellId)
    {
        const auto spell = MWBase::Environment::get().getESMStore()->get<ESM::Spell>().search(spellId);
        return spell && spellIncreasesSkill(spell);
    }
}
