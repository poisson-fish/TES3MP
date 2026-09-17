#include "loadout.hpp"
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadclot.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>

namespace TES3MP::Native
{
    std::vector<Loadout::PlacedContainer> Loadout::placedContainers(std::string_view cell)
    {
        if (cell.empty() || cell.size() > 256)
            throw std::invalid_argument("Native placed container cell invalid");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        auto& loaded = world.getInterior(cell);
        std::vector<PlacedContainer> result;
        loaded.forEachType<ESM::Container>([&](const MWWorld::Ptr& ptr) {
            if (result.size() == 4096)
                throw std::invalid_argument("Native placed container discovery budget exceeded");
            ESM::ObjectState state;
            ptr.getCellRef().writeState(state);
            const auto id = MWWorld::placedRefId(state.mRef.mRefNum, mOptions.mContent);
            if (!id || !ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile())
                return true;
            for (float value : state.mRef.mPos.pos)
                if (!std::isfinite(value) || std::abs(double(value)) >= double(INT64_MAX) / 1024)
                    throw std::invalid_argument("Native placed container position outside wire range");
            const auto* base = ptr.get<ESM::Container>()->mBase;
            result.push_back({state.mRef, *id, mOptions.mContent.at(state.mRef.mRefNum.mContentFile),
                base->mInventory.mList.empty(), !base->mScript.empty()});
            return true;
        });
        std::ranges::sort(result, {}, &PlacedContainer::mIdentity);
        return result;
    }

    Loadout::PlacedContainer Loadout::resolveContainer(std::string_view cell, std::string_view plugin, uint32_t index)
    {
        auto references = placedContainers(cell);
        const auto found = std::ranges::find_if(references, [&](const auto& ref) {
            return ref.mRef.mRefNum.mIndex == index && Misc::StringUtils::ciEqual(ref.mPlugin, plugin);
        });
        if (found == references.end())
            throw std::invalid_argument("Native placed container missing, deleted, disabled or not a container");
        if (found->mScripted || found->mRef.mIsLocked || !found->mRef.mTrap.empty())
            throw std::invalid_argument("Native placed container script, lock or trap services unavailable");
        return *found;
    }

    std::vector<Loadout::PlacedContainer> Loadout::resolveContainers(std::string_view cell, size_t limit)
    {
        auto references = placedContainers(cell);
        if (references.empty() || references.size() > limit)
            throw std::invalid_argument("Native interior container count is empty or exceeds the startup budget");
        for (const auto& ref : references)
            if (ref.mScripted || ref.mRef.mIsLocked || !ref.mRef.mTrap.empty())
                throw std::invalid_argument("Native interior container " + std::to_string(ref.mIdentity)
                    + " (" + ref.mRef.mRefID.toDebugString() + ") requires script, lock or trap services");
        return references;
    }

    void Loadout::writeContainers(std::ostream& output, std::string_view cell)
    {
        const auto references = placedContainers(cell);
        for (const auto& ref : references)
        {
            output << "container " << std::quoted(ref.mPlugin) << ' ' << ref.mRef.mRefNum.mIndex << ' '
                << std::quoted(ref.mRef.mRefID.getRefIdString()) << " identity=" << ref.mIdentity
                << " position=" << ref.mRef.mPos.pos[0] << ',' << ref.mRef.mPos.pos[1] << ',' << ref.mRef.mPos.pos[2]
                << " empty=" << ref.mEmptyBase << " script=" << ref.mScripted << " lock=" << ref.mRef.mLockLevel
                << " trap=" << !ref.mRef.mTrap.empty() << '\n';
            const auto& items = mStore.get<ESM::Container>().find(ref.mRef.mRefID)->mInventory.mList;
            if (items.size() > 128) throw std::invalid_argument("Container diagnostic item budget exceeded");
            for (const auto& item : items)
                output << "  item " << item.mItem << " count=" << item.mCount << '\n';
        }
        if (!output) throw std::runtime_error("Native placed container report failed");
    }
}
