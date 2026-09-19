#ifndef TES3MP_NATIVE_INVENTORY_HPP
#define TES3MP_NATIVE_INVENTORY_HPP

#include "canonical_persistence.hpp"
#include "server_command_intake.hpp"
#include "native_door.hpp"
#include <functional>

namespace TES3MP
{
    // The engine implementation owns detached candidates and its existing image.
    // Only the reducer supplies this serialized durability callback. Committed
    // authorizes nonthrowing engine installation before canonical publication.
    using NativeInventoryCommit = std::function<CanonicalDurabilityResult(std::span<const std::byte>)>;

    class PreparedNativeInventory
    {
    public:
        virtual ~PreparedNativeInventory() = default;
        // A door may stage relocation of the authenticated requester. The
        // reducer persists it with the unchanged native image before publication.
        virtual std::optional<Transform> playerDestination() const { return {}; }
        virtual CanonicalDurabilityResult commit(const NativeInventoryCommit& durability) noexcept = 0;
    };

    class NativeInventoryAuthority
    {
    public:
        // Application composition keeps this service and its loaded content
        // alive until the reducer and all preparations have been destroyed.
        virtual ~NativeInventoryAuthority() = default;
        virtual std::unique_ptr<PreparedNativeInventory> prepareInventory(
            const CanonicalServerState& players, const ServerCommandProposal& command) = 0;
        virtual std::span<const std::byte> inventoryImage() const noexcept = 0;
        virtual bool hasNativeDoor() const noexcept { return false; }
        virtual bool ownsNativeDoor(InteractiveObjectId) const noexcept { return false; }
        virtual bool requiresDoorTraversal() const noexcept { return false; }
        virtual bool streamsPlayerAreas() const noexcept { return false; }
        virtual bool hasLeveledActors() const noexcept { return false; }
        // Resolve inherited player motion against the native area domain. This
        // never grants interior travel; the engine adapter may permit contiguous
        // exterior crossings while explicit relocations remain transactional.
        virtual std::optional<CellId> movementCell(CellId current, Position3) const { return current; }
        virtual bool allowsCellTransition(CellId, CellId, Position3) const { return false; }
        virtual std::unique_ptr<PreparedNativeInventory> prepareDoorActivation(
            const CanonicalServerState&, const ServerCommandProposal&) { return {}; }
        virtual std::unique_ptr<PreparedNativeInventory> prepareDoorStep(
            const CanonicalServerState&, ServerTick, float) { return {}; }
        virtual void reportDoorObstruction(const CanonicalServerState&, const ClientDoorObstruction&, ServerTick) {}
    };
}
#endif
