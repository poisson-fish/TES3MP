#include "loadout.hpp"
#include "ordinary_door.hpp"

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>

#include <optional>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace TES3MP::Native
{
    std::vector<Loadout::PlacedDoor> Loadout::teleportDoors(std::string_view cell, std::string_view destination)
    {
        if (cell.empty() || destination.empty() || cell.size() > 256 || destination.size() > 256
            || cell.find('\0') != std::string_view::npos || destination.find('\0') != std::string_view::npos
            || !mStore.getLuaScriptsCfg().mScripts.empty())
            throw std::invalid_argument("Native teleport cell bounds or Lua services invalid");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        const auto destinationId = world.getInterior(destination).getCell()->getId();
        auto& loaded = world.getInterior(cell);
        if (loaded.getCell()->getId() == destinationId)
            throw std::invalid_argument("Native teleport requires two distinct interiors");
        std::vector<PlacedDoor> result;
        loaded.forEachType<ESM::Door>([&](const MWWorld::Ptr& ptr) {
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
            const auto& ref = ptr.getCellRef();
            if (!ref.getTeleport() || ref.getDestCell().empty() || ref.isLocked() || !ref.getTrap().empty()
                || !ptr.get<ESM::Door>()->mBase->mScript.empty()) return true;
            // The engine resolves case/overrides and validates the destination;
            // exterior and unrelated doors never enter this bounded domain.
            if (ref.getDestCell() != destinationId) return true;
            if (world.getInterior(ref.getDestCell().getRefIdString()).getCell()->getId() != destinationId)
                throw std::invalid_argument("Native teleport destination resolution changed");
            const auto refNum = ref.getRefNum();
            const auto id = MWWorld::placedRefId(refNum, mOptions.mContent);
            if (!id || result.size() == 32)
                throw std::invalid_argument("Native teleport identity or count outside bounds");
            ESM::DoorState state;
            ref.writeState(state);
            if (state.mRef.mCount != 1 || state.mRef.mReferenceBlocked > 0)
                throw std::invalid_argument("Unsupported native teleport placement");
            for (const auto& position : {state.mRef.mPos, state.mRef.mDoorDest})
                for (int i = 0; i < 3; ++i)
                    if (!std::isfinite(position.pos[i]) || std::abs(double(position.pos[i])) >= double(INT64_MAX) / 1024
                        || !std::isfinite(position.rot[i]))
                        throw std::invalid_argument("Native teleport transform outside bounds");
            result.push_back({state.mRef, *id, mOptions.mContent.at(refNum.mContentFile)});
            return true;
        });
        std::ranges::sort(result, {}, &PlacedDoor::mIdentity);
        return result;
    }

    Loadout::PlacedDoor Loadout::resolveDoor(std::string_view cell, std::string_view plugin, uint32_t index)
    {
        if (cell.empty() || cell.size() > 256 || cell.find('\0') != std::string_view::npos
            || plugin.empty() || plugin.size() > 256 || plugin.find('\0') != std::string_view::npos)
            throw std::invalid_argument("Native door selection outside bounds");
        // Global or attached Lua could mutate a door even without an MWScript.
        // No Lua services are composed for this preparation-only slice.
        if (!mStore.getLuaScriptsCfg().mScripts.empty())
            throw std::invalid_argument("Native door Lua services unavailable");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        auto& loaded = world.getInterior(cell);
        std::optional<PlacedDoor> result;
        loaded.forEachType<ESM::Door>([&](const MWWorld::Ptr& ptr) {
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
            const auto refNum = ptr.getCellRef().getRefNum();
            const auto id = MWWorld::placedRefId(refNum, mOptions.mContent);
            if (!id || refNum.mIndex != index
                || !Misc::StringUtils::ciEqual(mOptions.mContent.at(refNum.mContentFile), plugin)) return true;
            ESM::DoorState state;
            ptr.getCellRef().writeState(state);
            OrdinaryDoor checked(*ptr.get<ESM::Door>()->mBase, state.mRef);
            if (result) throw std::invalid_argument("Native door placement is ambiguous");
            result = PlacedDoor{state.mRef, *id, mOptions.mContent.at(refNum.mContentFile)};
            return true;
        });
        if (!result) throw std::invalid_argument("Native door missing, deleted, disabled or not a door");
        return std::move(*result);
    }
}
