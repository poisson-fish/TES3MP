#ifndef TES3MP_SERVER_NATIVE_INVENTORY_SERVICE_HPP
#define TES3MP_SERVER_NATIVE_INVENTORY_SERVICE_HPP
#include "inventory_interest_projection.hpp"
#include <tes3mp/combat_replication.hpp>
#include <tes3mp/native_inventory.hpp>

namespace TES3MP::ServerApp
{
    struct NativeTravelDiagnostics
    {
        enum class Status { Idle, Running, CellCapacity, StepCapacity, Boundary, NoPath };
        Status status = Status::Idle;
        size_t demandedCells = 0;
        size_t cellLimit = 0;
        size_t stepLimit = 0;
        uint64_t tick = 0;
        std::array<float, 3> position{};
        bool completed = false;
    };
    // App-owned projection boundary. The independent reducer never sees engine
    // types; the server uses the same owned delivery for join, resume and ticks.
    class NativeInventoryService : public NativeInventoryAuthority
    {
    public:
        // Cache/scheduling lifetime follows committed sessions and native travel.
        // Canonical inventory/world state outlives these loaded cell resources.
        virtual void synchronizeCells(const CanonicalServerState&) {}
        virtual std::optional<NativeTravelDiagnostics> travelDiagnostics() const { return {}; }
        virtual std::optional<InventoryInterestDelivery> projectInventory(const CanonicalServerState& players,
            SessionId target, ServerTick tick, CanonicalRevision revision,
            const PreparedNativeInventory* candidate = nullptr) const = 0;
        virtual std::optional<LatestWinsCombatSnapshot> projectCombat(const CanonicalServerState&,
            SessionId, ServerTick, CanonicalRevision, const PreparedNativeInventory* = nullptr) const { return {}; }
    };
}
#endif
