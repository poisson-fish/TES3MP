#ifndef TES3MP_NATIVE_TRANSFER_REHEARSAL_H
#define TES3MP_NATIVE_TRANSFER_REHEARSAL_H

#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/esm3/objectstate.hpp>

#include <filesystem>

namespace MWWorld::Testing
{
    enum class TestPersistenceResult
    {
        Rejected,
        Accepted,
        Uncertain
    };

    // Allocation-free signal. An uncertain fixture must be discarded; no retry
    // or recovery installation is authorized by this test composition.
    struct TestDurabilityUncertain
    {
    };

    // Owned test save: identity associations remain separate from ObjectState.
    // This is not a production persistence format or a durable file adapter.
    struct SerializedInventory
    {
        std::vector<ESM::ObjectState> mObjects;
        std::vector<ESM::RefNum> mProposedIdentities;

        void swap(SerializedInventory& other) noexcept
        {
            static_assert(noexcept(mObjects.swap(other.mObjects)));
            static_assert(noexcept(mProposedIdentities.swap(other.mProposedIdentities)));
            mObjects.swap(other.mObjects);
            mProposedIdentities.swap(other.mProposedIdentities);
        }
    };

    struct SerializedPair
    {
        SerializedInventory mSource, mDestination;
    };

    template <class Identity>
    void serializeInventory(const PreparedContainerTransfer::MiscList& storage, Identity identity,
        const Compiler::Locals& declarations, SerializedInventory& inventory)
    {
        inventory.mObjects.reserve(storage.size());
        inventory.mProposedIdentities.reserve(storage.size());
        size_t i = 0;
        for (const auto& node : storage)
        {
            inventory.mProposedIdentities.push_back(identity(i++));
            inventory.mObjects.emplace_back();
            auto& object = inventory.mObjects.back();
            object.blank();
            node.mRef.writeState(object);
            node.mData.write(object, declarations);
            object.mHasCustomState = false;
        }
    }

    // Test-target-only composition. Own every exchanged store/service, accept no
    // external mutation target. Rehearsal rolls back; commit gates installation
    // in this disposable fixture on an explicit synchronous test sink.
    // Content/readers and the declarations-only script manager outlive this fixture.
    class DisposableTransferRehearsal
    {
        bool mActive = false;
        bool mFailedClosed = false;

    public:
        WorldModel mModel;
        ManualRef mSourceOwner, mDestinationOwner, mOtherOwner;
        LocalScripts mSourceScripts, mDestinationScripts;
        ContainerStore mSource, mDestination, mOther;
        int mNotifications = 0;
        ContainerStoreAddContext mSourceAdd, mDestinationAdd, mOtherAdd;
        ContainerStoreRemoveContext mRemoval;

        DisposableTransferRehearsal(
            ESMStore& store, ESM::ReadersCache& readers, MWBase::ScriptManager& scripts, ESM::RefId owner, bool shared);
        DisposableTransferRehearsal(const DisposableTransferRehearsal&) = delete;
        DisposableTransferRehearsal& operator=(const DisposableTransferRehearsal&) = delete;

        enum class Stage
        {
            Validated,
            Identities,
            SourceInventory,
            DestinationInventory,
            SourceScripts,
            DestinationScripts, // Only for distinct services, after source.
            Registry
        };
        // Consumes the pair so failure/discard destroys it only after rollback.
        // On success returns the exact detached pair, fully revalidated. The test
        // observer may inspect or throw, but must not mutate/destroy this fixture,
        // its borrowed dependencies or saved pair views. No gameplay/effect calls.
        PreparedContainerTransfer rehearse(
            PreparedContainerTransfer pair, const std::function<void(Stage)>& observer = {});

        // The sink must not mutate/reenter/destroy the fixture, pair or borrowed
        // dependencies. It may copy the owned save but must not retain its address.
        // False/exception means no acceptance and preserves the entire fixture.
        // True accepts synchronously: only noexcept installation/retirement follows.
        // Pair consumption applies to success and failure. No effects are emitted.
        using TestSink = std::function<bool(const SerializedPair&)>;
        bool commit(PreparedContainerTransfer pair, const Compiler::Locals& declarations, const TestSink& sink);

        // Throws TestDurabilityUncertain and permanently blocks commit/rehearse
        // if the sink cannot prove rejection or acceptance. Such a sink must
        // return Uncertain, never throw after it may have replaced durable bytes.
        using TestDurableSink = std::function<TestPersistenceResult(const SerializedPair&)>;
        bool commitDurably(
            PreparedContainerTransfer pair, const Compiler::Locals& declarations, const TestDurableSink& sink);
        bool failedClosed() const noexcept { return mFailedClosed; }

        // Read-only exact storage/cursor witnesses for rollback assertions.
        const PreparedContainerTransfer::MiscList& sourceStorage() const;
        const PreparedContainerTransfer::MiscList& destinationStorage() const;
        const PreparedContainerTransfer::MiscList& otherStorage() const;
        std::vector<const void*> scriptNodes(const LocalScripts& service) const;
        const void* scriptCursor(const LocalScripts& service) const;
        const void* registryNode(ESM::RefNum identity) const;
        auto cacheState(const ContainerStore& store) const
        {
            return std::tuple{ store.mCachedWeight, store.mWeightUpToDate, store.mRechargingItemsUpToDate,
                store.mModified, store.mStorageIdentity.get(), store.mSeed, store.mRechargingItems,
                store.mRechargingItems.data(), store.mRechargingItems.capacity(), store.mResolved, store.mListener };
        }
    };

    void serializePair(const DisposableTransferRehearsal& fixture, const PreparedContainerTransfer& pair,
        const Compiler::Locals& declarations, SerializedPair& output);

    void checkTransferRehearsal(const ESMStore& content);
    void checkTransferRehearsalAllocations(const ESMStore& content);
    void checkTransferPreparationAllocations(const ESMStore& content);
    void checkTransferSerialization(const ESMStore& content);
    void checkTransferObjectState(const ESMStore& content);
    void checkTransferLocalsRestore(const ESMStore& content);
    void checkTransferRestore(const ESMStore& content);
    void checkTransferCodec(const ESMStore& content);
    void checkTransferFileSink(const ESMStore& content, const std::filesystem::path& scratch);
    void checkTransferCommand(const ESMStore& content, const std::filesystem::path& scratch);
    void checkTransferCommit(const ESMStore& content);
}

#endif
