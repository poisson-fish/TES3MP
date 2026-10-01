#ifndef OPENMW_MECHANICS_SUMMONING_H
#define OPENMW_MECHANICS_SUMMONING_H

#include <string_view>
#include <utility>
#include <array>
#include <algorithm>
#include <exception>
#include <osg/Quat>
#include <osg/Vec3f>
#include <components/esm3/loadmgef.hpp>

#include <components/esm3/refnum.hpp>

namespace ESM
{
    class RefId;
}
namespace MWWorld
{
    class Ptr;
    class ESMStore;
}

namespace MWMechanics
{
    // One content-neutral selection table for stock and detached actor runtimes.
    // Resolve against the caller's store: caching GMST results globally aliases
    // independent loadouts and loses overrides when a store is replaced.
    inline const std::array<std::pair<ESM::RefId, std::string_view>, 22>& summonSettings()
    {
        static const std::array<std::pair<ESM::RefId, std::string_view>, 22> values{{
            {ESM::MagicEffect::SummonAncestralGhost, "sMagicAncestralGhostID"},
            {ESM::MagicEffect::SummonBonelord, "sMagicBonelordID"},
            {ESM::MagicEffect::SummonBonewalker, "sMagicLeastBonewalkerID"},
            {ESM::MagicEffect::SummonCenturionSphere, "sMagicCenturionSphereID"},
            {ESM::MagicEffect::SummonClannfear, "sMagicClannfearID"},
            {ESM::MagicEffect::SummonDaedroth, "sMagicDaedrothID"},
            {ESM::MagicEffect::SummonDremora, "sMagicDremoraID"},
            {ESM::MagicEffect::SummonFabricant, "sMagicFabricantID"},
            {ESM::MagicEffect::SummonFlameAtronach, "sMagicFlameAtronachID"},
            {ESM::MagicEffect::SummonFrostAtronach, "sMagicFrostAtronachID"},
            {ESM::MagicEffect::SummonGoldenSaint, "sMagicGoldenSaintID"},
            {ESM::MagicEffect::SummonGreaterBonewalker, "sMagicGreaterBonewalkerID"},
            {ESM::MagicEffect::SummonHunger, "sMagicHungerID"},
            {ESM::MagicEffect::SummonScamp, "sMagicScampID"},
            {ESM::MagicEffect::SummonSkeletalMinion, "sMagicSkeletalMinionID"},
            {ESM::MagicEffect::SummonStormAtronach, "sMagicStormAtronachID"},
            {ESM::MagicEffect::SummonWingedTwilight, "sMagicWingedTwilightID"},
            {ESM::MagicEffect::SummonWolf, "sMagicCreature01ID"},
            {ESM::MagicEffect::SummonBear, "sMagicCreature02ID"},
            {ESM::MagicEffect::SummonBonewolf, "sMagicCreature03ID"},
            {ESM::MagicEffect::SummonCreature04, "sMagicCreature04ID"},
            {ESM::MagicEffect::SummonCreature05, "sMagicCreature05ID"},
        }};
        return values;
    }

    inline std::string_view summonSetting(ESM::RefId effect)
    {
        const auto& values = summonSettings();
        const auto found = std::find_if(values.begin(), values.end(),
            [&](const auto& value) { return value.first == effect; });
        return found == values.end() ? std::string_view{} : found->second;
    }

    inline bool isSummoningEffect(ESM::RefId effectId) { return !summonSetting(effectId).empty(); }

    template<class Lookup>
    ESM::RefId selectSummonedCreature(ESM::RefId effect, Lookup&& lookup)
    {
        const auto setting = summonSetting(effect);
        return setting.empty() ? ESM::RefId{} : ESM::RefId::stringRefId(lookup(setting));
    }

    ESM::RefId getSummonedCreature(ESM::RefId effectId);
    ESM::RefId getSummonedCreature(ESM::RefId effectId, const MWWorld::ESMStore& store);

    // Stock safePlaceObject tries front, right, left, back, raises the
    // candidate for slopes and then snaps it down. Keep the fallback policy
    // independent of World; callers own collision queries and ground placement.
    template<class Clear>
    osg::Vec3f summonSpawnPoint(const osg::Vec3f& origin, float yaw, int direction,
        float distance, bool actor, Clear&& clear)
    {
        const osg::Quat orientation(yaw, osg::Vec3f(0, 0, -1));
        const std::array<int, 4> directions{direction, (direction + 3) % 4,
            (direction + 2) % 4, (direction + 1) % 4};
        osg::Vec3f point = origin;
        for (int next : directions)
        {
            if (next == 0) point = origin + (orientation * osg::Vec3f(0, 1, 0)) * distance;
            else if (next == 1) point = origin - (orientation * osg::Vec3f(0, 1, 0)) * distance;
            else if (next == 2) point = origin - (orientation * osg::Vec3f(1, 0, 0)) * distance;
            else if (next == 3) point = origin + (orientation * osg::Vec3f(1, 0, 0)) * distance;
            if (!actor) break;
            point.z() += 30;
            if (clear(point, osg::Vec3f(origin.x(), origin.y(), origin.z() + 20))) break;
        }
        return point;
    }

    template<class Identity, class Spawn, class Follow, class Remember, class Failure>
    Identity createSummon(ESM::RefId record, Spawn&& spawn, Follow&& follow, Remember&& remember, Failure&& failure)
    {
        if (record.empty()) return {};
        Identity identity{};
        try
        {
            spawn(record, identity);
            if (identity != Identity{}) follow(identity);
        }
        catch (const std::exception& error) { failure(error); }
        // A failed spawn remains associated with this effect. It must not retry
        // each frame, and removal must not clean up an unrelated reference.
        remember(identity);
        return identity;
    }

    template<class Identity, class Children, class Remove, class Finish>
    void removeSummonTree(Identity identity, Children&& children, Remove&& remove, Finish&& finish)
    {
        // Snapshot children before deletion invalidates the engine reference.
        const auto descendants = children(identity);
        remove(identity);
        for (auto child : descendants) removeSummonTree(child, children, remove, finish);
        finish(identity);
    }

    void purgeSummonEffect(const MWWorld::Ptr& summoner, const std::pair<ESM::RefId, ESM::RefNum>& summon);

    ESM::RefNum summonCreature(ESM::RefId effectId, const MWWorld::Ptr& summoner);

    void updateSummons(const MWWorld::Ptr& summoner, bool cleanup);
}

#endif
