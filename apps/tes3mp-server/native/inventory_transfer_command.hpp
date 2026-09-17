#ifndef TES3MP_NATIVE_INVENTORY_TRANSFER_COMMAND_H
#define TES3MP_NATIVE_INVENTORY_TRANSFER_COMMAND_H

#include "inventory_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace TES3MP::Native { struct FileFaults; }

namespace TES3MP::Native
{
    // Owned app-local values, not a wire protocol or a durable request identity.
    struct InventoryTransferCommand
    {
        InventoryInstanceId mSourceOwner, mDestinationOwner, mInitiator, mItem;
        std::int32_t mQuantity = 0;
        std::uint64_t mExpectedRevision = 0;
        bool operator==(const InventoryTransferCommand&) const = default;
    };

    // Trusted per-call authorization supplied by the server composition, never
    // derived from command input. This boundary supports either fixed owner
    // as caller; it does not implement authentication or persist authorization.
    struct InventoryTransferCaller
    {
        InventoryInstanceId mInitiator;
    };

    enum class InventoryNotificationKind
    {
        ItemRemoved,
        InventoryUpdated,
        ItemAdded
    };

    struct InventoryNotificationIntent
    {
        InventoryNotificationKind mKind = InventoryNotificationKind::InventoryUpdated;
        InventoryInstanceId mOwner, mInitiator, mItem;
        std::int32_t mQuantity = 0;
        std::uint64_t mRevision = 0;
        bool operator==(const InventoryNotificationIntent&) const = default;
    };

    // Stock per-store order: optional removed, source updated, optional added,
    // destination updated. Fixed capacity includes all notification storage in
    // the preallocated success. Updated intents retain the affected item/count;
    // routing uses the owner, never substitutes the initiator for that owner.
    using InventoryNotificationBatch = std::array<std::optional<InventoryNotificationIntent>, 4>;

    struct InventoryTransferSuccess
    {
        InventoryTransferCommand mCommand;
        InventoryInstanceId mDestinationItem;
        std::int32_t mSourceCount = 0, mDestinationCount = 0;
        std::uint64_t mRevision = 0;
        InventoryNotificationBatch mNotifications;
        // Prepared stock selections, including unset and dormant identities.
        InventoryInstanceId mSourceSelection, mDestinationSelection;
        bool operator==(const InventoryTransferSuccess&) const = default;
    };

    // Shared owned-command boundary for the native runtime and retained MISC
    // migration tests. The composition resolves live references separately.
    size_t validateInventoryTransferIntent(InventoryTransferCaller caller, const InventoryTransferCommand& command,
        const std::array<InventoryInstanceId, 2>& owners, uint64_t revision, InventoryInstanceId counter);
    InventoryTransferSuccess inventoryTransferSuccess(InventoryTransferCommand command, InventoryInstanceId destination,
        int32_t sourceCount, int32_t destinationCount, uint64_t revision,
        InventoryInstanceId sourceSelection, InventoryInstanceId destinationSelection, bool removed, bool added);

    class InventoryNotificationConsumer
    {
    public:
        virtual ~InventoryNotificationConsumer() = default;
        virtual void receive(InventoryNotificationIntent intent) = 0;
    };

    enum class InventoryNotificationDeliveryStatus
    {
        NoPendingSuccess,
        Delivered,
        FailedAfterCommit
    };

    struct InventoryNotificationDelivery
    {
        InventoryNotificationDeliveryStatus mStatus = InventoryNotificationDeliveryStatus::NoPendingSuccess;
        std::uint64_t mRevision = 0;
        std::size_t mConfirmed = 0;
    };

    // One-shot delivery of a committed transfer success. Detach
    // it before callbacks; consume it on success AND failure. A throwing receiver
    // may already have handled the failing intent: stop, report the committed
    // revision and confirmed prefix, and never replay the batch or gameplay.
    // This deliberately loses the undelivered suffix; no durable queue/dedup or
    // production delivery guarantee. The receiver may allocate/throw, but must
    // not mutate/reenter gameplay. This function owns no fixture or engine views.
    InventoryNotificationDelivery consumeInventoryNotifications(
        std::unique_ptr<const InventoryTransferSuccess>& pending, InventoryNotificationConsumer& consumer) noexcept;
}

#endif
