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
    std::vector<Loadout::PlacedDoor> Loadout::teleportDoors(ESM::RefId cell, ESM::RefId destination)
    {
        validateCell(cell);
        validateCell(destination);
        if (!mStore.getLuaScriptsCfg().mScripts.empty())
            throw std::invalid_argument("Native teleport cell bounds or Lua services invalid");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        const auto destinationId = world.getCell(destination).getCell()->getId();
        auto& loaded = world.getCell(cell);
        if (loaded.getCell()->getId() == destinationId)
            throw std::invalid_argument("Native teleport requires two distinct cells");
        std::vector<PlacedDoor> result;
        loaded.forEachType<ESM::Door>([&](const MWWorld::Ptr& ptr) {
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
            const auto& ref = ptr.getCellRef();
            if (!ref.getTeleport() || ref.isLocked() || !ref.getTrap().empty()
                || !ptr.get<ESM::Door>()->mBase->mScript.empty()) return true;
            ESM::DoorState state;
            ref.writeState(state);
            const auto& dest = state.mRef.mDoorDest;
            for (float value : dest.pos)
                if (!std::isfinite(value) || std::abs(double(value)) >= double(INT64_MAX) / 1024)
                    throw std::invalid_argument("Native teleport destination outside wire bounds");
            if (state.mRef.mDestCell.empty())
                for (int i = 0; i < 2; ++i)
                    if (double(dest.pos[i]) < -32768.0 * 8192 || double(dest.pos[i]) >= 32768.0 * 8192)
                        throw std::invalid_argument("Native teleport exterior destination outside bounds");
            // The engine resolves case/overrides and validates the destination;
            // unrelated doors never enter this bounded domain.
            if (ref.getDestCell() != destinationId) return true;
            if (world.getCell(ref.getDestCell()).getCell()->getId() != destinationId)
                throw std::invalid_argument("Native teleport destination resolution changed");
            const auto refNum = ref.getRefNum();
            const auto id = MWWorld::placedRefId(refNum, mOptions.mContent);
            if (!id || result.size() == 32)
                throw std::invalid_argument("Native teleport identity or count outside bounds");
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

    Loadout::PlacedDoor Loadout::resolveDoor(ESM::RefId cell, std::string_view plugin, uint32_t index)
    {
        validateCell(cell);
        if (plugin.empty() || plugin.size() > 256 || plugin.find('\0') != std::string_view::npos)
            throw std::invalid_argument("Native door selection outside bounds");
        // Global or attached Lua could mutate a door even without an MWScript.
        // No Lua services are composed for this preparation-only slice.
        if (!mStore.getLuaScriptsCfg().mScripts.empty())
            throw std::invalid_argument("Native door Lua services unavailable");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        auto& loaded = world.getCell(cell);
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
