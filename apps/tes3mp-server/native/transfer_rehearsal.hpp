#ifndef TES3MP_NATIVE_TRANSFER_REHEARSAL_H
#define TES3MP_NATIVE_TRANSFER_REHEARSAL_H

#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/esm3/objectstate.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <type_traits>

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

    // Owned restart values, captured from the complete prepared registry. A
    // counter can exceed every surviving identity; it is never inferred from nodes.
    struct TransferRestartMetadata
    {
        uint64_t mRevision = 0;
        ESM::RefNum mLastGenerated;
        bool operator==(const TransferRestartMetadata&) const = default;
    };

    inline constexpr size_t MaxTransferInventoryItems = 1024;
    inline constexpr size_t MaxTransferScriptEntries = 3 * MaxTransferInventoryItems;

    struct TransferScriptItem
    {
        ESM::RefNum mIdentity;
        ESM::RefId mBase;
        bool mConfigured = false;
        bool operator==(const TransferScriptItem&) const = default;
    };

    struct TransferScriptRegistration
    {
        ESM::RefNum mIdentity;
        ESM::RefId mScript;
        bool operator==(const TransferScriptRegistration&) const = default;
    };

    struct TransferScriptService
    {
        std::vector<TransferScriptRegistration> mEntries;
        size_t mCursor = 0; // size means end, including an empty service.
        bool operator==(const TransferScriptService&) const = default;
    };

    // Owned semantic metadata only. Source is service 0; destination is 0 when
    // shared, otherwise 1. Other-store bindings belong to service 0. They do not
    // reconstruct that store's values. A configured item need not be registered.
    struct TransferScriptMetadata
    {
        bool mShared = true;
        std::array<TransferScriptService, 2> mServices;
        std::vector<TransferScriptItem> mOther;
        bool operator==(const TransferScriptMetadata&) const = default;
    };

    struct SerializedPair
    {
        SerializedInventory mSource, mDestination;
        TransferRestartMetadata mRestart;
        TransferScriptMetadata mScripts;

        void swap(SerializedPair& other) noexcept
        {
            mSource.swap(other.mSource);
            mDestination.swap(other.mDestination);
            static_assert(std::is_nothrow_swappable_v<TransferRestartMetadata>);
            std::swap(mRestart, other.mRestart);
            static_assert(std::is_nothrow_swappable_v<TransferScriptMetadata>);
            std::swap(mScripts, other.mScripts);
        }
    };

    // Test-only detached stock lists. Views capture lifetimes during restoration,
    // so registry preparation never lazily changes an input node's witness.
    struct RestoredInventory
    {
        PreparedContainerTransfer::MiscList mNodes;
        std::vector<ESM::RefNum> mProposedIdentities;
        std::vector<ConstPtr> mViews;
    };

    struct RestoredPair
    {
        RestoredInventory mSource, mDestination;
        TransferRestartMetadata mRestart;
        TransferScriptMetadata mScripts; // Carried as values; no service is rebuilt.
    };

    struct RestoreContent;
    struct SaveEnvelope;
    void restorePair(
        const SerializedPair& input, const RestoreContent& content, std::unique_ptr<const RestoredPair>& output);

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

        // Explicit fresh fixture bindings, including dormant other-store nodes.
        // Store addresses are compare-only until owner lifetimes validate.
        struct RestartBindings
        {
            std::array<Ptr, 3> mOwners;
            std::array<const ContainerStore*, 3> mStores;
            std::array<std::shared_ptr<const void>, 3> mStorage;
            std::array<std::weak_ptr<const void>, 3> mLifetimes;
            std::vector<std::pair<ESM::RefNum, Ptr>> mOther;
        };
        RestartBindings restartBindings() const;

        class RestartRegistry
        {
            friend class DisposableTransferRehearsal;
            RestartBindings mFresh;
            PtrRegistry::Snapshot mBindings;
            std::unique_ptr<PtrRegistry::PreparedStorage> mStorage;

        public:
            RestartRegistry() = default;
            RestartRegistry(const RestartRegistry&) = delete;
            RestartRegistry& operator=(const RestartRegistry&) = delete;
            const PtrRegistry::Snapshot& getBindings() const { return mBindings; }
            ConstPtr getItem(ESM::RefNum id) const;
        };

        // No installation or service reconstruction. Every borrowed input must
        // outlive use and remain serialized/unchanged. Complete validation precedes
        // staging; only the owned candidate pointer is published, by noexcept swap.
        void prepareRestartRegistry(const SerializedPair& decoded, const RestoredPair& restored,
            const SaveEnvelope& envelope, const RestartBindings& fresh,
            std::unique_ptr<const RestartRegistry>& output) const;

        struct RestartScriptBindings
        {
            RestartBindings mRegistry;
            // Source/destination/other roles, with explicit sharing.
            std::array<const LocalScripts*, 3> mServices{};
            std::array<std::weak_ptr<const void>, 3> mLifetimes;
            std::array<LocalScripts::List, 3> mOriginal;
        };
        RestartScriptBindings restartScriptBindings() const;

        class RestartScripts
        {
            friend class DisposableTransferRehearsal;
            RestartScriptBindings mFresh;
            TransferRestartMetadata mRestart;
            bool mShared = true;
            std::vector<ConstPtr> mItems;
            std::array<std::unique_ptr<LocalScripts::PreparedStorage>, 2> mStorage;

        public:
            RestartScripts() = default;
            RestartScripts(const RestartScripts&) = delete;
            RestartScripts& operator=(const RestartScripts&) = delete;
            const TransferRestartMetadata& getRestart() const { return mRestart; }
            const LocalScripts::PreparedStorage& getSourceStorage() const;
            const LocalScripts::PreparedStorage& getDestinationStorage() const;
        };

        // No installation, locals initialization or execution. Inputs/dependencies
        // remain caller-owned and serialized. Only an owned candidate is published.
        void prepareRestartScripts(const SerializedPair& decoded, const RestoredPair& restored,
            const RestoreContent& content, const SaveEnvelope& envelope, const RestartRegistry& registry,
            const RestartScriptBindings& fresh, std::unique_ptr<const RestartScripts>& output) const;

    private:
        void validateRestartRegistry(const SerializedPair& decoded, const RestoredPair& restored,
            const SaveEnvelope& envelope, const RestartBindings& fresh) const;
        static void validateRestartScriptLifetimes(const RestartScriptBindings& fresh);

    public:

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
    void checkTransferRestartRegistry(const ESMStore& content);
    void checkTransferScriptMetadata(const ESMStore& content);
    void checkTransferRestartScripts(const ESMStore& content);
    void checkTransferCodec(const ESMStore& content);
    void checkTransferFileSink(const ESMStore& content, const std::filesystem::path& scratch);
    void checkTransferCommand(const ESMStore& content, const std::filesystem::path& scratch);
    void checkTransferCommit(const ESMStore& content);
}

#endif
