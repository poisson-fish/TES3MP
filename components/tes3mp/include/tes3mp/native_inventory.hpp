#ifndef TES3MP_NATIVE_INVENTORY_HPP
#define TES3MP_NATIVE_INVENTORY_HPP

#include "canonical_persistence.hpp"
#include "server_command_intake.hpp"
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
    };
}
#endif
