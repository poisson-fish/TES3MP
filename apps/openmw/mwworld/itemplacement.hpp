#ifndef OPENMW_MWWORLD_ITEMPLACEMENT_H
#define OPENMW_MWWORLD_ITEMPLACEMENT_H

#include <components/esm/position.hpp>
#include <osg/Node>
#include <functional>
#include <optional>

namespace MWWorld
{
    struct ItemPlacementHit
    {
        bool mHit = false;
        osg::Vec3f mPoint;
        osg::Vec3f mNormal;
    };

    inline constexpr float ItemPlacementDistance = 200.f;
    using ItemPlacementRay = std::function<ItemPlacementHit(const osg::Vec3f&, const osg::Vec3f&)>;

    // Stock drop calculations, without inventory, reference, script or scene mutation.
    bool canPlaceItem(const ItemPlacementHit& hit);
    ESM::Position cursorItemPlacement(ESM::Position actor, const ItemPlacementHit& hit);
    ESM::Position groundItemPlacement(ESM::Position actor, const ItemPlacementRay& castRay);
    // node has the proposed position, rotation and scale, as a normal scene root does.
    std::optional<ESM::Position> adjustItemPlacement(ESM::Position position, osg::Node& node);
}
#endif
