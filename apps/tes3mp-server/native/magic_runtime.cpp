#include "magic_runtime.hpp"

#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmechanics/spelleffects.hpp>
#include <apps/openmw/mwmechanics/spellutil.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/misc/rng.hpp>
#include <stdexcept>

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

        void applyInstantEffect(const ESM::ENAMstruct& effect, MWMechanics::NpcStats& target)
        {
            const float magnitude = float(effect.mMagnMin);
            if (effect.mEffectID == ESM::MagicEffect::RestoreHealth)
                MWMechanics::restoreHealth(target, magnitude);
            else if (effect.mEffectID == ESM::MagicEffect::RestoreMagicka)
            {
                auto stat = target.getMagicka();
                stat.setCurrent(stat.getCurrent() + magnitude);
                target.setMagicka(stat);
            }
            else if (effect.mEffectID == ESM::MagicEffect::RestoreFatigue)
            {
                auto stat = target.getFatigue();
                stat.setCurrent(stat.getCurrent() + magnitude);
                target.setFatigue(stat);
            }
            else throw std::invalid_argument("Unsupported native instant effect");
        }
    }

    std::optional<PreparedInstantSpell> prepareInstantSpell(const ESM::Spell& spell,
        const MWWorld::ESMStore& content)
    {
        if (spell.mData.mType != ESM::Spell::ST_Spell
            || !(spell.mData.mFlags & ESM::Spell::F_Always)
            || spell.mEffects.mList.empty() || spell.mEffects.mList.size() > 8)
            return std::nullopt;
        PreparedInstantSpell result;
        for (const auto& entry : spell.mEffects.mList)
        {
            const auto& effect = entry.mData;
            const auto* magic = content.get<ESM::MagicEffect>().search(effect.mEffectID);
            if (!magic || !supportedInstantEffect(effect)
                || effect.mRange != ESM::RT_Self || effect.mArea != 0 || effect.mDuration != 0
                || effect.mMagnMin <= 0 || effect.mMagnMin != effect.mMagnMax || effect.mMagnMax > 1000
                || (magic->mData.mFlags & (ESM::MagicEffect::NoDuration | ESM::MagicEffect::AppliedOnce)))
                return std::nullopt;
            result.effects.push_back(effect);
        }
        result.cost = MWMechanics::calcSpellCost(spell, content);
        if (result.cost < 0 || result.cost > 1000000) return std::nullopt;
        return result;
    }

    InstantSpellResult resolveInstantSpell(const PreparedInstantSpell& spell,
        MWMechanics::NpcStats& caster, Misc::Rng::Generator& rng)
    {
        if (caster.getHealth().getCurrent() <= 0 || caster.getMagicka().getCurrent() < spell.cost)
            throw std::invalid_argument("Native spell became stale before tick composition");
        // Stock CastSpell consumes this roll even for Always Succeeds.
        (void)Misc::Rng::roll0to99(rng);
        const float beforeHealth = caster.getHealth().getCurrent();
        const float beforeMagicka = caster.getMagicka().getCurrent();
        const float beforeFatigue = caster.getFatigue().getCurrent();
        auto magicka = caster.getMagicka();
        magicka.setCurrent(magicka.getCurrent() - spell.cost);
        caster.setMagicka(magicka);
        for (const auto& effect : spell.effects) applyInstantEffect(effect, caster);
        return {caster.getHealth().getCurrent() - beforeHealth,
            caster.getMagicka().getCurrent() - beforeMagicka,
            caster.getFatigue().getCurrent() - beforeFatigue};
    }
}
