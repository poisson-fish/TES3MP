#ifndef TES3MP_NATIVE_MAGIC_RUNTIME_HPP
#define TES3MP_NATIVE_MAGIC_RUNTIME_HPP

#include <components/esm3/effectlist.hpp>
#include <components/misc/rng.hpp>
#include <optional>
#include <vector>

namespace ESM { struct Spell; }
namespace MWWorld { class ESMStore; }
namespace MWMechanics { class NpcStats; }

namespace TES3MP::Native
{
    // A bounded, detached cast. The caller owns authentication and source/target
    // identity; this runtime owns OpenMW record rules, costs and direct effects.
    struct PreparedInstantSpell
    {
        int cost = 0;
        std::vector<ESM::ENAMstruct> effects;
    };

    struct InstantSpellResult
    {
        float health = 0;
        float magicka = 0;
        float fatigue = 0;
    };

    std::optional<PreparedInstantSpell> prepareInstantSpell(const ESM::Spell& spell,
        const MWWorld::ESMStore& content);
    InstantSpellResult resolveInstantSpell(const PreparedInstantSpell& spell,
        MWMechanics::NpcStats& caster, Misc::Rng::Generator& rng);
}

#endif
