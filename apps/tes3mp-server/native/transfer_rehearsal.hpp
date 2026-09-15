#ifndef TES3MP_NATIVE_TRANSFER_REHEARSAL_H
#define TES3MP_NATIVE_TRANSFER_REHEARSAL_H

#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>

namespace MWWorld::Testing
{
    // Test-target-only composition. Own every exchanged store/service, accept no
    // external mutation target, and always roll back. There is no commit/release.
    // Content/readers and the declarations-only script manager outlive this fixture.
    class DisposableTransferRehearsal
    {
        bool mActive = false;

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

    void checkTransferRehearsal(const ESMStore& content);
    void checkTransferRehearsalAllocations(const ESMStore& content);
    void checkTransferPreparationAllocations(const ESMStore& content);
    void checkTransferSerialization(const ESMStore& content);
}

#endif
