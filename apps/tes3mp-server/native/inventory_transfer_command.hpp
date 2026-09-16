#ifndef TES3MP_NATIVE_INVENTORY_TRANSFER_COMMAND_H
#define TES3MP_NATIVE_INVENTORY_TRANSFER_COMMAND_H

#include <cstdint>
#include <memory>

namespace MWWorld::Testing
{
    class DisposableTransferRehearsal;
    class TransferFileSink;
    struct SaveBindings;
    struct FileFaults;

    // Owned app-local values, not a wire protocol or a durable request identity.
    struct InventoryInstanceId
    {
        std::uint32_t mIndex = 0;
        std::int32_t mContentFile = -1;
        bool operator==(const InventoryInstanceId&) const = default;
    };

    struct InventoryTransferCommand
    {
        InventoryInstanceId mSourceOwner, mDestinationOwner, mInitiator, mItem;
        std::int32_t mQuantity = 0;
        std::uint64_t mExpectedRevision = 0;
        bool operator==(const InventoryTransferCommand&) const = default;
    };

    struct InventoryTransferSuccess
    {
        InventoryTransferCommand mCommand;
        InventoryInstanceId mDestinationItem;
        std::int32_t mSourceCount = 0, mDestinationCount = 0;
        std::uint64_t mRevision = 0;
        bool operator==(const InventoryTransferSuccess&) const = default;
    };

    // Test-target-only, synchronous, serialized access. The fixture fixes the
    // source/destination stores and authorized initiator; commands cannot redirect
    // its services. Borrowed fixture/content/bindings/sink must outlive the call;
    // their consumers may not mutate or reenter. No production callers or effects.
    // Invalid input/preparation/encoding throws; safe file rejection returns false.
    // Uncertainty throws TestDurabilityUncertain and forbids retry of that composition.
    // Every failure preserves output (including its allocation). Success publishes
    // by noexcept swap only after persistence acceptance AND fixture installation.
    bool executeInventoryTransfer(DisposableTransferRehearsal& fixture, InventoryTransferCommand command,
        const SaveBindings& bindings, TransferFileSink& sink, FileFaults& faults,
        std::unique_ptr<const InventoryTransferSuccess>& output);
}

#endif
