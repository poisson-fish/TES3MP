#include "magic_runtime.hpp"

#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/spelleffects.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwmechanics/spellresistance.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/misc/rng.hpp>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace TES3MP::Native
{
    namespace
    {
        bool supportedInstantEffect(const ESM::ENAMstruct& effect)
        {
            return effect.mEffectID == ESM::MagicEffect::RestoreHealth
                || effect.mEffectID == ESM::MagicEffect::RestoreMagicka
                || effect.mEffectID == ESM::MagicEffect::RestoreFatigue
                || effect.mEffectID == ESM::MagicEffect::DamageHealth
                || effect.mEffectID == ESM::MagicEffect::DamageMagicka
                || effect.mEffectID == ESM::MagicEffect::DamageFatigue
                || effect.mEffectID == ESM::MagicEffect::ResistMagicka;
        }

        void applyInstantEffect(const ESM::ENAMstruct& effect, MWMechanics::CreatureStats& target,
            Misc::Rng::Generator* rng, const MWWorld::ESMStore* content, bool uncappedDamageFatigue)
        {
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
                if (effect.mEffectID == ESM::MagicEffect::DamageHealth)
                    MWMechanics::adjustDynamicStatValue(target, 0, -loss);
                else if (effect.mEffectID == ESM::MagicEffect::DamageMagicka)
                    MWMechanics::adjustDynamicStatValue(target, 1, -loss);
                else
                    MWMechanics::adjustDynamicStatValue(target, 2, -loss, uncappedDamageFatigue);
            }
            else if (effect.mEffectID != ESM::MagicEffect::ResistMagicka)
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

    std::optional<PreparedInstantEffects> prepareInstantEffects(const ESM::EffectList& effects,
        const MWWorld::ESMStore& content)
    {
        if (effects.mList.empty() || effects.mList.size() > 8) return std::nullopt;
        PreparedInstantEffects result;
        for (const auto& entry : effects.mList)
        {
            const auto& effect = entry.mData;
            const auto* magic = content.get<ESM::MagicEffect>().search(effect.mEffectID);
            if (!magic || !supportedInstantEffect(effect)
                || (effect.mRange != ESM::RT_Self && effect.mRange != ESM::RT_Touch
                    && effect.mRange != ESM::RT_Target)
                || effect.mArea < 0 || effect.mArea > 64
                || effect.mMagnMin <= 0 || effect.mMagnMin > effect.mMagnMax || effect.mMagnMax > 1000
                || (effect.mEffectID == ESM::MagicEffect::ResistMagicka
                    && effect.mMagnMin != effect.mMagnMax)
                || (effect.mEffectID == ESM::MagicEffect::ResistMagicka
                    ? effect.mDuration < 1 || effect.mDuration > 3600
                    : effect.mDuration != 0)
                || (magic->mData.mFlags & ESM::MagicEffect::NoDuration)
                || (effect.mEffectID != ESM::MagicEffect::ResistMagicka
                    && (magic->mData.mFlags & ESM::MagicEffect::AppliedOnce)))
                return std::nullopt;
            result.effects.push_back(effect);
        }
        return result;
    }

    std::optional<PreparedInstantSpell> prepareInstantSpell(const ESM::Spell& spell,
        const MWWorld::ESMStore& content)
    {
        if (spell.mData.mType != ESM::Spell::ST_Spell) return std::nullopt;
        auto effects = prepareInstantEffects(spell.mEffects, content);
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
        bool uncappedDamageFatigue)
    {
        if (!spell.source || caster.getHealth().getCurrent() <= 0
            || caster.getMagicka().getCurrent() < spell.cost)
            throw std::invalid_argument("Native spell became stale before tick composition");
        const bool succeeded = Misc::Rng::roll0to99(rng)
            < MWMechanics::getSpellSuccessChance(*spell.source, caster, content, true, false);
        auto magicka = caster.getMagicka();
        magicka.setCurrent(magicka.getCurrent() - spell.cost);
        caster.setMagicka(magicka);
        auto result = succeeded ? applyInstantEffects(spell.effects, ESM::RT_Self, caster, &rng, &content,
            uncappedDamageFatigue)
            : InstantSpellResult{};
        result.magicka -= float(spell.cost);
        return {succeeded, result};
    }

}
