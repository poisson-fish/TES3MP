#ifndef TES3MP_NATIVE_AI_MAGIC_HPP
#define TES3MP_NATIVE_AI_MAGIC_HPP

#include "magic_runtime.hpp"
#include <components/esm3/refnum.hpp>
#include <span>

namespace TES3MP::Native
{
    // Caller supplies known spells, owned item instances and current activity.
    // Activity must be scoped to this caster and the intended target; these are
    // simulation inputs, never client-provided eligibility claims.
    struct AiMagicSpell
    {
        const ESM::Spell* record = nullptr;
        bool racialPower = false;
        bool activeOnSelf = false;
        bool activeOnEnemy = false;
    };

    struct AiMagicItem
    {
        ESM::RefNum instance;
        ESM::RefId enchantment;
        float charge = -1.f;
        bool equipped = false;
        bool activeOnSelf = false;
        float enemyDuration = 0.f;
    };

    struct AiMagicContext
    {
        const MWMechanics::NpcStats& caster;
        const MWMechanics::CreatureStats* enemy = nullptr;
        bool casterUnderwater = false;
        bool enemyUnderwater = false;
    };

    struct AiMagicPayment
    {
        ESM::RefNum instance;
        float before = 0;
        float after = 0;
    };

    struct PreparedAiMagicCast
    {
        PreparedInstantSpell spell;
        ESM::RefId effectSource;
        std::optional<AiMagicPayment> payment;
        float rating = 0;
    };

    inline constexpr size_t MaximumAiMagicSources = 1024;

    // Read-only selection: no cost, charge, RNG, effects or presentation changes.
    // Enumerate equipped WhenUsed items first, then known spells, as stock AI
    // does; equal ratings retain the earlier source. Unsupported effect plans
    // are skipped as a whole. The returned plan uses the player launch helpers.
    // Production scheduling and publication require durable actor caster identity.
    std::optional<PreparedAiMagicCast> prepareAiMagicCast(const AiMagicContext& context,
        std::span<const AiMagicSpell> spells, std::span<const AiMagicItem> items,
        const MWWorld::ESMStore& content);
}

#endif
