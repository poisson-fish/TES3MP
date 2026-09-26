#ifndef TES3MP_NATIVE_MAGIC_RUNTIME_HPP
#define TES3MP_NATIVE_MAGIC_RUNTIME_HPP

#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/misc/rng.hpp>
#include <optional>
#include <vector>

namespace MWWorld { class ESMStore; }
namespace MWMechanics { class CreatureStats; class NpcStats; }

namespace TES3MP::Native
{
    // Source-neutral bounded effect plan. Spell and enchantment records both
    // carry an ESM::EffectList; non-self targets require authoritative contact.
    // In V35 the composed actor tick installs timed effect instances separately.
    struct PreparedInstantEffects
    {
        std::vector<ESM::ENAMstruct> effects;
        bool onlyRange(int range) const noexcept;
        bool hasRange(int range) const noexcept;
    };

    // A bounded spell cast. The source points into the caller's stable ESMStore;
    // that store must outlive preparation and the composed launch tick.
    // The caller owns authentication and spell knowledge.
    struct PreparedInstantSpell
    {
        int cost = 0;
        PreparedInstantEffects effects;
        const ESM::Spell* source = nullptr;
    };

    struct InstantSpellResult
    {
        float health = 0;
        float magicka = 0;
        float fatigue = 0;
    };

    struct InstantSpellLaunch
    {
        bool succeeded = false;
        InstantSpellResult result;
    };

    std::optional<PreparedInstantEffects> prepareInstantEffects(const ESM::EffectList& effects,
        const MWWorld::ESMStore& content, bool actorLifecycle = false);
    std::optional<PreparedInstantSpell> prepareInstantSpell(const ESM::Spell& spell,
        const MWWorld::ESMStore& content, bool actorLifecycle = false);
    InstantSpellResult applyInstantEffects(const PreparedInstantEffects& effects, int range,
        MWMechanics::CreatureStats& target, Misc::Rng::Generator* rng = nullptr,
        const MWWorld::ESMStore* content = nullptr, bool uncappedDamageFatigue = false);
    // Launch spends the source cost and resolves Self effects. Target effects stay
    // in the prepared plan until the authoritative contact step supplies a target.
    InstantSpellLaunch launchInstantSpell(const PreparedInstantSpell& spell,
        MWMechanics::NpcStats& caster, const MWWorld::ESMStore& content, Misc::Rng::Generator& rng,
        bool uncappedDamageFatigue = false, bool resolveSelf = true);
}

#endif
