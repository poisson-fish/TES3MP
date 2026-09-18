#include "inventory_transfer_command.hpp"
#include "runtime_phases.hpp"
#include <limits>
#include <stdexcept>

namespace TES3MP::Native
{
    size_t validateInventoryTransferIntent(InventoryTransferCaller caller, const InventoryTransferCommand& command,
        const std::array<InventoryInstanceId, 2>& owners, uint64_t revision, InventoryInstanceId counter)
    {
        for (auto value : { command.mSourceOwner, command.mDestinationOwner, command.mInitiator, command.mItem })
            if ((value.mIndex == 0 && value.mContentFile == -1) || value.mContentFile < -1)
                throw std::invalid_argument("Inventory command requires valid instance IDs");
        if (command.mInitiator != caller.mInitiator || command.mQuantity <= 0
            || command.mSourceOwner == command.mDestinationOwner || command.mExpectedRevision != revision
            || revision == std::numeric_limits<size_t>::max() || counter.mContentFile != -1
            || counter.mIndex == std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("Inventory command caller, quantity, owners or counters invalid");
        const size_t source = command.mSourceOwner == owners[0] ? 0 : 1;
        if (command.mSourceOwner != owners[source] || command.mDestinationOwner != owners[1 - source]
            || (caller.mInitiator != owners[0] && caller.mInitiator != owners[1]))
            throw std::invalid_argument("Inventory command current owner/initiator mismatch");
        return source;
    }

    InventoryTransferSuccess inventoryTransferSuccess(InventoryTransferCommand command, InventoryInstanceId destination,
        int32_t sourceCount, int32_t destinationCount, uint64_t revision,
        InventoryInstanceId sourceSelection, InventoryInstanceId destinationSelection, bool removed, bool added)
    {
        const auto intent = [&](InventoryNotificationKind kind, InventoryInstanceId owner, InventoryInstanceId item) {
            return InventoryNotificationIntent{ kind, owner, command.mInitiator, item, command.mQuantity, revision };
        };
        InventoryNotificationBatch notifications;
        if (removed) notifications[0] = intent(InventoryNotificationKind::ItemRemoved, command.mSourceOwner, command.mItem);
        notifications[1] = intent(InventoryNotificationKind::InventoryUpdated, command.mSourceOwner, command.mItem);
        if (added) notifications[2] = intent(InventoryNotificationKind::ItemAdded, command.mDestinationOwner, destination);
        notifications[3] = intent(InventoryNotificationKind::InventoryUpdated, command.mDestinationOwner, destination);
        return { command, destination, sourceCount, destinationCount, revision, notifications, {}, sourceSelection, destinationSelection };
    }

    InventoryNotificationDelivery consumeInventoryNotifications(
        std::unique_ptr<const InventoryTransferSuccess>& pending, InventoryNotificationConsumer& consumer) noexcept
    {
        Allocations::InPhase phase(Allocations::Phase::Delivery);
        auto committed = std::move(pending);
        if (!committed)
            return {};
        InventoryNotificationDelivery result{ InventoryNotificationDeliveryStatus::Delivered, committed->mRevision, 0 };
        try
        {
            for (const auto& intent : committed->mNotifications)
                if (intent)
                {
                    consumer.receive(*intent);
                    ++result.mConfirmed;
                }
            for (const auto& intent : committed->mBulkNotifications)
            {
                consumer.receive(intent);
                ++result.mConfirmed;
            }
        }
        catch (...)
        {
            result.mStatus = InventoryNotificationDeliveryStatus::FailedAfterCommit;
        }
        return result;
    }
}
