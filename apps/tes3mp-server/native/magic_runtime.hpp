#ifndef TES3MP_NATIVE_MAGIC_RUNTIME_HPP
#define TES3MP_NATIVE_MAGIC_RUNTIME_HPP

#include <components/esm3/effectlist.hpp>
#include <components/misc/rng.hpp>
#include <optional>
#include <vector>

namespace ESM { struct Spell; }
namespace MWWorld { class ESMStore; }
namespace MWMechanics { class CreatureStats; }

namespace TES3MP::Native
{
    // Source-neutral effect plan. Spell and enchantment records both carry an
    // ESM::EffectList; non-self targets require authoritative contact.
    struct PreparedInstantEffects
    {
        std::vector<ESM::ENAMstruct> effects;
        bool onlyRange(int range) const noexcept;
        bool hasRange(int range) const noexcept;
    };

    // A bounded, detached spell cast. The caller owns authentication and
    // spell knowledge; this runtime owns OpenMW record rules, costs and effects.
    struct PreparedInstantSpell
    {
        int cost = 0;
        PreparedInstantEffects effects;
    };

    struct InstantSpellResult
    {
        float health = 0;
        float magicka = 0;
        float fatigue = 0;
    };

    std::optional<PreparedInstantEffects> prepareInstantEffects(const ESM::EffectList& effects,
        const MWWorld::ESMStore& content);
    std::optional<PreparedInstantSpell> prepareInstantSpell(const ESM::Spell& spell,
        const MWWorld::ESMStore& content);
    InstantSpellResult applyInstantEffects(const PreparedInstantEffects& effects, int range,
        MWMechanics::CreatureStats& target);
    InstantSpellResult resolveInstantSpell(const PreparedInstantSpell& spell,
        MWMechanics::CreatureStats& caster, Misc::Rng::Generator& rng);
}

#endif
