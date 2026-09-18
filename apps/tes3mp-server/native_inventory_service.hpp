#ifndef TES3MP_SERVER_NATIVE_INVENTORY_SERVICE_HPP
#define TES3MP_SERVER_NATIVE_INVENTORY_SERVICE_HPP
#include "inventory_interest_projection.hpp"
#include <tes3mp/native_inventory.hpp>

namespace TES3MP::ServerApp
{
    // App-owned projection boundary. The independent reducer never sees engine
    // types; the server uses the same owned delivery for join, resume and ticks.
    class NativeInventoryService : public NativeInventoryAuthority
    {
    public:
        // Cache/scheduling lifetime follows committed active sessions only.
        // Canonical inventory/world state outlives these loaded cell resources.
        virtual void synchronizeCells(const CanonicalServerState&) {}
        virtual std::optional<InventoryInterestDelivery> projectInventory(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedNativeInventory* candidate = nullptr) const = 0;
    };
}
#endif
