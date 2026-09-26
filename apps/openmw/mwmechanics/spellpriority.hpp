#ifndef OPENMW_SPELL_PRIORITY_H
#define OPENMW_SPELL_PRIORITY_H

#include <optional>

namespace ESM
{
    struct Spell;
    struct EffectList;
    struct ENAMstruct;
    struct Enchantment;
}

namespace MWWorld
{
    class Ptr;
    class ESMStore;
}

namespace MWMechanics
{
    class CreatureStats;

    // Common combat effects can be rated without World, player or presentation
    // services. Unhandled effects require the richer stock actor context.
    std::optional<float> rateCommonEffect(const ESM::ENAMstruct& effect,
        const CreatureStats& actor, float restoreMagickaPriority);
    float adjustEffectRating(const ESM::ENAMstruct& effect, float rating,
        const CreatureStats& actor, const CreatureStats* enemy, const MWWorld::ESMStore& store,
        bool actorUnderwater, bool enemyUnderwater);
    float effectRatingMultiplier(int range, const MWWorld::ESMStore& store);
    float spellRatingMultiplier(const ESM::Spell& spell, float successChance,
        bool racialPower, bool activeOnSelf, bool activeOnEnemy);
    float magicItemRatingMultiplier(const ESM::Enchantment& enchantment, bool npc, bool equipped,
        float charge, int castCost, bool activeOnSelf, float enemyDuration);

    // RangeTypes using bitflags to allow multiple range types, as can be the case with spells having multiple effects.
    enum RangeTypes
    {
        Self = 0x1,
        Touch = 0x10,
        Target = 0x100
    };

    int getRangeTypes(const ESM::EffectList& effects);

    float rateSpell(
        const ESM::Spell* spell, const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy, bool checkMagicka = true);
    float rateMagicItem(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy);
    float ratePotion(const MWWorld::Ptr& item, const MWWorld::Ptr& actor);

    /// @note target may be empty
    float rateEffect(const ESM::ENAMstruct& effect, const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy);
    /// @note target may be empty
    float rateEffects(
        const ESM::EffectList& list, const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy, bool useSpellMult = true);

    float vanillaRateSpell(const ESM::Spell* spell, const MWWorld::Ptr& actor, const MWWorld::Ptr& enemy);
}

#endif
