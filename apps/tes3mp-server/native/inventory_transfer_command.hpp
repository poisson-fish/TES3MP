#ifndef TES3MP_NATIVE_INVENTORY_TRANSFER_COMMAND_H
#define TES3MP_NATIVE_INVENTORY_TRANSFER_COMMAND_H

#include "inventory_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace TES3MP::Native { struct FileFaults; }

namespace MWWorld::Testing
{
    class DisposableTransferRehearsal;
    class TransferFileSink;
    struct SaveBindings;
    using TES3MP::Native::FileFaults;

    // Owned app-local values, not a wire protocol or a durable request identity.
    using TES3MP::Native::InventoryInstanceId;

    struct InventoryTransferCommand
    {
        InventoryInstanceId mSourceOwner, mDestinationOwner, mInitiator, mItem;
        std::int32_t mQuantity = 0;
        std::uint64_t mExpectedRevision = 0;
        bool operator==(const InventoryTransferCommand&) const = default;
    };

    // Trusted per-call authorization supplied by the server composition, never
    // derived from command input. This test boundary supports either fixed owner
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

    // Test-target-only, synchronous, serialized access. The fixture fixes the
    // two owner stores and save-envelope initiator. The separately authorized
    // caller may alternate; commands must match it and may choose either direction.
    // Save owner/service roles stay fixed and commands cannot redirect services.
    // Borrowed fixture/content/bindings/sink must outlive the call;
    // their consumers may not mutate or reenter. No production callers or dispatch.
    // Invalid input/preparation/encoding throws; safe file rejection returns false.
    // Uncertainty throws TestDurabilityUncertain and forbids retry of that composition.
    // Every failure preserves output (including its allocation). Success publishes
    // by noexcept swap only after persistence acceptance AND fixture installation.
    // The bounded version-4 command rejects exhausted revision/generated counters,
    // even when a stack would not require another generated identity.
    bool executeInventoryTransfer(DisposableTransferRehearsal& fixture, InventoryTransferCaller caller,
        InventoryTransferCommand command, const SaveBindings& bindings, TransferFileSink& sink, FileFaults& faults,
        std::unique_ptr<const InventoryTransferSuccess>& output);

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

    // Test-only one-shot delivery of an executeInventoryTransfer success. Detach
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
