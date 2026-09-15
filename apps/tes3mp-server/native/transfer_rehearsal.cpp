#include "transfer_rehearsal.hpp"
#include "test_allocations.hpp"

#include <components/esm3/loadcont.hpp>

#include <utility>

namespace MWWorld::Testing
{
    DisposableTransferRehearsal::DisposableTransferRehearsal(
        ESMStore& store, ESM::ReadersCache& readers, MWBase::ScriptManager& scripts, ESM::RefId owner, bool shared)
        : mModel(store, readers, 1)
        , mSourceOwner(store, owner)
        , mDestinationOwner(store, owner)
        , mOtherOwner(store, owner)
        , mSourceScripts(store)
        , mDestinationScripts(store)
        , mSourceAdd{ store, mModel, mSourceOwner.getPtr(), mSourceOwner.getPtr(), &mSourceScripts, &scripts,
            [this](const Ptr&) { ++mNotifications; } }
        , mDestinationAdd{ store, mModel, mDestinationOwner.getPtr(), mDestinationOwner.getPtr(),
            shared ? &mSourceScripts : &mDestinationScripts, &scripts, mSourceAdd.mInventoryUpdated }
        , mOtherAdd{ store, mModel, {}, mOtherOwner.getPtr(), &mSourceScripts, &scripts, mSourceAdd.mInventoryUpdated }
        , mRemoval{ mModel, mSourceOwner.getPtr(), mSourceScripts, mSourceAdd.mInventoryUpdated }
    {
        Misc::Rng::Generator prng{ 0 };
        for (auto [inventory, actor] : { std::pair{ &mSource, mSourceOwner.getPtr() },
                 std::pair{ &mDestination, mDestinationOwner.getPtr() }, std::pair{ &mOther, mOtherOwner.getPtr() } })
        {
            mModel.registerPtr(actor);
            inventory->setPtr(actor, mModel);
            inventory->fill({}, {}, prng);
        }
    }

