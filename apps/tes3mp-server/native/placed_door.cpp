#include "loadout.hpp"
#include "ordinary_door.hpp"

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>

#include <optional>
#include <stdexcept>

namespace TES3MP::Native
{
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
