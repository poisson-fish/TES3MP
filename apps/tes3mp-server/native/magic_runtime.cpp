#include "magic_runtime.hpp"

#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/spelleffects.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
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
                || effect.mEffectID == ESM::MagicEffect::RestoreFatigue;
        }

        void applyInstantEffect(const ESM::ENAMstruct& effect, MWMechanics::CreatureStats& target)
        {
            const float magnitude = float(effect.mMagnMin);
            if (effect.mEffectID == ESM::MagicEffect::RestoreHealth)
                MWMechanics::restoreHealth(target, magnitude);
            else if (effect.mEffectID == ESM::MagicEffect::RestoreMagicka)
                MWMechanics::restoreDynamicStat(target, 1, magnitude);
            else if (effect.mEffectID == ESM::MagicEffect::RestoreFatigue)
                MWMechanics::restoreDynamicStat(target, 2, magnitude);
            else throw std::invalid_argument("Unsupported native instant effect");
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
                || effect.mArea != 0 || effect.mDuration != 0
                || effect.mMagnMin <= 0 || effect.mMagnMin != effect.mMagnMax || effect.mMagnMax > 1000
                || (magic->mData.mFlags & (ESM::MagicEffect::NoDuration | ESM::MagicEffect::AppliedOnce)))
                return std::nullopt;
            result.effects.push_back(effect);
        }
        return result;
    }

    std::optional<PreparedInstantSpell> prepareInstantSpell(const ESM::Spell& spell,
        const MWWorld::ESMStore& content)
    {
        if (spell.mData.mType != ESM::Spell::ST_Spell
            || !(spell.mData.mFlags & ESM::Spell::F_Always)) return std::nullopt;
        auto effects = prepareInstantEffects(spell.mEffects, content);
        if (!effects) return std::nullopt;
        PreparedInstantSpell result;
        result.effects = std::move(*effects);
        result.cost = MWMechanics::calcSpellCost(spell, content);
        if (result.cost < 0 || result.cost > 1000000) return std::nullopt;
        return result;
    }

    InstantSpellResult applyInstantEffects(const PreparedInstantEffects& effects, int range,
        MWMechanics::CreatureStats& target)
    {
        if (range != ESM::RT_Self && range != ESM::RT_Touch && range != ESM::RT_Target)
            throw std::invalid_argument("Native instant effect range invalid");
        const float beforeHealth = target.getHealth().getCurrent();
        const float beforeMagicka = target.getMagicka().getCurrent();
        const float beforeFatigue = target.getFatigue().getCurrent();
        for (const auto& effect : effects.effects)
            if (effect.mRange == range) applyInstantEffect(effect, target);
        return {target.getHealth().getCurrent() - beforeHealth,
            target.getMagicka().getCurrent() - beforeMagicka,
            target.getFatigue().getCurrent() - beforeFatigue};
    }

    InstantSpellResult resolveInstantSpell(const PreparedInstantSpell& spell,
        MWMechanics::CreatureStats& caster, Misc::Rng::Generator& rng)
    {
        if (caster.getHealth().getCurrent() <= 0 || caster.getMagicka().getCurrent() < spell.cost)
            throw std::invalid_argument("Native spell became stale before tick composition");
        // Stock CastSpell consumes this roll even for Always Succeeds.
        (void)Misc::Rng::roll0to99(rng);
        auto magicka = caster.getMagicka();
        magicka.setCurrent(magicka.getCurrent() - spell.cost);
        caster.setMagicka(magicka);
        auto result = applyInstantEffects(spell.effects, ESM::RT_Self, caster);
        result.magicka -= float(spell.cost);
        return result;
    }
}
