#ifndef TES3MP_NATIVE_INVENTORY_VIEW_H
#define TES3MP_NATIVE_INVENTORY_VIEW_H

#include "inventory_transfer_command.hpp"

#include <vector>

namespace MWWorld::Testing
{
    using TES3MP::Native::InventoryInstanceId;
    using TES3MP::Native::InventoryNotificationConsumer;
    using TES3MP::Native::InventoryNotificationIntent;
    using TES3MP::Native::InventoryTransferSuccess;
    struct InventoryViewItem
    {
        InventoryInstanceId mItem;
        std::int32_t mCount = 0; // Signed stock count; zero retains dormant membership.
        bool operator==(const InventoryViewItem&) const = default;
    };

    struct InventoryOwnerView
    {
        InventoryInstanceId mOwner, mSelection;
        std::uint64_t mRevision = 0;
        std::vector<InventoryViewItem> mItems;
        bool operator==(const InventoryOwnerView&) const = default;
        void swap(InventoryOwnerView& other) noexcept;
    };

    // Owned test-only presentation values, not engine state or a wire format.
    // Each owner has independent storage and an exact authoritative revision.
    struct InventoryViewSnapshot
    {
        std::array<InventoryOwnerView, 2> mOwners;
        bool operator==(const InventoryViewSnapshot&) const = default;
        void swap(InventoryViewSnapshot& other) noexcept;
    };

    // Test-only presentation consumer for executeInventoryTransfer successes and
    // views from snapshotInventoryViews (or prior consumption). Success is copied
    // as owned values; views must outlive consumption. This is not a wire decoder.
    // Accept the exact one-shot batch in order.
    // Updated intents replace one owner view using committed counts/selections,
    // never quantity arithmetic or engine operations. A gap/stale view or receiver
    // allocation failure requires an authoritative snapshot, never gameplay replay.
    class InventoryViewConsumer final : public InventoryNotificationConsumer
    {
        InventoryViewSnapshot& mViews;
        InventoryTransferSuccess mSuccess;
        size_t mNext = 0;

    public:
        InventoryViewConsumer(InventoryViewSnapshot& views, InventoryTransferSuccess success)
            : mViews(views), mSuccess(success)
        {
        }
        void receive(InventoryNotificationIntent intent) override;
    };
}

#endif
