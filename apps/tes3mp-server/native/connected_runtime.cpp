#include "equipment_runtime.hpp"
#include "actor_inventory.hpp"
#include "runtime_phases.hpp"

#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    ContainerStore& EquipmentRuntime::storage(size_t owner)
    {
        if (owner >= 2) return *mContainers.at(owner - 2)->mStore;
        return mInventories.at(owner);
    }
    const ContainerStore& EquipmentRuntime::storage(size_t owner) const
    {
        if (owner >= 2) return *mContainers.at(owner - 2)->mStore;
        return mInventories.at(owner);
    }
    Ptr EquipmentRuntime::ownerPtr(size_t owner) const
    {
        if (owner >= 2) return mContainers.at(owner - 2)->mReference->getPtr();
        return mActors.at(owner)->getPtr();
    }
    InventoryStore* EquipmentRuntime::inventoryStorage(size_t owner)
    {
        return dynamic_cast<InventoryStore*>(&storage(owner));
    }
    const InventoryStore* EquipmentRuntime::inventoryStorage(size_t owner) const
    {
        return dynamic_cast<const InventoryStore*>(&storage(owner));
    }
    EquipmentRuntime::ActorEffects& EquipmentRuntime::effects(size_t owner)
    {
        return owner >= 2 ? mContainers.at(owner - 2)->mEffects : mActorEffects.at(owner);
    }
    const EquipmentRuntime::ActorEffects& EquipmentRuntime::effects(size_t owner) const
    {
        return owner >= 2 ? mContainers.at(owner - 2)->mEffects : mActorEffects.at(owner);
    }

    void EquipmentRuntime::installStorage(ContainerStore& live, ContainerStore& candidate, bool replaceStorage) noexcept
    {
        candidate.forEachStored([&](auto& node, auto) { node.mWorldModel = &mWorld; });
        const auto swapLists = [&]<size_t... I>(std::index_sequence<I...>) {
            (std::get<I>(candidate.mLists.all()).mList.swap(std::get<I>(live.mLists.all()).mList), ...);
        };
        swapLists(std::make_index_sequence<12>{});
        if (replaceStorage) live.mStorageIdentity.swap(candidate.mStorageIdentity);
        live.mRechargingItems.clear();
        live.mWeightUpToDate = live.mRechargingItemsUpToDate = false;
        live.mModified = true;
        // Retired nodes cannot deregister their replacements.
        candidate.forEachStored([](auto& node, auto) { node.mWorldModel = nullptr; });
    }

    void EquipmentRuntime::installInventory(size_t actor, InventoryStore& candidate,
        const std::vector<ContainerStoreIterator>& slots, ContainerStoreIterator selected, const Ptr& item,
        std::shared_ptr<EquipmentNpcStats>& stats, bool replaceStorage) noexcept
    {
        auto& live = mInventories[actor];
        installStorage(live, candidate, replaceStorage);
        std::copy(slots.begin(), slots.end(), live.mSlots.begin());
        live.mSelectedEnchantItem = selected;
        mItems[actor] = item;
        mNpcStats[actor].swap(stats);
    }

    void EquipmentRuntime::installPrepared(size_t actor, Installation& staged, ContainerStore& candidate) noexcept
    {
        if (actor >= 2)
        {
            installStorage(storage(actor), candidate, false);
            if (auto* inventory = inventoryStorage(actor))
            {
                std::copy(staged.mSlots.begin(), staged.mSlots.end(), inventory->mSlots.begin());
                inventory->mSelectedEnchantItem = staged.mSelected;
            }
        }
        else installInventory(actor, static_cast<InventoryStore&>(candidate), staged.mSlots, staged.mSelected, staged.mItem,
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
        EquipmentBytes staged;
        encodeSession(std::move(values), staged);
        const auto result = file.writeSessionImage(staged, faults);
        if (result == PersistenceResult::Accepted) bytes.swap(staged);
        return result;
    }

    void EquipmentRuntime::encodeSession(EquipmentSessionValues values, EquipmentBytes& bytes) const
    {
        // Equipment may generate one split in either actor. The other actor's
        // image keeps its own fields and shares the resulting registry counter.
        if (values.mContainers.empty())
            for (size_t i = 2; i < ownerCount(); ++i) values.mContainers.push_back(installedValues(i));
        if (values.mContainers.size() != mContainers.size())
            throw std::invalid_argument("Session container count changed");
        if (!values.mWorldItems) values.mWorldItems = mWorldItems;
        if (!values.mWorldCells) values.mWorldCells = mWorldCells;
        if (!values.mDoor) values.mDoor = mDoorState;
        auto counter = values.mActors[0].mLastGenerated;
        const auto other = values.mActors[1].mLastGenerated;
        if (other.mContentFile < counter.mContentFile
            || (other.mContentFile == counter.mContentFile && other.mIndex > counter.mIndex)) counter = other;
        for (const auto& container : values.mContainers)
        {
            const auto shared = container.mLastGenerated;
            if (shared.mContentFile < counter.mContentFile
                || (shared.mContentFile == counter.mContentFile && shared.mIndex > counter.mIndex)) counter = shared;
        }
        std::vector<ESM::RefId> ids;
        for (size_t i = 0; i < ownerCount(); ++i)
        {
            auto& actor = i >= 2 ? values.mContainers[i - 2] : values.mActors[i];
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
        if (values.mWorldItems)
        {
            values.mWorldItems->mLastGenerated = counter;
            for (const auto& object : values.mWorldItems->mObjects)
                for (auto id : {object.mRef.mRefID, object.mRef.mOwner, object.mRef.mSoul,
                         object.mRef.mFaction, object.mRef.mKey, object.mRef.mTrap})
                    if (!id.empty()) ids.push_back(id);
        }
        validateWorldItems(values);
        const std::array envelopes{ expectedEnvelope(values.mActors[0].mActor), expectedEnvelope(values.mActors[1].mActor) };
        const std::array<EquipmentBindings, 2> bindings{{
            { envelopes[0], mStore, ids, mScriptLocals }, { envelopes[1], mStore, ids, mScriptLocals } }};
        std::vector<EquipmentEnvelope> containerEnvelopes;
        for (size_t i = 2; i < ownerCount(); ++i)
            containerEnvelopes.push_back(expectedEnvelope(ownerPtr(i).getCellRef().getRefNum()));
        std::vector<EquipmentBindings> containerBindings;
        for (size_t i = 0; i < containerEnvelopes.size(); ++i)
            containerBindings.push_back({containerEnvelopes[i], mStore, ids, mScriptLocals, inventoryStorage(i + 2) != nullptr});
        encodeEquipmentSession(values, bindings, bytes, containerBindings, mWorldItems ? &bindings[0] : nullptr,
            mDoorBinding ? &*mDoorBinding : nullptr, mWorldCells.has_value());
    }

    InventoryTransferCommand EquipmentRuntime::transferCommand(size_t source, InventoryInstanceId item, int quantity) const
    {
        if (source >= 2) throw std::invalid_argument("Transfer actor index out of range");
        const auto from = ownedId(mActors[source]->getPtr().getCellRef().getRefNum());
        return { from, ownedId(mActors[1 - source]->getPtr().getCellRef().getRefNum()), from, item,
            quantity, mWorld.getPtrRegistryRevision() };
    }

    InventoryTransferCommand EquipmentRuntime::containerCommand(size_t actor, bool drop,
        InventoryInstanceId item, int quantity, size_t shared) const
    {
        if (actor >= 2 || shared >= mContainers.size()) throw std::invalid_argument("Container command requires a bound actor and container");
        const auto player = ownedId(ownerPtr(actor).getCellRef().getRefNum());
        const auto container = ownedId(ownerPtr(2 + shared).getCellRef().getRefNum());
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
        if (!file.session()) throw std::invalid_argument("Transfer requires a session sink");
        auto prepared = prepare(caller, command);
        EquipmentFileCommitter durability(file, faults);
        return commit(prepared, durability, output, bytes);
    }

    struct EquipmentRuntime::PreparedTransfer::State
    {
        const EquipmentRuntime* mOwner;
        std::weak_ptr<const void> mLifetime;
        std::shared_ptr<const ESM::DoorState> mDoor;
        size_t mInitiator;
        std::array<size_t, 2> mOwners;
        std::array<std::unique_ptr<Installation>, 2> mStaged;
        PtrRegistry::Index mRegistry;
        std::unique_ptr<const InventoryTransferSuccess> mSuccess;
        EquipmentBytes mImage;
    };
    EquipmentRuntime::PreparedTransfer::PreparedTransfer(std::unique_ptr<State> state) : mState(std::move(state)) {}
    EquipmentRuntime::PreparedTransfer::PreparedTransfer(PreparedTransfer&&) noexcept = default;
    EquipmentRuntime::PreparedTransfer& EquipmentRuntime::PreparedTransfer::operator=(PreparedTransfer&&) noexcept = default;
    EquipmentRuntime::PreparedTransfer::~PreparedTransfer() = default;
    const InventoryTransferSuccess& EquipmentRuntime::PreparedTransfer::candidate() const
    {
        if (!mState || !mState->mSuccess) throw std::invalid_argument("Consumed transfer preparation");
        return *mState->mSuccess;
    }
    std::span<const char> EquipmentRuntime::PreparedTransfer::image() const
    {
        (void)candidate();
        return mState->mImage;
    }

    PlainEquipmentValues EquipmentRuntime::preparedValues(const PreparedTransfer& prepared, size_t owner) const
    {
        (void)prepared.candidate();
        const auto& state = *prepared.mState;
        if (state.mOwner != this || state.mLifetime.lock() != mLifetime || state.mDoor != mDoorState
            || state.mSuccess->mCommand.mExpectedRevision != mWorld.getPtrRegistryRevision())
            throw std::invalid_argument("Native projection candidate is stale or foreign");
        for (size_t i = 0; i < 2; ++i)
            if (state.mOwners[i] == owner) return state.mStaged[i]->mSaved;
        return installedValues(owner);
    }

    EquipmentRuntime::PreparedTransfer EquipmentRuntime::prepare(InventoryTransferCaller caller, InventoryTransferCommand command)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        const auto id = [](InventoryInstanceId value) { return ESM::RefNum{ value.mIndex, value.mContentFile }; };
        if (mFailedClosed || !mConnected || mRestartActor
            || command.mExpectedRevision >= std::numeric_limits<size_t>::max() - 1
            || mWorld.mPtrRegistry.mIndex.size() > registryBound())
            throw std::invalid_argument("Transfer persistence, recovery, revision or registry bound invalid");
        const auto index = [&](InventoryInstanceId value) {
            for (size_t i = 0; i < ownerCount(); ++i)
                if (ownedId(ownerPtr(i).getCellRef().getRefNum()) == value) return i;
            throw std::invalid_argument("Transfer owner is not bound to this runtime");
        };
        const size_t source = index(command.mSourceOwner), destination = index(command.mDestinationOwner);
        const size_t initiator = index(caller.mInitiator);
        if (command.mTakeAll && (source < 2 || destination != initiator || command.mQuantity != 1))
            throw std::invalid_argument("Take All requires a shared source and participating player");
        if (initiator >= 2 || (source >= 2 && destination != initiator)
            || (destination >= 2 && source != initiator))
            throw std::invalid_argument("Container intent requires its participating actor as trusted caller");
        for (const auto owner : {source, destination})
            if (owner >= 2 && actorInventory(ownerPtr(owner)) && !initialCorpse(ownerPtr(owner)))
                throw std::invalid_argument("Living actor inventory access requires gameplay services");
        validateInventoryTransferIntent(caller, command, { command.mSourceOwner, command.mDestinationOwner },
            mWorld.getPtrRegistryRevision(), ownedId(mWorld.getLastGeneratedRefNum()));
        for (size_t i = 0; i < ownerCount(); ++i)
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
            item, id(command.mItem), command.mExpectedRevision, command.mQuantity, contexts,
            source >= 2 && initialCorpse(ownerPtr(source)), command.mTakeAll);
        std::array<std::unique_ptr<Installation>, 2> staged;
        auto registry = mWorld.mPtrRegistry.mIndex;
        EquipmentSessionValues values{ { installedValues(0), installedValues(1) } };
        for (size_t i = 2; i < ownerCount(); ++i) values.mContainers.push_back(installedValues(i));
        for (size_t i = 0; i < 2; ++i)
        {
            const auto owner = owners[i];
            staged[i] = stageInstallation(owner, ownerPtr(owner), std::move(prepared[i]), initiator);
            (void)staged[i]->mPrepared.installationCandidate(contexts[i], storage(owner));
            storage(owner).forEachStored([&](const auto& ref, auto) { registry.erase(ref.mRef.getRefNum()); });
            for (const auto& object : staged[i]->mSaved.mObjects)
                registry.insert_or_assign(object.mRef.mRefNum, staged[i]->mRegistry.at(object.mRef.mRefNum));
            if (owner >= 2) values.mContainers[owner - 2] = staged[i]->mSaved;
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
        auto success = std::make_unique<InventoryTransferSuccess>(inventoryTransferSuccess(command,
            ownedId(to.mTransferred), count(staged[0]->mSaved, from.mTransferred),
            count(staged[1]->mSaved, to.mTransferred), revision,
            ownedId(from.mSelected), ownedId(to.mSelected), true, true));
        if (command.mTakeAll) success->mNotifications = {};
        for (const auto& transfer : from.mBulkTransfers)
        {
            auto itemCommand = command;
            itemCommand.mItem = ownedId(transfer.mSource);
            itemCommand.mQuantity = transfer.mCount;
            const auto item = inventoryTransferSuccess(itemCommand, ownedId(transfer.mDestination), 0,
                count(staged[1]->mSaved, transfer.mDestination), revision,
                ownedId(from.mSelected), ownedId(to.mSelected), true, true);
            for (const auto& notification : item.mNotifications)
                if (notification) success->mBulkNotifications.push_back(*notification);
        }
        phase.set(Phase::Revalidation);
        for (size_t i = 0; i < 2; ++i) staged[i]->mPrepared.validate(contexts[i]);
        EquipmentBytes encoded;
        encodeSession(std::move(values), encoded);
        auto state = std::make_unique<PreparedTransfer::State>();
        state->mOwner = this;
        state->mLifetime = mLifetime;
        state->mDoor = mDoorState;
        state->mInitiator = initiator;
        state->mOwners = owners;
        state->mStaged = std::move(staged);
        state->mRegistry = std::move(registry);
        state->mSuccess = std::move(success);
        state->mImage = std::move(encoded);
        return PreparedTransfer(std::move(state));
    }

    PersistenceResult EquipmentRuntime::commit(PreparedTransfer& prepared, EquipmentSessionCommitter& durability,
        std::unique_ptr<const InventoryTransferSuccess>& output, EquipmentBytes& bytes)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        if (mFailedClosed) return PersistenceResult::Uncertain;
        if (!prepared.mState || prepared.mState->mOwner != this
            || prepared.mState->mLifetime.lock() != mLifetime || prepared.mState->mDoor != mDoorState || !prepared.mState->mSuccess || mRestartActor)
            throw std::invalid_argument("Transfer preparation does not belong to this live runtime");
        auto& state = *prepared.mState;
        const auto& command = state.mSuccess->mCommand;
        if (mWorld.getPtrRegistryRevision() != command.mExpectedRevision)
            throw std::invalid_argument("Transfer preparation is stale");
        for (size_t i = 0; i < ownerCount(); ++i)
            validateCaller(i, mWorld.getPtr(ownerPtr(i).getCellRef().getRefNum()));
        std::array<ContainerStore*, 2> candidates;
        phase.set(Phase::Revalidation);
        for (size_t i = 0; i < 2; ++i)
        {
            const auto owner = state.mOwners[i];
            const auto context = preparationContext(owner, state.mInitiator);
            state.mStaged[i]->mPrepared.validate(context);
            candidates[i] = &state.mStaged[i]->mPrepared.installationCandidate(context, storage(owner));
        }
        const auto revision = state.mSuccess->mRevision;
        const auto counter = state.mStaged[1]->mPrepared.result().mLastGenerated;
        phase.set(Phase::Persistence);
        const auto result = durability.commit(state.mImage);
        if (result != PersistenceResult::Accepted)
        {
            mFailedClosed = result == PersistenceResult::Uncertain;
            return result;
        }
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            for (size_t i = 0; i < 2; ++i) installPrepared(state.mOwners[i], *state.mStaged[i], *candidates[i]);
            mWorld.mPtrRegistry.mIndex.swap(state.mRegistry);
            mWorld.mPtrRegistry.mRevision = revision;
            mWorld.mPtrRegistry.mLastGenerated = counter;
            phase.set(Phase::Publication);
            output.swap(state.mSuccess);
            bytes.swap(state.mImage);
        };
        install();
        phase.set(Phase::Retirement);
        prepared.mState.reset();
        return result;
    }

    struct EquipmentRuntime::PreparedEquipment::State
    {
        const EquipmentRuntime* mOwner;
        std::weak_ptr<const void> mLifetime;
        std::shared_ptr<const ESM::DoorState> mDoor;
        size_t mActor;
        std::unique_ptr<Installation> mStaged;
        std::unique_ptr<const EquipmentSuccess> mSuccess;
        EquipmentBytes mImage;
    };
    EquipmentRuntime::PreparedEquipment::PreparedEquipment(std::unique_ptr<State> state) : mState(std::move(state)) {}
    EquipmentRuntime::PreparedEquipment::PreparedEquipment(PreparedEquipment&&) noexcept = default;
    EquipmentRuntime::PreparedEquipment& EquipmentRuntime::PreparedEquipment::operator=(PreparedEquipment&&) noexcept = default;
    EquipmentRuntime::PreparedEquipment::~PreparedEquipment() = default;
    const EquipmentSuccess& EquipmentRuntime::PreparedEquipment::candidate() const
    {
        if (!mState || !mState->mSuccess) throw std::invalid_argument("Consumed equipment preparation");
        return *mState->mSuccess;
    }
    std::span<const char> EquipmentRuntime::PreparedEquipment::image() const
    {
        (void)candidate();
        return mState->mImage;
    }
    PlainEquipmentValues EquipmentRuntime::preparedValues(const PreparedEquipment& prepared, size_t owner) const
    {
        (void)prepared.candidate();
        const auto& state = *prepared.mState;
        if (state.mOwner != this || state.mLifetime.lock() != mLifetime || state.mDoor != mDoorState
            || state.mSuccess->mCommand.mExpectedRevision != mWorld.getPtrRegistryRevision())
            throw std::invalid_argument("Equipment projection candidate is stale or foreign");
        return owner == state.mActor ? state.mStaged->mSaved : installedValues(owner);
    }
    EquipmentRuntime::PreparedEquipment EquipmentRuntime::prepare(EquipmentCaller caller, EquipmentCommand command)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        if (mFailedClosed || !mConnected || mRestartActor
            || command.mExpectedRevision >= std::numeric_limits<size_t>::max() - 1)
            throw std::invalid_argument("Equipment persistence, recovery or revision mode invalid");
        const auto actor = validateCommand(caller, command);
        for (size_t i = 0; i < ownerCount(); ++i) validateCaller(i, ownerPtr(i));
        const ESM::RefNum item{command.mItem.mIndex, command.mItem.mContentFile};
        phase.set(Phase::Preparation);
        auto input = PreparedPlainEquipment::prepare(ContainerStoreResolution(storage(actor), ownerPtr(actor)),
            mWorld.getPtr(item), item, command.mExpectedRevision,
            command.mState == EquipmentRequestedState::Equipped, preparationContext(actor), command.mSlot);
        auto staged = stageInstallation(actor, ownerPtr(actor), std::move(input), actor);
        // Slot changes invalidate stale commands even when no stack was split.
        staged->mRevision = std::max(staged->mRevision, size_t(command.mExpectedRevision) + 1);
        const auto& result = staged->mPrepared.result();
        auto success = std::make_unique<const EquipmentSuccess>(EquipmentSuccess{command,
            ownedId(result.mSlots[InventoryStore::Slot_Shirt]), ownedId(result.mSelected),
            ownedId(result.mLastGenerated), staged->mRevision, result.mLuck, result.mSkipped});
        EquipmentSessionValues values{{installedValues(0), installedValues(1)}, staged->mRevision};
        values.mActors[actor] = staged->mSaved;
        EquipmentBytes image;
        encodeSession(std::move(values), image);
        auto state = std::make_unique<PreparedEquipment::State>();
        state->mOwner = this;
        state->mLifetime = mLifetime;
        state->mDoor = mDoorState;
        state->mActor = actor;
        state->mStaged = std::move(staged);
        state->mSuccess = std::move(success);
        state->mImage = std::move(image);
        return PreparedEquipment(std::move(state));
    }
    PersistenceResult EquipmentRuntime::commit(PreparedEquipment& prepared, EquipmentSessionCommitter& durability,
        std::unique_ptr<const EquipmentSuccess>& output, EquipmentBytes& bytes)
    {
        using namespace Allocations;
        InPhase phase(Phase::Validation);
        if (mFailedClosed) return PersistenceResult::Uncertain;
        if (!prepared.mState || prepared.mState->mOwner != this || prepared.mState->mLifetime.lock() != mLifetime || prepared.mState->mDoor != mDoorState
            || !prepared.mState->mSuccess || mRestartActor)
            throw std::invalid_argument("Equipment preparation does not belong to this live runtime");
        auto& state = *prepared.mState;
        const auto& command = state.mSuccess->mCommand;
        if (validateCommand({command.mActor}, command) != state.mActor)
            throw std::invalid_argument("Equipment preparation actor changed");
        for (size_t i = 0; i < ownerCount(); ++i) validateCaller(i, ownerPtr(i));
        phase.set(Phase::Revalidation);
        auto& staged = *state.mStaged;
        auto& candidate = staged.mPrepared.installationCandidate(preparationContext(state.mActor), storage(state.mActor));
        phase.set(Phase::Persistence);
        const auto outcome = durability.commit(state.mImage);
        if (outcome != PersistenceResult::Accepted)
        {
            mFailedClosed = outcome == PersistenceResult::Uncertain;
            return outcome;
        }
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            installPrepared(state.mActor, staged, candidate);
            mWorld.mPtrRegistry.mIndex.swap(staged.mRegistry);
            mWorld.mPtrRegistry.mRevision = staged.mRevision;
            mWorld.mPtrRegistry.mLastGenerated = staged.mSaved.mLastGenerated;
            phase.set(Phase::Publication);
            output.swap(state.mSuccess);
            bytes.swap(state.mImage);
        };
        install();
        phase.set(Phase::Retirement);
        prepared.mState.reset();
        return outcome;
    }

    FileReadResult EquipmentRuntime::restartSession(const std::filesystem::path& path,
        std::span<const ESM::RefId> referenceIds, std::unique_ptr<const EquipmentSessionValues>& output,
        EquipmentBytes& bytes, FileFaults& faults)
    {
        EquipmentBytes accepted;
        const auto result = readBoundedFile(path, MaxEquipmentSessionBytes, accepted, faults);
        if (result != FileReadResult::Read) return result;
        restoreSession(std::move(accepted), referenceIds, output, bytes);
        return result;
    }

    void EquipmentRuntime::restoreSession(EquipmentBytes accepted, std::span<const ESM::RefId> referenceIds,
        std::unique_ptr<const EquipmentSessionValues>& output, EquipmentBytes& bytes)
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
        if (fresh.mRegistry.size() != ownerCount())
            throw std::invalid_argument("Session recovery requires exactly its registered owners");
        phase.set(Phase::Preparation);
        EquipmentSessionValues values;
        std::vector<EquipmentEnvelope> containerEnvelopes;
        for (size_t i = 2; i < ownerCount(); ++i)
            containerEnvelopes.push_back(expectedEnvelope(ownerPtr(i).getCellRef().getRefNum()));
        std::vector<EquipmentBindings> containerBindings;
        for (size_t i = 0; i < containerEnvelopes.size(); ++i)
            containerBindings.push_back({containerEnvelopes[i], mStore, referenceIds, mScriptLocals, inventoryStorage(i + 2) != nullptr});
        decodeEquipmentSession(accepted, bindings, values, containerBindings, mWorldItems ? &bindings[0] : nullptr,
            mDoorBinding ? &*mDoorBinding : nullptr, mWorldCells.has_value());
        validateWorldItems(values);
        EquipmentBytes canonical;
        encodeEquipmentSession(values, bindings, canonical, containerBindings, mWorldItems ? &bindings[0] : nullptr,
            mDoorBinding ? &*mDoorBinding : nullptr, mWorldCells.has_value());
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
        std::vector<std::unique_ptr<RestoredPlainEquipment>> containers;
        std::vector<ContainerStore*> sharedCandidates;
        std::vector<std::vector<ContainerStoreIterator>> sharedSlots;
        std::vector<ContainerStoreIterator> sharedSelected;
        for (size_t i = 0; i < values.mContainers.size(); ++i)
        {
            containers.push_back(std::make_unique<RestoredPlainEquipment>(RestoredPlainEquipment::restore(
                values.mContainers[i], mStore, ownerPtr(i + 2).getCellRef().getRefNum(), mScriptLocals,
                inventoryStorage(i + 2) == nullptr)));
            auto& candidate = containers.back()->installationStorage(
                mStore, ownerPtr(i + 2).getCellRef().getRefNum(), fresh.mSavedCounter);
            sharedCandidates.push_back(&candidate);
            auto& live = storage(i + 2);
            sharedSlots.emplace_back(InventoryStore::Slots, live.end());
            sharedSelected.push_back(live.end());
            candidate.forEachStored([&](auto& node, auto it) {
                it.mContainer = &live;
                Ptr ptr(&node, nullptr);
                ptr.mContainerStore = &live;
                for (int slot = 0; slot < InventoryStore::Slots; ++slot)
                    if (node.mRef.getRefNum() == values.mContainers[i].mSlots[slot]) sharedSlots[i][slot] = it;
                if (node.mRef.getRefNum() == values.mContainers[i].mSelected) sharedSelected[i] = it;
                if (!registry.emplace(node.mRef.getRefNum(), ptr).second)
                    throw std::invalid_argument("Container recovery identity collision");
            });
        }
        phase.set(Phase::Result);
        auto restoredWorld = values.mWorldItems;
        auto restoredCells = values.mWorldCells;
        auto restoredDoor = values.mDoor;
        auto saved = std::make_unique<const EquipmentSessionValues>(std::move(values));
        phase.set(Phase::Revalidation);
        for (size_t i = 0; i < 2; ++i) validateRestart(i, mActors[i]->getPtr(), bindings[i], fresh);
        const auto install = [&]() noexcept {
            phase.set(Phase::Installation);
            for (size_t i = 0; i < 2; ++i)
                installInventory(i, *candidates[i], staged[i]->mSlots, staged[i]->mSelected, staged[i]->mItem, staged[i]->mNpcStats, true);
            mWorld.mPtrRegistry.mIndex.swap(registry);
            for (size_t i = 0; i < sharedCandidates.size(); ++i)
            {
                installStorage(storage(i + 2), *sharedCandidates[i], true);
                if (auto* inventory = inventoryStorage(i + 2))
                {
                    std::copy(sharedSlots[i].begin(), sharedSlots[i].end(), inventory->mSlots.begin());
                    inventory->mSelectedEnchantItem = sharedSelected[i];
                }
            }
            mWorld.mPtrRegistry.mRevision = saved->mRevision;
            mWorld.mPtrRegistry.mLastGenerated = fresh.mSavedCounter;
            mWorldItems.swap(restoredWorld);
            mWorldCells.swap(restoredCells);
            mDoorState.swap(restoredDoor);
            mRestartActor.reset();
            phase.set(Phase::Publication);
            output.swap(saved);
            bytes.swap(accepted);
        };
        install();
        phase.set(Phase::Retirement);
    }
}