    PreparedContainerTransfer DisposableTransferRehearsal::rehearse(
        PreparedContainerTransfer pair, const std::function<void(Stage)>& observer)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mFailedClosed)
            throw TestDurabilityUncertain{};
        if (mActive)
            throw std::invalid_argument("Disposable rehearsal already active");
        // Even modified fixture contexts cannot redirect an exchange to a live
        // service. validateTransfer subsequently checks all protected bindings.
        if (&mRemoval.mWorldModel != &mModel || &mDestinationAdd.mWorldModel != &mModel
            || &mRemoval.mLocalScripts != &mSourceScripts
            || (mDestinationAdd.mLocalScripts != &mSourceScripts
                && mDestinationAdd.mLocalScripts != &mDestinationScripts))
            throw std::invalid_argument("Disposable rehearsal service mismatch");
        if (!mSource.validateTransfer(pair, mDestination, mRemoval, mDestinationAdd).isComplete())
            throw std::invalid_argument("Disposable rehearsal requires complete resolution");

        phase.set(Allocations::Phase::Setup);
        // Validation precedes every cast/access to the protected stock storage.
        // Originals are retained by swapping nodes, never copying/rebuilding them.
        auto& source = const_cast<PreparedContainerTransfer::MiscList&>(pair.getSourceStorage());
        auto& destination = const_cast<PreparedContainerTransfer::MiscList&>(pair.getDestinationStorage());
        auto& sourceScripts = const_cast<LocalScripts::PreparedStorage&>(pair.getSourceScriptStorage());
        auto& destinationScripts = const_cast<LocalScripts::PreparedStorage&>(pair.getDestinationScriptStorage());
        auto& registry = const_cast<PtrRegistry::PreparedStorage&>(pair.getRegistryStorage());
        auto& liveRegistry = mModel.mPtrRegistry;
        const bool shared = &sourceScripts == &destinationScripts;
        const auto selection = [](ContainerStore& store, auto& list, const ConstPtr& selected) {
            for (auto it = list.begin(); it != list.end(); ++it)
                if (&*it == selected.mRef)
                    return ContainerStoreIterator(&store, it);
            return store.end(); // The validated empty selection, including full removal.
        };
        const auto cursor = [](auto& list, size_t position) {
            using Iterator = decltype(list.begin());
            return position == list.size() ? std::optional<Iterator>()
                                           : std::optional<Iterator>(std::next(list.begin(), position));
        };
        const auto sourceSelection = selection(mSource, source, pair.getRelocation().mSourceSelection);
        const auto destinationSelection
            = selection(mDestination, destination, pair.getRelocation().mDestinationSelection);
        const auto originalSourceSelection = mSource.mSelectedEnchantItem;
        const auto originalDestinationSelection = mDestination.mSelectedEnchantItem;
        const auto sourceCursor = cursor(sourceScripts.mEntries, pair.getSourceScripts().mCursor);
        const auto destinationCursor = cursor(destinationScripts.mEntries, pair.getDestinationScripts().mCursor);
        const auto originalSourceCursor = cursor(mSourceScripts.mScripts, mSourceScripts.snapshot().mCursor);
        auto& destinationService = *mDestinationAdd.mLocalScripts;
        const auto originalDestinationCursor
            = cursor(destinationService.mScripts, destinationService.snapshot().mCursor);
        auto revision = registry.mBindings.mRevision;
        auto counter = registry.mBindings.mLastGenerated;
        bool identities = false, sourceInstalled = false, destinationInstalled = false;
        bool sourceScriptsInstalled = false, destinationScriptsInstalled = false, registryInstalled = false;
        // Preserve cache values even if a read-only observer computes weight.
        struct Cache
        {
            float mWeight;
            bool mWeightValid, mRechargeValid, mModified;
        };
        const auto cache = [](const ContainerStore& store) {
            return Cache{ store.mCachedWeight, store.mWeightUpToDate, store.mRechargingItemsUpToDate, store.mModified };
        };
        const auto sourceCache = cache(mSource), destinationCache = cache(mDestination);
        const auto restoreCache = [](ContainerStore& store, const Cache& saved) noexcept {
            store.mCachedWeight = saved.mWeight;
            store.mWeightUpToDate = saved.mWeightValid;
            store.mRechargingItemsUpToDate = saved.mRechargeValid;
            store.mModified = saved.mModified;
        };
        const auto exchangeRegistry = [&]() noexcept {
            static_assert(noexcept(liveRegistry.mIndex.swap(registry.mIndex)));
            liveRegistry.mIndex.swap(registry.mIndex);
            std::swap(liveRegistry.mRevision, revision);
            std::swap(liveRegistry.mLastGenerated, counter);
        };
        const auto rollback = [&]() noexcept {
            Allocations::InPhase rollbackPhase(Allocations::Phase::Rollback);
            if (registryInstalled)
                exchangeRegistry();
            if (destinationScriptsInstalled)
            {
                destinationService.mScripts.swap(destinationScripts.mEntries);
                destinationService.mIter
                    = originalDestinationCursor ? *originalDestinationCursor : destinationService.mScripts.end();
            }
            if (sourceScriptsInstalled)
            {
                mSourceScripts.mScripts.swap(sourceScripts.mEntries);
                mSourceScripts.mIter = originalSourceCursor ? *originalSourceCursor : mSourceScripts.mScripts.end();
            }
            if (destinationInstalled)
            {
                mDestination.mLists.mMiscItems.mList.swap(destination);
                mDestination.mSelectedEnchantItem = originalDestinationSelection;
            }
            if (sourceInstalled)
            {
                mSource.mLists.mMiscItems.mList.swap(source);
                mSource.mSelectedEnchantItem = originalSourceSelection;
            }
            if (identities)
                for (auto* list : { &source, &destination })
                    for (auto& node : *list)
                    {
                        node.mWorldModel = nullptr;
                        node.mRef.setRefNum({}); // setRefNum preserves hasChanged().
                    }
            restoreCache(mSource, sourceCache);
            restoreCache(mDestination, destinationCache);
            mActive = false;
        };
        {
            phase.set(Allocations::Phase::Exchange);
            struct Rollback
            {
                const decltype(rollback)& mRun;
                ~Rollback() { mRun(); }
            } guard{ rollback };
            mActive = true;
            const auto checkpoint = [&](Stage stage) {
                if (observer)
                    observer(stage);
            };
            checkpoint(Stage::Validated);
            identities = true;
            const auto assign = [&](auto& list, const auto& views) {
                size_t i = 0;
                for (auto& node : list)
                {
                    const auto id = views[i++].mIdentity;
                    node.mRef.setRefNum(id.isSet() ? id : pair.getDestinationIdentity());
                    node.mWorldModel = &mModel;
                }
            };
            assign(source, pair.getRelocation().mSource);
            assign(destination, pair.getRelocation().mDestination);
            checkpoint(Stage::Identities);
            static_assert(noexcept(source.swap(mSource.mLists.mMiscItems.mList)));
            mSource.mLists.mMiscItems.mList.swap(source);
            sourceInstalled = true;
            mSource.mSelectedEnchantItem = sourceSelection;
            mSource.mWeightUpToDate = mSource.mRechargingItemsUpToDate = false;
            mSource.mModified = true;
            checkpoint(Stage::SourceInventory);
            mDestination.mLists.mMiscItems.mList.swap(destination);
            destinationInstalled = true;
            mDestination.mSelectedEnchantItem = destinationSelection;
            mDestination.mWeightUpToDate = mDestination.mRechargingItemsUpToDate = false;
            mDestination.mModified = true;
            checkpoint(Stage::DestinationInventory);
            static_assert(noexcept(mSourceScripts.mScripts.swap(sourceScripts.mEntries)));
            mSourceScripts.mScripts.swap(sourceScripts.mEntries);
            sourceScriptsInstalled = true;
            mSourceScripts.mIter = sourceCursor ? *sourceCursor : mSourceScripts.mScripts.end();
            checkpoint(Stage::SourceScripts);
            if (!shared)
            {
                destinationService.mScripts.swap(destinationScripts.mEntries);
                destinationScriptsInstalled = true;
                destinationService.mIter = destinationCursor ? *destinationCursor : destinationService.mScripts.end();
                checkpoint(Stage::DestinationScripts);
            }
            exchangeRegistry();
            registryInstalled = true;
            checkpoint(Stage::Registry);
        }
        phase.set(Allocations::Phase::Revalidation);
        mSource.validateTransfer(pair, mDestination, mRemoval, mDestinationAdd);
        return pair;
    }

    bool DisposableTransferRehearsal::commit(
        PreparedContainerTransfer input, const Compiler::Locals& declarations, const TestSink& sink)
    {
        if (!sink)
            throw std::invalid_argument("Disposable commit requires a sink");
        return commitDurably(std::move(input), declarations, [&](const SerializedPair& saved) {
            return sink(saved) ? TestPersistenceResult::Accepted : TestPersistenceResult::Rejected;
        });
    }

    bool DisposableTransferRehearsal::commitDurably(
        PreparedContainerTransfer input, const Compiler::Locals& declarations, const TestDurableSink& sink)
    {
        Allocations::InPhase phase(Allocations::Phase::Validation);
        if (mFailedClosed)
            throw TestDurabilityUncertain{};
        if (mActive)
            throw std::invalid_argument("Disposable rehearsal already active");
        if (!sink || &mRemoval.mWorldModel != &mModel || &mDestinationAdd.mWorldModel != &mModel
            || &mRemoval.mLocalScripts != &mSourceScripts
            || (mDestinationAdd.mLocalScripts != &mSourceScripts
                && mDestinationAdd.mLocalScripts != &mDestinationScripts))
            throw std::invalid_argument("Disposable commit sink or service mismatch");
        mActive = true;
        struct Active
        {
            bool& mValue;
            ~Active() { mValue = false; }
        } active{ mActive };
        // Destroy the consumed state before leaving the measured phase/active
        // guard, including its private iterators, services and old inventory nodes.
        auto pair = std::move(input);
        SerializedPair saved;
        serializePair(*this, pair, declarations, saved); // Full validation before storage reads.

        phase.set(Allocations::Phase::Setup);
        auto& source = const_cast<PreparedContainerTransfer::MiscList&>(pair.getSourceStorage());
        auto& destination = const_cast<PreparedContainerTransfer::MiscList&>(pair.getDestinationStorage());
        auto& sourceScripts = const_cast<LocalScripts::PreparedStorage&>(pair.getSourceScriptStorage());
        auto& destinationScripts = const_cast<LocalScripts::PreparedStorage&>(pair.getDestinationScriptStorage());
        auto& registry = const_cast<PtrRegistry::PreparedStorage&>(pair.getRegistryStorage());
        auto& liveRegistry = mModel.mPtrRegistry;
        auto& destinationService = *mDestinationAdd.mLocalScripts;
        const bool shared = &sourceScripts == &destinationScripts;
        const auto selection = [](ContainerStore& store, auto& list, const ConstPtr& selected) {
            for (auto it = list.begin(); it != list.end(); ++it)
                if (&*it == selected.mRef)
                    return ContainerStoreIterator(&store, it);
            return store.end();
        };
        const auto sourceSelection = selection(mSource, source, pair.getRelocation().mSourceSelection);
        const auto destinationSelection
            = selection(mDestination, destination, pair.getRelocation().mDestinationSelection);
        const auto cursor = [](auto& list, size_t position) {
            using Iterator = decltype(list.begin());
            return position == list.size() ? std::optional<Iterator>()
                                           : std::optional<Iterator>(std::next(list.begin(), position));
        };
        const auto sourceCursor = cursor(sourceScripts.mEntries, pair.getSourceScripts().mCursor);
        const auto destinationCursor = cursor(destinationScripts.mEntries, pair.getDestinationScripts().mCursor);
        const auto revision = registry.mBindings.mRevision;
        const auto counter = registry.mBindings.mLastGenerated;

        phase.set(Allocations::Phase::Revalidation);
        if (!mSource.validateTransfer(pair, mDestination, mRemoval, mDestinationAdd).isComplete())
            throw std::invalid_argument("Disposable commit requires complete resolution");
        // No pair readers/validators are called after this point. Assign IDs only
        // to owned nodes while still detached, so even a throwing CellRef setter
        // or rejecting sink cannot mutate/deregister anything in the fixture.
        const auto identities = [](auto& nodes, const auto& ids) {
            size_t i = 0;
            for (auto& node : nodes)
                node.mRef.setRefNum(ids[i++]);
        };
        identities(source, saved.mSource.mProposedIdentities);
        identities(destination, saved.mDestination.mProposedIdentities);

        phase.set(Allocations::Phase::Persistence);
        const auto outcome = sink(saved);
        if (outcome == TestPersistenceResult::Rejected)
            return false;
        if (outcome != TestPersistenceResult::Accepted)
        {
            mFailedClosed = true;
            throw TestDurabilityUncertain{};
        }

        // Synchronous acceptance is the last fallible call. No callbacks,
        // validation, Ptr construction, identity generation or effect dispatch.
        const auto install = [&]() noexcept {
            phase.set(Allocations::Phase::Installation);
            for (auto* list : { &source, &destination })
                for (auto& node : *list)
                    node.mWorldModel = &mModel;
            static_assert(noexcept(source.swap(mSource.mLists.mMiscItems.mList)));
            mSource.mLists.mMiscItems.mList.swap(source);
            mDestination.mLists.mMiscItems.mList.swap(destination);
            // Stock iterator assignment only copies fields, weak witnesses and
            // list iterators; selected nodes and receiving end owners are staged.
            mSource.mSelectedEnchantItem = sourceSelection;
            mDestination.mSelectedEnchantItem = destinationSelection;
            for (auto* store : { &mSource, &mDestination })
            {
                store->mRechargingItems.clear();
                store->mWeightUpToDate = store->mRechargingItemsUpToDate = false;
                store->mModified = true;
            }
            static_assert(noexcept(mSourceScripts.mScripts.swap(sourceScripts.mEntries)));
            mSourceScripts.mScripts.swap(sourceScripts.mEntries);
            // list::swap need not preserve end iterators. End is a staged logical
            // position, materialized from the receiving list after its swap.
            static_assert(noexcept(mSourceScripts.mScripts.end()));
            mSourceScripts.mIter = sourceCursor ? *sourceCursor : mSourceScripts.mScripts.end();
            if (!shared)
            {
                destinationService.mScripts.swap(destinationScripts.mEntries);
                destinationService.mIter = destinationCursor ? *destinationCursor : destinationService.mScripts.end();
            }
            static_assert(noexcept(liveRegistry.mIndex.swap(registry.mIndex)));
            liveRegistry.mIndex.swap(registry.mIndex);
            liveRegistry.mRevision = revision;
            liveRegistry.mLastGenerated = counter;

            phase.set(Allocations::Phase::Retirement);
            // Old nodes now belong to the consumed pair. Detach them before its
            // ordered teardown so destructors never touch the installed registry.
            for (auto* list : { &source, &destination })
                for (auto& node : *list)
                    node.mWorldModel = nullptr;
        };
        install();
        return true;
    }

    void serializePair(const DisposableTransferRehearsal& fixture, const PreparedContainerTransfer& pair,
        const Compiler::Locals& declarations, SerializedPair& output)
    {
        if (!fixture.mSource.validateTransfer(pair, fixture.mDestination, fixture.mRemoval, fixture.mDestinationAdd)
                .isComplete())
            throw std::invalid_argument("ObjectState serialization requires complete resolution");
        SerializedPair staged;
        const auto serialize = [&](const auto& storage, const auto& views, SerializedInventory& inventory) {
            serializeInventory(
                storage,
                [&](size_t i) {
                    const auto id = views.at(i).mIdentity;
                    return id.isSet() ? id : pair.getDestinationIdentity();
                },
                declarations, inventory);
        };
        serialize(pair.getSourceStorage(), pair.getRelocation().mSource, staged.mSource);
        serialize(pair.getDestinationStorage(), pair.getRelocation().mDestination, staged.mDestination);
        output.mSource.swap(staged.mSource);
        output.mDestination.swap(staged.mDestination);
    }

    const PreparedContainerTransfer::MiscList& DisposableTransferRehearsal::sourceStorage() const
    {
        return mSource.mLists.mMiscItems.mList;
    }
    const PreparedContainerTransfer::MiscList& DisposableTransferRehearsal::destinationStorage() const
    {
        return mDestination.mLists.mMiscItems.mList;
    }
    const PreparedContainerTransfer::MiscList& DisposableTransferRehearsal::otherStorage() const
    {
        return mOther.mLists.mMiscItems.mList;
    }
    std::vector<const void*> DisposableTransferRehearsal::scriptNodes(const LocalScripts& service) const
    {
        std::vector<const void*> result;
        for (const auto& node : service.mScripts)
            result.push_back(&node);
        return result;
    }
    const void* DisposableTransferRehearsal::scriptCursor(const LocalScripts& service) const
    {
        return service.mIter == service.mScripts.end() ? nullptr : &*service.mIter;
    }
    const void* DisposableTransferRehearsal::registryNode(ESM::RefNum identity) const
    {
        const auto it = mModel.mPtrRegistry.mIndex.find(identity);
        return it == mModel.mPtrRegistry.mIndex.end() ? nullptr : &*it;
    }
}
