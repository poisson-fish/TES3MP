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
    // V40 adds equipped melee weapon competition to automatic spell selection.
    // Shares stock damage/condition/hit/speed rating arithmetic; equipped
    // WhenUsed scheduling and ranged weapon selection remain pending.
    // Keeps V38 image layout; descriptor identity requires a fresh campaign.
    // V39 enables automatic known-spell selection for the unarmed selected NPC.
    // Nearest active living player, stock spell ratings, eight-tick admission,
    // one NPC flight at a time. Shared immediate launch; animation timing,
    // weapon competition and equipped WhenUsed scheduling remain pending.
    // Keeps V38 image layout; descriptor identity requires a fresh campaign.
    // V38 persists caster kind and life through projectiles, timed/constant
    // effects and NPC death history. Players retain life 1 across reconnect;
    // NPC placement/life attribution survives respawn on other targets.
    // Requires a fresh campaign; V37 and older retain their recovery layouts.
    // Trusted NPC spells/WhenUsed share player launch/contact/effect execution.
    // Automatic AI scheduling remains pending; T3C2 publishes typed cast events.
    // V37 adds constant effect arguments/ordinals, variable rolls, multiple sources
    // and all equipment slots. Attribute/skill fortification and six resistances
    // are supported; other constant behaviors reject. Maximum 512 instances.
    // Unchanged item instances retain rolls; respawn rolls fresh identities.
    // Requires a fresh campaign; V36 and older descriptors retain recovery.
    // V36 derives fixed Fortify Luck shirt instances from candidate equipment.
    // Item identity and caster survive restart; indefinite effects leave with
    // unequip/replacement. V36 requires a fresh campaign.
    // V35 carries bounded actor effect instances with source/caster identity,
    // rolled magnitude, resistance and active duration in the composed image.
    // Timed elemental/resource effects accrue on active ticks; timed resistance
    // feeds shared OpenMW melee/magic rules. Fresh campaign required.
    // V33 saves fatigue knockout for both players and the selected NPC. Active
    // actors recover fatigue at the stock combat rate; knocked actors cannot
    // attack, and unarmed hits then damage health. Fresh campaign required.
    // V32 retains up to eight independently identified projectiles in one actor
    // tick/image, resolving concurrent contacts and deduplicating pending casts.
    // It requires a fresh campaign under native-inventory-32.
    // V31 adds direct player targets using server position contact and durable
    // target kind. It requires a fresh campaign under native-inventory-31.
    // V30 adds bounded Target area effects centered on server projectile
    // contact, with one committed outcome per affected actor. Fresh campaign.
    // V29 retains bounded one-second to one-hour Resist Magicka effects for
    // either caster or selected NPC. Expiry uses composed ticks and pauses for
    // offline players or inactive NPCs. It requires a fresh campaign.
    // V28 adds a source-tagged projectile and durable WhenUsed item charge.
    // It requires a fresh campaign under native-inventory-28.
    // V27 adds one durable, server-contacted Target spell projectile under
    // native-inventory-27 and requires a fresh campaign. Its launch cost,
    // in-flight state and resolved effect share composed actor ticks.
    // V26 uses V25's image under native-inventory-26 and requires a fresh
    // campaign. It admits base-known, Always Succeeds, fixed, zero-duration
    // Self Restore Health/Magicka/Fatigue spells through the composed combat tick.
    // V24 uses V23's image under native-inventory-24 and requires a fresh
    // campaign. At the selected NPC's KF hit key, detached OpenMW stats and
    // dedicated RNG resolve accuracy, fatigue and physical weapon damage.
    // The same durable tick includes condition in the nested inventory image;
    // a rejected write installs none of them. A player inventory intent at the
    // hit key composes with NPC weapon wear into one durable candidate.
    // Authenticated player weapon attacks resolve on that tick too. NPC death
    // is durable health state and opens the selected actor's corpse inventory.
    // V23 uses V22's fields under native-inventory-23 and requires a fresh
    // campaign. Its actor wrapper saves OpenMW-initialized combat attributes,
    // resources and skills for both players and the selected NPC, plus a
    // dedicated combat RNG. Equipped weapon condition remains in the nested
    // inventory image and is sampled from that owner during tick preparation.
    // V23 does not apply damage, costs, wear or death.
    // V22 uses V21's descriptor and adds server-owned release/target and
    // contact at the authored hit key. It requires a fresh campaign.
    // V21 uses native-inventory-21 with V20 fields, then adds
    // melee "ANIMATION_GROUP" "DIRECTION" SPEED. It binds the resolved KF
    // identity and saves the selected NPC's swing snapshot with each actor
    // tick. V21 requires a fresh campaign; contacts and combat are pending.
    // V20 uses native-inventory-20 with V19 fields and a fresh campaign. It
    // accepts one dry interior or 1..9 exterior cells within the first cell's
    // 3x3 neighborhood; the first cell owns the single selected NPC. Collision
    // references are deduplicated across cells, with stock LAND heightfields.
    // The declared neighborhood is retained as one scene until completion.
    // Physics positions determine exterior crossings, including signed edges.
    // Append `processing CELLS STEPS` after destination: 0..9 cells, 0..2
    // physics steps admitted per tick. Inadequate capacity pauses the complete
    // actor step, retaining destination/path/completion. These policy limits
    // may change on restart without changing the content-bound campaign.
    // Diagnostics report running, idle, cell/step saturation, dry-domain edge
    // and unavailable path; 256 nav tiles/2048 path points remain hard bounds.
    // An unavailable route stays pending. Source and destination appearances
    // coalesce to one actor; origin interest suppresses the authored local copy.
    // Beyond-domain travel, water, teleports and sliding neighborhoods await
    // later slices. V16..V19 retain their prior domains.
    // V19 uses native-inventory-19 with V18 fields and a fresh campaign. One
    // selected traveler sustains the bound interior independently of player
    // interest. The player/traveler union runs the existing composed tick once:
    // two 60 Hz steps, at most 8192 collision references, 256 navigation tiles
    // and 2048 path points. Unavailable paths retain demand and destination.
    // Completion releases an unoccupied scene; reload validates fresh resources
    // and restores the exact committed path, avoidance/RNG and collision frame.
    // Restart retains active/completed travel, with no wall-clock catch-up.
    // Cross-cell travel, general AI and multiple travelers remain unsupported.
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
    // importing character saves or running chargen/actor/item scripts. A script-free
    // WhenStrikes weapon may equip; other enchanted gear needs effect services.
    // Recovery never auto-equips.
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
