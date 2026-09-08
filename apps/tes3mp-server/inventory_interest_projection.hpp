#ifndef TES3MP_SERVER_INVENTORY_INTEREST_PROJECTION_HPP
#define TES3MP_SERVER_INVENTORY_INTEREST_PROJECTION_HPP

#include <tes3mp/canonical_state.hpp>
#include <tes3mp/inventory_replication.hpp>
#include <tes3mp/inventory_world.hpp>
#include <tes3mp/transport.hpp>

#include <optional>
#include <vector>

namespace TES3MP::ServerApp
{
    struct InventoryInterestDelivery
    {
        SessionId targetSession;
        std::vector<ReliablePlayerInventoryBaseline> playerInventory;
        std::vector<ReliableContainerInventoryBaseline> containers;
        std::vector<ReliableGroundItemBaseline> groundItems;
        std::optional<LatestWinsEquipmentSnapshot> equipment;
    };

    std::optional<InventoryInterestDelivery> projectInventoryInterestBaseline(const CanonicalServerState& players,
        const CanonicalInventoryWorld& inventory, SessionId target, ServerTick tick,
        CanonicalRevision canonicalRevision);

    bool appendInventoryInterestMessages(std::vector<std::vector<std::byte>>& owned,
        std::vector<OutboundQueueSet::AtomicMessage>& messages, TransportConnectionId connection,
        const InventoryInterestDelivery& delivery);
}

#endif
