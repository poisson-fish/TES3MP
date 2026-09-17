#include "equipment_runtime.hpp"
#include "runtime_phases.hpp"

#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    void EquipmentRuntime::installInventory(size_t actor, InventoryStore& candidate,
        ContainerStoreIterator shirt, ContainerStoreIterator selected, const Ptr& item,
        std::shared_ptr<EquipmentNpcStats>& stats, bool replaceStorage) noexcept
    {
        auto& live = mInventories[actor];
        auto& nodes = candidate.mLists.mClothes.mList;
        for (auto& node : nodes) node.mWorldModel = &mWorld;
        nodes.swap(live.mLists.mClothes.mList);
        if (replaceStorage) live.mStorageIdentity.swap(candidate.mStorageIdentity);
        live.mSlots[InventoryStore::Slot_Shirt] = shirt;
        live.mSelectedEnchantItem = selected;
        live.mRechargingItems.clear();
        live.mWeightUpToDate = live.mRechargingItemsUpToDate = false;
        live.mModified = true;
        mItems[actor] = item;
        mNpcStats[actor].swap(stats);
        // Retired nodes cannot deregister their replacements.
        for (auto& node : nodes) node.mWorldModel = nullptr;
    }

    void EquipmentRuntime::installPrepared(size_t actor, Installation& staged, InventoryStore& candidate) noexcept
    {
        installInventory(actor, candidate, staged.mShirt, staged.mSelected, staged.mItem,
            staged.mPrepared.installationNpcStats());
        auto& effects = mActorEffects[actor];
        effects.mListener.mCalls = staged.mEffects.mListener.mCalls;
        effects.mListener.mRemovals.swap(staged.mEffects.mListener.mRemovals);
        effects.mInventoryUpdates = staged.mEffects.mInventoryUpdates;
        effects.mNotifications.swap(staged.mEffects.mNotifications);
    }

    PersistenceResult EquipmentRuntime::persistSession(EquipmentSessionValues values, EquipmentFileSink& file,
        EquipmentBytes& bytes, FileFaults& faults) const
    {
        // Equipment may generate one split in either actor. The other actor's
        // image keeps its own fields and shares the resulting registry counter.
        auto counter = values.mActors[0].mLastGenerated;
        const auto other = values.mActors[1].mLastGenerated;
        if (other.mContentFile < counter.mContentFile
            || (other.mContentFile == counter.mContentFile && other.mIndex > counter.mIndex)) counter = other;
        std::vector<ESM::RefId> ids;
        for (size_t i = 0; i < 2; ++i)
        {
            auto& actor = values.mActors[i];
            actor.mLastGenerated = counter;
            actor.validate(mStore, mActors[i]->getPtr().getCellRef().getRefNum(), mScriptLocals.get());
            if (actor.mNpcStats)
            {
                ids.push_back(actor.mNpcStats->mBase);
                for (auto id : actor.mNpcStats->mSpells) if (!id.empty()) ids.push_back(id);
            }
            for (const auto& object : actor.mObjects)
                for (auto id : { object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul,
                         object.mRef.mFaction, object.mRef.mKey, object.mRef.mTrap })
                    if (!id.empty()) ids.push_back(id);
        }
        const std::array envelopes{ expectedEnvelope(values.mActors[0].mActor), expectedEnvelope(values.mActors[1].mActor) };
        const std::array<EquipmentBindings, 2> bindings{{
            { envelopes[0], mStore, ids, mScriptLocals }, { envelopes[1], mStore, ids, mScriptLocals } }};
        return file.writeSession(values, bindings, bytes, faults);
    }

    InventoryTransferCommand EquipmentRuntime::transferCommand(size_t source, InventoryInstanceId item, int quantity) const
    {
        if (source >= 2) throw std::invalid_argument("Transfer actor index out of range");
        const auto from = ownedId(mActors[source]->getPtr().getCellRef().getRefNum());
        return { from, ownedId(mActors[1 - source]->getPtr().getCellRef().getRefNum()), from, item,
            quantity, mWorld.getPtrRegistryRevision() };
    }

    PersistenceResult EquipmentRuntime::execute(InventoryTransferCaller caller, InventoryTransferCommand command,
        EquipmentFileSink& file, std::unique_ptr<const InventoryTransferSuccess>& output,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        if (mFailedClosed || file.failedClosed())
        {
            mFailedClosed = true;
            return PersistenceResult::Uncertain;
        }
        const auto id = [](InventoryInstanceId value) { return ESM::RefNum{ value.mIndex, value.mContentFile }; };
        if (!mConnected || !file.session() || mRestartActor
            || command.mExpectedRevision >= std::numeric_limits<size_t>::max() - 1
            || mWorld.mPtrRegistry.mIndex.size() > 2 * PlainEquipmentValues::MaxItems + 2)
            throw std::invalid_argument("Transfer persistence, recovery, revision or registry bound invalid");
        const size_t source = validateInventoryTransferIntent(caller, command,
            { ownedId(mActors[0]->getPtr().getCellRef().getRefNum()), ownedId(mActors[1]->getPtr().getCellRef().getRefNum()) },
            mWorld.getPtrRegistryRevision(), ownedId(mWorld.getLastGeneratedRefNum()));
        const size_t destination = 1 - source;
        for (size_t i = 0; i < 2; ++i)
        {
            const auto actor = mActors[i]->getPtr();
            validateCaller(i, mWorld.getPtr(actor.getCellRef().getRefNum()));
        }
        const auto item = mWorld.getPtr(id(command.mItem));
        if (!item.hasLiveReference() || item.getContainerStore() != &mInventories[source])
            throw std::invalid_argument("Transfer item is not owned by the source actor");
        phase.set(Phase::Preparation);
        auto prepared = PreparedPlainEquipment::prepareTransfer(
            { ContainerStoreResolution(mInventories[source], mActors[source]->getPtr()),
                ContainerStoreResolution(mInventories[destination], mActors[destination]->getPtr()) },
            item, id(command.mItem), command.mExpectedRevision, command.mQuantity,
            { preparationContext(source), preparationContext(destination) });
        std::array<std::unique_ptr<Installation>, 2> staged;
        staged[source] = stageInstallation(source, mActors[source]->getPtr(), std::move(prepared[0]));
        staged[destination] = stageInstallation(destination, mActors[destination]->getPtr(), std::move(prepared[1]));
        std::array<InventoryStore*, 2> candidates;
        for (size_t i = 0; i < 2; ++i)
            candidates[i] = &staged[i]->mPrepared.installationCandidate(preparationContext(i), mInventories[i]);
        // Combine relocation before durability. A per-actor candidate map still
        // points to the other's original nodes and must never become canonical.
        auto registry = mWorld.mPtrRegistry.mIndex;
        for (size_t i = 0; i < 2; ++i)
            for (const auto& object : staged[i]->mSaved.mObjects)
                registry.insert_or_assign(object.mRef.mRefNum, staged[i]->mRegistry.at(object.mRef.mRefNum));
        const auto revision = staged[destination]->mRevision;
        const auto& from = staged[source]->mPrepared.result();
        const auto& to = staged[destination]->mPrepared.result();
        const auto count = [](const PlainEquipmentValues& values, ESM::RefNum id) {
            for (const auto& object : values.mObjects) if (object.mRef.mRefNum == id) return object.mRef.mCount;
            throw std::logic_error("Transfer result identity missing");
        };
        phase.set(Phase::Result);
        auto success = std::make_unique<const InventoryTransferSuccess>(inventoryTransferSuccess(command,
            ownedId(to.mTransferred), count(staged[source]->mSaved, from.mTransferred),
            count(staged[destination]->mSaved, to.mTransferred), revision,
            ownedId(from.mSelected), ownedId(to.mSelected), true, true));
        phase.set(Phase::Revalidation);
        for (size_t i = 0; i < 2; ++i) staged[i]->mPrepared.validate(preparationContext(i));
        EquipmentBytes encoded;
        phase.set(Phase::Persistence);
        const auto result = persistSession({ { staged[0]->mSaved, staged[1]->mSaved }, revision }, file, encoded, faults);
        if (result != PersistenceResult::Accepted)
        {
            mFailedClosed = result == PersistenceResult::Uncertain;
            return result;
        }
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            for (size_t i = 0; i < 2; ++i) installPrepared(i, *staged[i], *candidates[i]);
            mWorld.mPtrRegistry.mIndex.swap(registry);
            mWorld.mPtrRegistry.mRevision = revision;
            mWorld.mPtrRegistry.mLastGenerated = to.mLastGenerated;
            phase.set(Phase::Publication);
            output.swap(success);
            bytes.swap(encoded);
        };
        install();
        phase.set(Phase::Retirement);
        return result;
    }

    FileReadResult EquipmentRuntime::restartSession(const std::filesystem::path& path,
        std::span<const ESM::RefId> referenceIds, std::unique_ptr<const EquipmentSessionValues>& output,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        if (!mConnected || !mRestartActor || *mRestartActor != 2 || mFailedClosed)
            throw std::invalid_argument("Session recovery requires a fresh connected runtime");
        const std::array envelopes{ expectedEnvelope(mActors[0]->getPtr().getCellRef().getRefNum()),
            expectedEnvelope(mActors[1]->getPtr().getCellRef().getRefNum()) };
        const std::array<EquipmentBindings, 2> bindings{{ { envelopes[0], mStore, referenceIds, mScriptLocals },
            { envelopes[1], mStore, referenceIds, mScriptLocals } }};
        auto fresh = restartBindings(mWorld.getLastGeneratedRefNum());
        for (size_t i = 0; i < 2; ++i) validateRestart(i, mActors[i]->getPtr(), bindings[i], fresh);
        if (fresh.mRegistry.size() != 2)
            throw std::invalid_argument("Session recovery requires exactly its two registered actors");
        phase.set(Phase::Preparation);
        EquipmentBytes accepted;
        const auto result = readBoundedFile(path, MaxEquipmentSessionBytes, accepted, faults);
        if (result != FileReadResult::Read) return result;
        EquipmentSessionValues values;
        decodeEquipmentSession(accepted, bindings, values);
        EquipmentBytes canonical;
        encodeEquipmentSession(values, bindings, canonical);
        if (canonical != accepted) throw std::invalid_argument("Noncanonical equipment session image");
        fresh.mSavedCounter = values.mActors[0].mLastGenerated;
        std::array<std::unique_ptr<RestartInstallation>, 2> staged;
        std::array<InventoryStore*, 2> candidates;
        auto registry = fresh.mRegistry;
        for (size_t i = 0; i < 2; ++i)
        {
            std::unique_ptr<const RestoredPlainEquipment> restored = std::make_unique<RestoredPlainEquipment>(
                RestoredPlainEquipment::restore(values.mActors[i], mStore, envelopes[i].mActor, mScriptLocals));
            staged[i] = stageRestart(i, mActors[i]->getPtr(), bindings[i], fresh, restored);
            candidates[i] = &staged[i]->mRestored->installationCandidate(mStore, envelopes[i].mActor, fresh.mSavedCounter);
            for (const auto& object : values.mActors[i].mObjects)
                if (!registry.emplace(object.mRef.mRefNum, staged[i]->mRegistry.at(object.mRef.mRefNum)).second)
                    throw std::invalid_argument("Session recovery registry identity collision");
        }
        phase.set(Phase::Result);
        auto saved = std::make_unique<const EquipmentSessionValues>(std::move(values));
        phase.set(Phase::Revalidation);
        for (size_t i = 0; i < 2; ++i) validateRestart(i, mActors[i]->getPtr(), bindings[i], fresh);
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            for (size_t i = 0; i < 2; ++i)
                installInventory(i, *candidates[i], staged[i]->mShirt, staged[i]->mSelected, staged[i]->mItem, staged[i]->mNpcStats, true);
            mWorld.mPtrRegistry.mIndex.swap(registry);
            mWorld.mPtrRegistry.mRevision = saved->mRevision;
            mWorld.mPtrRegistry.mLastGenerated = fresh.mSavedCounter;
            mRestartActor.reset();
            phase.set(Phase::Publication);
            output.swap(saved);
            bytes.swap(accepted);
        };
        install();
        phase.set(Phase::Retirement);
        return result;
    }
}
