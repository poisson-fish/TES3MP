#ifndef TES3MP_NATIVE_PLACEMENT_SCENE_HPP
#define TES3MP_NATIVE_PLACEMENT_SCENE_HPP

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/objectstate.hpp>
#include <tes3mp/inventory_world.hpp>
#include <memory>
#include <span>
#include <string>

namespace TES3MP::Native
{
    class Loadout;
    // Read-only OpenMW model scene for the currently bound interior. This target
    // owns no gameplay state; dynamic membership is supplied from the committed image.
    class PlacementScene
    {
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    public:
        PlacementScene(Loadout& loadout, std::string_view cell, std::span<const ESM::CellRef> worldDomain);
        ~PlacementScene();
        const std::string& fingerprint() const;
        ESM::Position resolve(const ESM::Position& actor, const MWWorld::Ptr& item,
            const DropPlacementView& view, std::span<const ESM::ObjectState> worldItems);
    };
}
#endif
