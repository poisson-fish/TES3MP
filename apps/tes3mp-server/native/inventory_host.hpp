#ifndef TES3MP_NATIVE_INVENTORY_HOST_HPP
#define TES3MP_NATIVE_INVENTORY_HOST_HPP
#include "../native_inventory_service.hpp"
#include <tes3mp/player_identity.hpp>
#include <filesystem>

namespace TES3MP::Native
{
    // Owns loaded content/readers before the persistent inventory service.
    // The trusted startup descriptor is bounded text, in this exact order:
    // native-inventory-1
    // manifest HEX
    // config "OpenMW configuration directory"
    // players PLAYER_ID PLAYER_ID (already registered established characters)
    // actors "NPC_BASE" COUNT "NPC_BASE" COUNT
    // shirt "CLOTHING_BASE" WIRE_PROTOTYPE_ID
    // container "CONTAINER_BASE" WIRE_CONTAINER_ID
    // cell interior:SPACE_ID  (or exterior:SPACE_ID:X:Y)
    // position X Y Z
    // Counts seed only a fresh domain. The base container starts empty; placed
    // reference/base inventory import is deliberately not implied by this format.
    class InventoryHost
    {
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    public:
        InventoryHost(const std::filesystem::path& descriptor, const ContentManifest& manifest,
            const PlayerIdentityRegistry& players, CredentialCrypto& crypto, std::span<const std::byte> restored);
        ~InventoryHost();
        ServerApp::NativeInventoryService& service() noexcept;
    };
}
#endif
