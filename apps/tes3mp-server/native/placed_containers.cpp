#include "loadout.hpp"
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/placedrefid.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/inventoryrecordid.hpp>
#include <apps/openmw/mwmechanics/levelledlist.hpp>
#include <components/esm3/objectstate.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>

namespace TES3MP::Native
{
    std::vector<Loadout::PlacedInventory> Loadout::placedItems(ESM::RefId cell, size_t limit)
    {
        validateCell(cell);
        if (limit > 192)
            throw std::invalid_argument("Native world item discovery bound invalid");
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        auto& loaded = world.getCell(cell);
        std::vector<PlacedInventory> result;
        loaded.forEach([&](const MWWorld::Ptr& ptr) {
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
            if (ptr.getType() == ESM::ItemLevList::sRecordId)
                throw std::invalid_argument("Native world leveled-item spawning requires services");
            if (!MWWorld::ContainerStore::isStorableType(ptr.getType())) return true;
            // Fixed lighting is scene content, not a pickup reference.
            if (ptr.getType() == ESM::Light::sRecordId
                && !(ptr.get<ESM::Light>()->mBase->mData.mFlags & ESM::Light::Carry)) return true;
            if (result.size() == limit || !ptr.getClass().getScript(ptr).empty())
                throw std::invalid_argument("Native world item is scripted or exceeds the placement budget");
            ESM::ObjectState state;
            ptr.getCellRef().writeState(state);
            const auto id = MWWorld::placedRefId(state.mRef.mRefNum, mOptions.mContent);
            if (!id || state.mRef.mCount <= 0 || state.mRef.mCount > 1000000)
                throw std::invalid_argument("Native world item identity or count invalid");
            for (float value : state.mRef.mPos.pos)
                if (!std::isfinite(value) || std::abs(double(value)) >= double(INT64_MAX) / 1024)
                    throw std::invalid_argument("Native world position outside wire range");
            result.push_back({state.mRef, *id, mOptions.mContent.at(state.mRef.mRefNum.mContentFile), false, false});
            return true;
        });
        std::ranges::sort(result, {}, &PlacedInventory::mIdentity);
        return result;
    }

    std::vector<Loadout::PlacedInventory> Loadout::placedContainers(ESM::RefId cell)
    {
        validateCell(cell);
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        auto& loaded = world.getCell(cell);
        std::vector<PlacedInventory> result;
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
        std::ranges::sort(result, {}, &PlacedInventory::mIdentity);
        return result;
    }

    Loadout::PlacedInventory Loadout::resolveContainer(ESM::RefId cell, std::string_view plugin, uint32_t index)
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

    std::vector<Loadout::PlacedInventory> Loadout::placedActors(ESM::RefId cell)
    {
        validateCell(cell);
        MWClass::registerClasses();
        MWWorld::WorldModel world(mStore, mReaders, 1);
        auto& loaded = world.getCell(cell);
        std::vector<PlacedInventory> result;
        const auto visit = [&]<class T>(const MWWorld::Ptr& ptr) {
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
            if (result.size() == 4096)
                throw std::invalid_argument("Native placed actor discovery budget exceeded");
            ESM::ObjectState state;
            ptr.getCellRef().writeState(state);
            const auto id = MWWorld::placedRefId(state.mRef.mRefNum, mOptions.mContent);
            if (!id) throw std::invalid_argument("Native placed actor identity unavailable");
            for (float value : state.mRef.mPos.pos)
                if (!std::isfinite(value) || std::abs(double(value)) >= double(INT64_MAX) / 1024)
                    throw std::invalid_argument("Native placed actor position outside wire range");
            const auto* base = ptr.get<T>()->mBase;
            result.push_back({state.mRef, *id, mOptions.mContent.at(state.mRef.mRefNum.mContentFile),
                base->mInventory.mList.empty(), !base->mScript.empty()});
            return true;
        };
        loaded.forEachType<ESM::NPC>([&](const auto& ptr) { return visit.template operator()<ESM::NPC>(ptr); });
        loaded.forEachType<ESM::Creature>([&](const auto& ptr) { return visit.template operator()<ESM::Creature>(ptr); });
        loaded.forEachType<ESM::CreatureLevList>([&](const auto& ptr) {
            if (!ptr.getRefData().isEnabled() || ptr.getRefData().isDeletedByContentFile()) return true;
            if (result.size() == 4096) throw std::invalid_argument("Native placed actor discovery budget exceeded");
            ESM::ObjectState state;
            ptr.getCellRef().writeState(state);
            const auto id = MWWorld::placedRefId(state.mRef.mRefNum, mOptions.mContent);
            if (!id) throw std::invalid_argument("Native leveled actor identity unavailable");
            for (float value : state.mRef.mPos.pos)
                if (!std::isfinite(value) || std::abs(double(value)) >= double(INT64_MAX) / 1024)
                    throw std::invalid_argument("Native leveled actor position outside wire range");
            for (float value : state.mRef.mPos.rot)
                if (!std::isfinite(value)) throw std::invalid_argument("Native leveled actor rotation invalid");
            if (!std::isfinite(state.mRef.mScale) || state.mRef.mScale <= 0 || state.mRef.mCount != 1)
                throw std::invalid_argument("Native leveled actor scale or count invalid");
            result.push_back({state.mRef, *id, mOptions.mContent.at(state.mRef.mRefNum.mContentFile), false, false, true});
            return true;
        });
        std::ranges::sort(result, {}, &PlacedInventory::mIdentity);
        return result;
    }

