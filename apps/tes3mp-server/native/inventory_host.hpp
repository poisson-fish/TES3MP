#ifndef TES3MP_NATIVE_INVENTORY_HOST_HPP
#define TES3MP_NATIVE_INVENTORY_HOST_HPP
#include "../native_inventory_service.hpp"
#include <tes3mp/player_identity.hpp>
#include <filesystem>

namespace TES3MP::Native
{
    // Owns loaded content/readers before the persistent inventory service.
    // The trusted startup descriptor is bounded text, in this exact order:
    // native-inventory-8
    // manifest HEX
    // config "OpenMW configuration directory"
    // players PLAYER_ID PLAYER_ID (already registered established characters)
    // actors "NPC_BASE" "NPC_BASE"
    // loot LEVEL SEED (trusted fresh-campaign leveled-loot inputs)
    // interior "INTERIOR_NAME"
    // cell interior:SPACE_ID
    // V9 adds exactly one ordinary door to the coherent durable session. Use
    // native-inventory-9 and insert, between interior and cell:
    // door "ORIGIN_PLUGIN" REFERENCE_INDEX
    // The winning placement and descriptor version join content identity.
    // V8 campaigns remain V8; V9 requires a new campaign (no implicit upgrade).
    // Requires native-door-capable clients. The server commits activation and
    // motion; each client reports only contact with its local player. Reports
    // expire after ten observed server ticks and never supply door positions.
    // The item-placement scene still uses authored geometry for its floor rays.
    // V7 also binds 0..64 winning unscripted placed items. Their complete active
    // membership shares the inventory image; recovery never reloads placements.
    // Pickup consumes a whole world reference; drops split unequipped inventory
    // stacks. V8 resolves stock cursor/floor queries and item bounds on a headless
    // OpenMW scene, then commits the stationary position with the inventory image.
    // Model bytes join the saved content binding; camera input is bounded, without
    // additional reach, supporting-surface or overlap rules. The scene contains
    // initial unscripted non-actor geometry and committed world items in one interior.
    // V7 retains its earlier drop at the authoritative player position.
    // V7 permits an interior without shared stores; V6 retains its old domain.
    // V6 discovers winning containers, NPCs and creatures in one interior
    // (1..32 total shared inventories), sorted together by stable identity.
    // NPCs and weapon-bearing creatures auto-equip ordinary gear. Content-defined
    // corpses support take/put and replicated equipment; living access requires
    // unavailable theft/companion services. No AI, scripts, death transitions,
    // leveled actor spawning or corpse disposal are activated by inventory binding.
    // Any unsupported placement rejects the whole bootstrap with a diagnostic.
    // V5 retains container-only discovery. V5/v6 initialize player inventories
    // from their winning NPC bases, then shared owners, through one loot stream.
    // They auto-equip ordinary starting gear using stock NPC selection, without
    // importing character saves or running chargen/actor/item scripts. Selected enchanted
    // gear rejects until the required services exist; recovery never auto-equips.
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
