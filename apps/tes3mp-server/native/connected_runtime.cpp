#include "equipment_runtime.hpp"
#include "runtime_phases.hpp"

#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    ContainerStore& EquipmentRuntime::storage(size_t owner)
    {
        if (owner == 2 && mContainer) return mContainerStore;
        return mInventories.at(owner);
    }
    const ContainerStore& EquipmentRuntime::storage(size_t owner) const
    {
        if (owner == 2 && mContainer) return mContainerStore;
        return mInventories.at(owner);
    }
    Ptr EquipmentRuntime::ownerPtr(size_t owner) const
    {
        if (owner == 2 && mContainer) return mContainer->getPtr();
        return mActors.at(owner)->getPtr();
    }
    EquipmentRuntime::ActorEffects& EquipmentRuntime::effects(size_t owner)
    {
        return owner == 2 ? mContainerEffects : mActorEffects.at(owner);
    }
    const EquipmentRuntime::ActorEffects& EquipmentRuntime::effects(size_t owner) const
    {
        return owner == 2 ? mContainerEffects : mActorEffects.at(owner);
    }

    void EquipmentRuntime::installStorage(ContainerStore& live, ContainerStore& candidate, bool replaceStorage) noexcept
    {
        auto& nodes = candidate.mLists.mClothes.mList;
        for (auto& node : nodes) node.mWorldModel = &mWorld;
        nodes.swap(live.mLists.mClothes.mList);
        if (replaceStorage) live.mStorageIdentity.swap(candidate.mStorageIdentity);
        live.mRechargingItems.clear();
        live.mWeightUpToDate = live.mRechargingItemsUpToDate = false;
        live.mModified = true;
        // Retired nodes cannot deregister their replacements.
        for (auto& node : nodes) node.mWorldModel = nullptr;
    }

    void EquipmentRuntime::installInventory(size_t actor, InventoryStore& candidate,
        ContainerStoreIterator shirt, ContainerStoreIterator selected, const Ptr& item,
        std::shared_ptr<EquipmentNpcStats>& stats, bool replaceStorage) noexcept
    {
        auto& live = mInventories[actor];
        installStorage(live, candidate, replaceStorage);
        live.mSlots[InventoryStore::Slot_Shirt] = shirt;
        live.mSelectedEnchantItem = selected;
        mItems[actor] = item;
        mNpcStats[actor].swap(stats);
    }

    void EquipmentRuntime::installPrepared(size_t actor, Installation& staged, ContainerStore& candidate) noexcept
    {
        if (actor == 2) installStorage(mContainerStore, candidate, false);
        else installInventory(actor, static_cast<InventoryStore&>(candidate), staged.mShirt, staged.mSelected, staged.mItem,
            staged.mPrepared.installationNpcStats());
        auto& pending = effects(actor);
        pending.mListener.mCalls = staged.mEffects.mListener.mCalls;
        pending.mListener.mRemovals.swap(staged.mEffects.mListener.mRemovals);
        pending.mInventoryUpdates = staged.mEffects.mInventoryUpdates;
        pending.mNotifications.swap(staged.mEffects.mNotifications);
    }

    PersistenceResult EquipmentRuntime::persistSession(EquipmentSessionValues values, EquipmentFileSink& file,
        EquipmentBytes& bytes, FileFaults& faults) const
    {
        // Equipment may generate one split in either actor. The other actor's
        // image keeps its own fields and shares the resulting registry counter.
        if (mContainer && !values.mContainer) values.mContainer = installedValues(2);
        auto counter = values.mActors[0].mLastGenerated;
        const auto other = values.mActors[1].mLastGenerated;
        if (other.mContentFile < counter.mContentFile
            || (other.mContentFile == counter.mContentFile && other.mIndex > counter.mIndex)) counter = other;
        if (values.mContainer)
        {
            const auto shared = values.mContainer->mLastGenerated;
            if (shared.mContentFile < counter.mContentFile
                || (shared.mContentFile == counter.mContentFile && shared.mIndex > counter.mIndex)) counter = shared;
        }
        std::vector<ESM::RefId> ids;
        for (size_t i = 0; i < (mContainer ? 3 : 2); ++i)
        {
            auto& actor = i == 2 ? *values.mContainer : values.mActors[i];
            actor.mLastGenerated = counter;
            actor.validate(mStore, ownerPtr(i).getCellRef().getRefNum(), mScriptLocals.get());
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
        const auto containerEnvelope = mContainer ? expectedEnvelope(ownerPtr(2).getCellRef().getRefNum()) : EquipmentEnvelope{};
        const EquipmentBindings containerBinding{ containerEnvelope, mStore, ids, mScriptLocals };
        return file.writeSession(values, bindings, bytes, faults, mContainer ? &containerBinding : nullptr);
    }

    InventoryTransferCommand EquipmentRuntime::transferCommand(size_t source, InventoryInstanceId item, int quantity) const
    {
        if (source >= 2) throw std::invalid_argument("Transfer actor index out of range");
        const auto from = ownedId(mActors[source]->getPtr().getCellRef().getRefNum());
        return { from, ownedId(mActors[1 - source]->getPtr().getCellRef().getRefNum()), from, item,
            quantity, mWorld.getPtrRegistryRevision() };
    }

    InventoryTransferCommand EquipmentRuntime::containerCommand(size_t actor, bool drop,
        InventoryInstanceId item, int quantity) const
    {
        if (actor >= 2 || !mContainer) throw std::invalid_argument("Container command requires a bound actor and container");
        const auto player = ownedId(ownerPtr(actor).getCellRef().getRefNum());
        const auto container = ownedId(ownerPtr(2).getCellRef().getRefNum());
        return { drop ? player : container, drop ? container : player, player, item,
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
            || mWorld.mPtrRegistry.mIndex.size() > registryBound())
            throw std::invalid_argument("Transfer persistence, recovery, revision or registry bound invalid");
        const auto index = [&](InventoryInstanceId value) {
            for (size_t i = 0; i < (mContainer ? 3 : 2); ++i)
                if (ownedId(ownerPtr(i).getCellRef().getRefNum()) == value) return i;
            throw std::invalid_argument("Transfer owner is not bound to this runtime");
        };
        const size_t source = index(command.mSourceOwner), destination = index(command.mDestinationOwner);
        const size_t initiator = index(caller.mInitiator);
        if (initiator >= 2 || (source == 2 && destination != initiator)
            || (destination == 2 && source != initiator))
            throw std::invalid_argument("Container intent requires its participating actor as trusted caller");
        validateInventoryTransferIntent(caller, command, { command.mSourceOwner, command.mDestinationOwner },
            mWorld.getPtrRegistryRevision(), ownedId(mWorld.getLastGeneratedRefNum()));
        for (size_t i = 0; i < (mContainer ? 3 : 2); ++i)
            validateCaller(i, mWorld.getPtr(ownerPtr(i).getCellRef().getRefNum()));
        const auto item = mWorld.getPtr(id(command.mItem));
        if (!item.hasLiveReference() || item.getContainerStore() != &storage(source))
            throw std::invalid_argument("Transfer item is not owned by the source");
        const std::array owners{ source, destination };
        const std::array contexts{ preparationContext(source, initiator), preparationContext(destination, initiator) };
        phase.set(Phase::Preparation);
        auto prepared = PreparedPlainEquipment::prepareTransfer(
            { ContainerStoreResolution(storage(source), ownerPtr(source)),
                ContainerStoreResolution(storage(destination), ownerPtr(destination)) },
            item, id(command.mItem), command.mExpectedRevision, command.mQuantity, contexts);
        std::array<std::unique_ptr<Installation>, 2> staged;
        std::array<ContainerStore*, 2> candidates;
        auto registry = mWorld.mPtrRegistry.mIndex;
        EquipmentSessionValues values{ { installedValues(0), installedValues(1) } };
        for (size_t i = 0; i < 2; ++i)
        {
            const auto owner = owners[i];
            staged[i] = stageInstallation(owner, ownerPtr(owner), std::move(prepared[i]), initiator);
            candidates[i] = &staged[i]->mPrepared.installationCandidate(contexts[i], storage(owner));
            for (const auto& object : staged[i]->mSaved.mObjects)
                registry.insert_or_assign(object.mRef.mRefNum, staged[i]->mRegistry.at(object.mRef.mRefNum));
            if (owner == 2) values.mContainer = staged[i]->mSaved;
            else values.mActors[owner] = staged[i]->mSaved;
        }
        const auto revision = staged[1]->mRevision;
        values.mRevision = revision;
        const auto& from = staged[0]->mPrepared.result();
        const auto& to = staged[1]->mPrepared.result();
        const auto count = [](const PlainEquipmentValues& values, ESM::RefNum identity) {
            for (const auto& object : values.mObjects) if (object.mRef.mRefNum == identity) return object.mRef.mCount;
            throw std::logic_error("Transfer result identity missing");
        };
        phase.set(Phase::Result);
        auto success = std::make_unique<const InventoryTransferSuccess>(inventoryTransferSuccess(command,
            ownedId(to.mTransferred), count(staged[0]->mSaved, from.mTransferred),
            count(staged[1]->mSaved, to.mTransferred), revision,
            ownedId(from.mSelected), ownedId(to.mSelected), true, true));
        phase.set(Phase::Revalidation);
        for (size_t i = 0; i < 2; ++i) staged[i]->mPrepared.validate(contexts[i]);
        EquipmentBytes encoded;
        phase.set(Phase::Persistence);
        const auto result = persistSession(std::move(values), file, encoded, faults);
        if (result != PersistenceResult::Accepted)
        {
            mFailedClosed = result == PersistenceResult::Uncertain;
            return result;
        }
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            for (size_t i = 0; i < 2; ++i) installPrepared(owners[i], *staged[i], *candidates[i]);
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
        if (fresh.mRegistry.size() != (mContainer ? 3 : 2))
            throw std::invalid_argument("Session recovery requires exactly its registered owners");
        phase.set(Phase::Preparation);
        EquipmentBytes accepted;
        const auto result = readBoundedFile(path, MaxEquipmentSessionBytes, accepted, faults);
        if (result != FileReadResult::Read) return result;
        EquipmentSessionValues values;
        const auto containerEnvelope = mContainer ? expectedEnvelope(ownerPtr(2).getCellRef().getRefNum()) : EquipmentEnvelope{};
        const EquipmentBindings containerBinding{ containerEnvelope, mStore, referenceIds, mScriptLocals };
        const auto* sharedBinding = mContainer ? &containerBinding : nullptr;
        decodeEquipmentSession(accepted, bindings, values, sharedBinding);
        EquipmentBytes canonical;
        encodeEquipmentSession(values, bindings, canonical, sharedBinding);
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
        std::optional<RestoredPlainEquipment> container;
        ContainerStore* sharedCandidate = nullptr;
        if (values.mContainer)
        {
            container.emplace(RestoredPlainEquipment::restore(*values.mContainer, mStore,
                ownerPtr(2).getCellRef().getRefNum(), mScriptLocals, true));
            sharedCandidate = &container->installationStorage(mStore, ownerPtr(2).getCellRef().getRefNum(), fresh.mSavedCounter);
            for (auto& node : sharedCandidate->mLists.mClothes.mList)
            {
                Ptr ptr(&node, nullptr);
                ptr.mContainerStore = &mContainerStore;
                if (!registry.emplace(node.mRef.getRefNum(), ptr).second)
                    throw std::invalid_argument("Container recovery identity collision");
            }
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
            if (sharedCandidate) installStorage(mContainerStore, *sharedCandidate, true);
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
