#ifndef TES3MP_NATIVE_INVENTORY_HOST_HPP
#define TES3MP_NATIVE_INVENTORY_HOST_HPP
#include "../native_inventory_service.hpp"
#include <tes3mp/player_identity.hpp>
#include <filesystem>

namespace TES3MP::Native
{
    // Owns loaded content/readers before the persistent inventory service.
    // The trusted startup descriptor is bounded text, in this exact order:
    // native-inventory-5
    // manifest HEX
    // config "OpenMW configuration directory"
    // players PLAYER_ID PLAYER_ID (already registered established characters)
    // actors "NPC_BASE" "NPC_BASE"
    // loot LEVEL SEED (trusted fresh-campaign leveled-loot inputs)
    // interior "INTERIOR_NAME"
    // cell interior:SPACE_ID
    // Discover all enabled, winning container placements in one interior (1..32).
    // Any unsupported placement rejects the whole bootstrap with a diagnostic.
    // V5 initializes each player's carried inventory from its winning NPC base
    // record, then containers, through one fixed/leveled loot stream. It does
    // not auto-equip, import character saves or run chargen/actor/item scripts.
    // Existing native-inventory-4 uses the legacy actor lines:
    // actors "NPC_BASE" COUNT "NPC_BASE" COUNT
    // shirt "CLOTHING_BASE"
    // Existing native-inventory-3 also remains supported by
    // replacing the interior line with:
    // container "INTERIOR_NAME" "ORIGIN_PLUGIN" REFERENCE_INDEX
    // Changing descriptor versions changes saved identity and requires a new campaign or
    // explicit migration; startup never discards a mismatched campaign.
    // Fresh contents use OpenMW fixed/leveled inventory loading. Recovery never
    // runs loot loading. Scripts, locks and traps require unavailable services.
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
