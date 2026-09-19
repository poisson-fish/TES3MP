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
    ESM::Position Loadout::playerSpawn(ESM::RefId start, std::span<const ESM::RefId> cells,
        std::optional<ESM::Position> position)
    {
        validateCell(start);
        if (cells.empty() || cells.size() > 256 || std::ranges::find(cells, start) == cells.end())
            throw std::invalid_argument("Spawn area does not contain the starting cell");
        if (!position)
        {
            std::vector<ESM::RefId> sources(cells.begin(), cells.end());
            if (sources.size() == 1)
                for (const auto& door : teleportDoors(start, std::span<const ESM::RefId>{}))
                    if (std::ranges::find(sources, door.mDestination) == sources.end()) sources.push_back(door.mDestination);
            for (const auto& cell : sources)
            {
                if (cell == start) continue;
                const auto entrances = teleportDoors(cell, start);
                if (!entrances.empty()) { position = entrances.front().mRef.mDoorDest; break; }
            }
        }
        if (!position) throw std::invalid_argument("No incoming door supplies a spawn; specify SPAWN_X:SPAWN_Y:SPAWN_Z after RADIUS");
        for (float value : position->pos)
            if (!std::isfinite(value) || std::abs(double(value) * 1024) > 2147483647)
                throw std::invalid_argument("Spawn exceeds the inherited movement coordinate bounds");
        for (float value : position->rot)
            if (!std::isfinite(value)) throw std::invalid_argument("Spawn rotation invalid");
        if (const auto* exterior = start.getIf<ESM::ESM3ExteriorCellRefId>(); exterior
            && (std::floor(double(position->pos[0]) / 8192) != exterior->getX()
                || std::floor(double(position->pos[1]) / 8192) != exterior->getY()))
            throw std::invalid_argument("Spawn does not belong to the starting exterior cell");
        return *position;
    }

    std::vector<ESM::RefId> Loadout::playerAreas(ESM::RefId start, unsigned exteriorRadius, size_t limit)
    {
        validateCell(start);
        if (!limit || limit > 256 || exteriorRadius > 7)
            throw std::invalid_argument("Native player-area discovery budget invalid");
        const auto& cells = mStore.get<ESM::Cell>();
        if (cells.getSize() > 65536) throw std::invalid_argument("Native cell catalog budget exceeded");
        std::vector<ESM::RefId> result{start};
        const auto add = [&](ESM::RefId cell) {
            validateCell(cell);
            if (std::ranges::find(result, cell) != result.end()) return;
            if (result.size() == limit) throw std::invalid_argument("Native player-area discovery exhausted its cell budget");
            result.push_back(cell);
        };
        std::optional<ESM::ESM3ExteriorCellRefId> anchor;
        const auto addExteriorArea = [&](ESM::RefId cell) {
            const auto* ext = cell.getIf<ESM::ESM3ExteriorCellRefId>();
            if (!ext || anchor) return;
            anchor = *ext;
            for (int y = -int(exteriorRadius); y <= int(exteriorRadius); ++y)
                for (int x = -int(exteriorRadius); x <= int(exteriorRadius); ++x)
                {
                    const auto cell = ESM::RefId::esm3ExteriorCell(ext->getX() + x, ext->getY() + y);
                    add(cell); // OpenMW supplies empty/ocean cells as well.
                }
        };
        addExteriorArea(start);
        // OpenMW resolves the winning door graph. Discovery never reloads loot,
        // executes scripts, or fabricates placements for the chosen area.
        for (size_t i = 0; i < result.size(); ++i)
            for (const auto& door : teleportDoors(result[i], std::span<const ESM::RefId>{}))
            {
                // An interior start anchors its exterior neighborhood at the
                // first reachable exit. Distant travel doors stay out of bounds.
                addExteriorArea(door.mDestination);
                if (const auto* ext = door.mDestination.getIf<ESM::ESM3ExteriorCellRefId>(); ext
                    && (std::abs(ext->getX() - anchor->getX()) > int(exteriorRadius)
                        || std::abs(ext->getY() - anchor->getY()) > int(exteriorRadius))) continue;
                add(door.mDestination);
            }
        std::sort(result.begin() + 1, result.end());
        return result;
    }

    std::vector<Loadout::PlacedDoor> Loadout::ordinaryDoors(ESM::RefId cell, size_t limit)
    {
        validateCell(cell);
        if (!limit || limit > 128 || !mStore.getLuaScriptsCfg().mScripts.empty())
            throw std::invalid_argument("Native ordinary door discovery bounds or Lua services invalid");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        std::vector<PlacedDoor> result;
        world.getCell(cell).forEachType<ESM::Door>([&](const MWWorld::Ptr& ptr) {
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()
                || ptr.getCellRef().getTeleport()) return true;
            ESM::DoorState state; ptr.getCellRef().writeState(state);
            // Unsupported activators remain visible but never gain authority.
            if (state.mRef.mIsLocked || state.mRef.mLockLevel != 0 || !state.mRef.mKey.empty() || !state.mRef.mTrap.empty()
                || !ptr.get<ESM::Door>()->mBase->mScript.empty()) return true;
            OrdinaryDoor checked(*ptr.get<ESM::Door>()->mBase, state.mRef);
            const auto ref = state.mRef.mRefNum;
            const auto id = MWWorld::placedRefId(ref, mOptions.mContent);
            if (!id || result.size() == limit) throw std::invalid_argument("Native ordinary door discovery budget exceeded");
            result.push_back({state.mRef, *id, mOptions.mContent.at(ref.mContentFile)});
            return true;
        });
        std::ranges::sort(result, {}, &PlacedDoor::mIdentity);
        return result;
    }

    std::vector<Loadout::PlacedDoor> Loadout::teleportDoors(ESM::RefId cell, ESM::RefId destination)
    {
        if (cell == destination) throw std::invalid_argument("Native teleport cells must be distinct");
        const std::array destinations{destination};
        return teleportDoors(cell, destinations);
    }

    std::vector<Loadout::PlacedDoor> Loadout::teleportDoors(ESM::RefId cell, std::span<const ESM::RefId> destinations)
    {
        validateCell(cell);
        for (const auto& destination : destinations) validateCell(destination);
        if (!mStore.getLuaScriptsCfg().mScripts.empty())
            throw std::invalid_argument("Native teleport cell bounds or Lua services invalid");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        auto& loaded = world.getCell(cell);
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
            const auto destinationId = ref.getDestCell();
            if (destinationId == loaded.getCell()->getId()
                || (!destinations.empty() && std::ranges::find(destinations, destinationId) == destinations.end())) return true;
            if (world.getCell(ref.getDestCell()).getCell()->getId() != destinationId)
                throw std::invalid_argument("Native teleport destination resolution changed");
            const auto refNum = ref.getRefNum();
            const auto id = MWWorld::placedRefId(refNum, mOptions.mContent);
            if (!id || result.size() == 32)
                throw std::invalid_argument("Native teleport identity or count outside bounds in "
                    + cell.toDebugString() + " after " + std::to_string(result.size()) + " doors");
            if (state.mRef.mCount != 1 || state.mRef.mReferenceBlocked > 0)
                throw std::invalid_argument("Unsupported native teleport placement");
            for (const auto& position : {state.mRef.mPos, state.mRef.mDoorDest})
                for (int i = 0; i < 3; ++i)
                    if (!std::isfinite(position.pos[i]) || std::abs(double(position.pos[i])) >= double(INT64_MAX) / 1024
                        || !std::isfinite(position.rot[i]))
                        throw std::invalid_argument("Native teleport transform outside bounds");
            result.push_back({state.mRef, *id, mOptions.mContent.at(refNum.mContentFile), destinationId});
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
