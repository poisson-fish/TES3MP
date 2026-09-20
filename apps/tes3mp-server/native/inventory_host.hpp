#ifndef TES3MP_NATIVE_INVENTORY_HOST_HPP
#define TES3MP_NATIVE_INVENTORY_HOST_HPP
#include "../native_inventory_service.hpp"
#include "../native_environment_service.hpp"
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
    // V18 uses native-inventory-18 with V17 fields and a fresh campaign. The
    // selected NPC uses shared AiAvoidDoor timer/stuck/direction and steering
    // logic, then resumes its retained destination. Avoidance and its random
    // stream commit with physics, inventory and doors. Navigator geometry is a
    // derived cache synchronized to the queried angle image; rotation and the
    // end of avoidance rebuild the path. Background actors remain frozen; no
    // neighbor propagation, automatic door activation or general AI packages.
    // V17 uses native-inventory-17 with V16 fields and a fresh campaign. Bind
    // 1..128 automatically discovered ordinary doors to the same collision scene.
    // Each 30 Hz tick stages door proposals against all retained NPC hulls, then
    // two NPC physics steps against those door angles, alongside an inventory
    // intent. The existing area image is the sole durable door-angle owner.
    // Authenticated player reports still supplement server NPC contacts. Recovery
    // restores actor/path and door collision transforms together. Background NPCs
    // stay frozen; V17 retains contact-only behavior and authored navigation.
    // Unsupported doors remain frozen obstacles. Teleports remain unavailable.
    // V16 uses native-inventory-16 with V15 fields, one dry interior and a fresh
    // campaign. After `areas 1`, append `npc "NPC_BASE" "NAV_SETTINGS_FILE"`
    // and `destination X Y Z SPEED`. Exactly one living unscripted, nonleveled
    // NPC of that base is bound. Stock NPC hulls use base_anim/base_animkna;
    // resource bytes and navigation settings join the campaign fingerprint.
    // Frozen background geometry/actors remain collision obstacles; background
    // inventories, pickups/drops, doors and teleports are unavailable in V16.
    // Its owner inventory, physics frame, remaining engine path, contacts and
    // motion tick commit together, including one ordered inventory intent.
    // Two 60 Hz physics steps run per 30 Hz tick while either player occupies
    // the cell. Both leaving freezes the path. Recovery preserves it exactly.
    // Requires native-actor-motion capability; clients render committed motion
    // through the existing interpolation buffer. Player movement is unchanged.
    // V15 uses native-inventory-15 with V14 fields and a fresh campaign. The
    // initial campaign level and a separate seed stream drive OpenMW leveled
    // actor selection. Persist chosen records and chance-none in the area image;
    // recovery never rolls. Clients require native-leveled-actors capability.
    // Initially living unscripted actors only; AI/respawn/leveled corpses remain
    // unsupported. Marker identities own the chosen actors; at most 1024 markers
    // per campaign and 128 appearances/markers per neighborhood.
    // V14 uses native-inventory-14 and replaces the selected door with `doors auto`.
    // After the first wire cell comes `areas COUNT`, then COUNT-1 selector/cell
    // pairs. COUNT is 1..256. OpenMW discovers ordinary/teleport doors, shared
    // stores and placed items; tes3mp_native_bootstrap exports this descriptor
    // and matching manifest/client mappings from the real configuration.
    // Active scenes are the union of occupied interiors and player 3x3 exterior
    // neighborhoods. State survives resource release without rerolling loot.
    // Exterior position updates cross adjacent bound cells; interior travel
    // still uses committed teleports. Ground/doors and nearby player appearance
    // share neighborhood baselines. V14 requires the native-streaming capability
    // and a fresh campaign; its outer image saves every discovered ordinary door.
    // Bounds: 1024 shared stores, 8192 ground references, 192 ground references
    // per cell, 128 ordinary doors per cell / 4096 total, 32 teleports per cell.
    // Initial publication additionally limits each cell to 128 shared owners
    // and each player neighborhood to 128 actor appearances.
    // V13 uses native-inventory-13 with V12 fields and a fresh campaign. Either
    // interior selector may instead be `exterior X Y`, with the matching wire
    // `cell exterior:WORLDSPACE_ID:X:Y`. TES3 coordinates are bounded to
    // [-32768,32767]. OpenMW resolves winning/moved references and teleport
    // destinations; terrain queries reuse ESMTerrain vertices and stock triangles.
    // Two occupied cells pin their scenes; empty cells release scene/terrain
    // resources without reloading loot. The existing campaign-wide reference
    // budgets, selected ordinary door and door-only traversal still apply.
    // Drops crossing a bound exterior edge reject before mutation. This is not
    // adjacent-cell streaming or an automatically discovered world bootstrap.
    // V12 uses native-inventory-12 with V11 fields and a new campaign. OpenMW
    // globals initialize shared time; winning REGN records and imported weather
    // fallbacks initialize regional weather. One coherent world transaction saves
    // clock, selection timer, queued transitions and a dedicated OpenMW RNG stream.
    // All loaded regions advance at 30 Hz, including empty/interior-only sessions.
    // Legacy world-file time/weather fields no longer drive this domain.
    // V10 uses native-inventory-10, the V9 fields, then appends:
    // interior "SECOND_INTERIOR_NAME"
    // cell interior:SECOND_SPACE_ID
    // Both engine cells and wire IDs must be distinct. One bound ordinary door
    // remains in the first cell; 32 shared inventories / 64 world items are total
    // budgets across both. Active sessions pin the union of occupied interiors.
    // Empty cells release their OpenMW CellStores/query scenes and freeze door
    // motion. Canonical inventories/items/door and drop-cell membership survive
    // unloading in one session-format-6 image; returning never reloads loot.
    // V10 requires a new campaign. Existing V8/V9 identities remain unchanged.
    // V11 uses native-inventory-11 with the same fields. Discover at most 32
    // winning unscripted, unlocked, untrapped teleport doors connecting the two
    // interiors through OpenMW. Exterior/unbound/unsupported doors cannot activate.
    // Activation validates authenticated caller, cell, placement, revision and
    // reach; persists only that player's resolved destination and a new authority
    // epoch with the native image, then publishes the destination baseline.
    // Direct client cell changes are rejected; prior-epoch motion cannot undo
    // the teleport. Requires native-teleport-capable clients and a new campaign.
    // V8/V9/V10 remain unchanged; immutable teleport data needs no new save format.
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
        ServerApp::NativeEnvironmentService* environment() noexcept;
    };
}
#endif