    std::vector<Loadout::PlacedInventory> Loadout::resolveActors(ESM::RefId cell, size_t limit)
    {
        auto references = placedActors(cell);
        if (references.size() > limit) throw std::invalid_argument("Native interior actor inventory budget exceeded");
        for (const auto& ref : references)
            if (ref.mScripted || ref.mLeveled)
                throw std::invalid_argument("Native placed actor " + std::to_string(ref.mIdentity)
                    + " (" + ref.mRef.mRefID.toDebugString() + ") requires script services");
        return references;
    }

    std::vector<Loadout::PlacedInventory> Loadout::resolveActors(ESM::RefId cell, size_t limit, int level,
        Misc::Rng::Generator& rng, std::vector<ActorSpawnSelection>& selections,
        const std::vector<ActorSpawnSelection>* restored)
    {
        if (limit > 4096 || level < 1 || level > 1000 || selections.size() > MaximumActorSpawns)
            throw std::invalid_argument("Native actor selection bounds invalid");
        if (restored) validateActorSpawns(*restored);
        auto references = placedActors(cell);
        if (references.size() > limit) throw std::invalid_argument("Native actor placement budget exceeded");
        const auto records = MWWorld::actorRecords(mStore);
        auto stagedRng = rng;
        auto staged = selections;
        for (auto& placed : references)
        {
            if (placed.mLeveled)
            {
                ESM::RefId selected;
                if (restored)
                {
                    const auto saved = std::ranges::find(*restored, placed.mIdentity, &ActorSpawnSelection::mPlacement);
                    if (saved == restored->end()) throw std::invalid_argument("Missing saved leveled actor selection");
                    if (saved->mRecord)
                    {
                        const auto record = records.find(saved->mRecord);
                        if (record == records.end()) throw std::invalid_argument("Saved leveled actor record unavailable");
                        selected = record->second;
                    }
                }
                else
                    selected = MWMechanics::getLevelledItem(mStore.get<ESM::CreatureLevList>().find(placed.mRef.mRefID),
                        true, stagedRng, level, mStore);
                if (staged.size() == MaximumActorSpawns) throw std::invalid_argument("Native actor spawn budget exceeded");
                staged.push_back({placed.mIdentity, selected.empty() ? 0 : MWWorld::inventoryRecordId(selected)});
                // Stock spawning copies only the marker's pose/scale onto a fresh actor.
                ESM::CellRef actor; actor.blank();
                actor.mRefNum = placed.mRef.mRefNum; actor.mRefID = selected;
                actor.mPos = placed.mRef.mPos; actor.mScale = placed.mRef.mScale; actor.mCount = 1;
                placed.mRef = std::move(actor);
                if (selected.empty()) continue;
                if (const auto* npc = mStore.get<ESM::NPC>().search(selected))
                {
                    if (npc->mNpdtType != ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS && npc->mNpdt.mHealth < 1)
                        throw std::invalid_argument("Leveled authored-corpse presentation requires actor lifecycle services");
                    placed.mScripted = !npc->mScript.empty(); placed.mEmptyBase = npc->mInventory.mList.empty();
                }
                else if (const auto* creature = mStore.get<ESM::Creature>().search(selected))
                {
                    if (creature->mData.mHealth < 1)
                        throw std::invalid_argument("Leveled authored-corpse presentation requires actor lifecycle services");
                    placed.mScripted = !creature->mScript.empty(); placed.mEmptyBase = creature->mInventory.mList.empty();
                }
                else throw std::invalid_argument("Leveled actor selected a non-actor record");
            }
            if (placed.mScripted)
                throw std::invalid_argument("Native placed actor " + placed.mRef.mRefID.toDebugString() + " requires script services");
        }
        std::erase_if(references, [](const auto& placed) { return placed.mRef.mRefID.empty(); });
        rng = stagedRng;
        selections.swap(staged);
        return references;
    }

    std::vector<Loadout::PlacedInventory> Loadout::resolveContainers(ESM::RefId cell, size_t limit)
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
