# Durable decisions

These rules describe the accepted direction. CURRENT.md distinguishes it from
the implementation still running. Replace obsolete rules; add only consequential
decisions, each in a short paragraph. This reset supersedes the checkpoint's
clean-room gameplay, custom-script-only, and baked-subset product constraints.

## Product and reuse

**OpenMW gameplay is the foundation.** The target is modded cooperative OpenMW,
including Tamriel Rebuilt, combat, shared quests/world state, and graphical mods.
The dedicated server reuses/refactors engine content, mechanics, world, and
scripting. An independent reimplementation of each gameplay system is not the
default. A successful small slice must improve this route, not broaden a parallel
catalog-based game indefinitely.

**Independent networking; engine-dependent runtime.** Keep components/tes3mp
portable and use owned integration values. A distinct server-runtime leaf may
link selected OpenMW libraries and use their types internally. Extraction may
change OpenMW's own callers so single-player and server use the same behavior.
Do not force an engine-wide library cleanup before proving a vertical slice.

**Compatibility targets the chosen OpenMW version.** The base remains 0.51.0
until an explicit upgrade. Existing TES3MP 0.8 wire/API/save compatibility is not
required. Existing engine or older multiplayer code may be reused when inspection
and tests justify it; there is no blanket ban based on its origin. Native/MWSE-only
mods are outside automatic compatibility. Broad mod support is a tested goal,
not a promise that arbitrary scripts have unambiguous multiplayer meaning.

**One gameplay loadout.** Use OpenMW's configuration, encoding, load order,
deletion/override handling, references, and normalization. Bind gameplay plugins,
scripts, settings, and relevant resources to server/client/save identity. Python
may package/hash/cache, but should cease being a separate ESM semantic authority.
Network IDs and bounded caches must not require manual catalog entries for every
mod item, actor, or quest. Unsupported behavior must be visible, never silently
delegated to a client's local simulation.

## Authority and cooperative semantics

**One server world.** The server owns gameplay outcomes, actors, object changes,
time, regional weather, and player resources. Each player has a distinct
character/inventory. Clients supply authenticated intent and present committed
results; prediction cannot author rewards, damage, or world mutations. Old and
new paths may not be simultaneous authorities. Client-authoritative movement is
an inherited limitation to replace/validate for the new combat path.

**Shared cooperative campaign first.** Start with one party's quest progression
and shared world consequences. Dialogue, Player operations, and local item
scripts use an explicit initiating/owning player context. Global gameplay
scripts run once server-side. Preserve player-specific condition/reward context;
do not multiply a script's item grants by connected-player count or clone unique
world loot. Retry/reconnect must not replay rewards. Additional parties, personal
quest instances, and alternative reward policies need a later explicit design.

**Simulation and presentation have different owners.** Script UI requests,
animations, audio, and graphics go to relevant clients. Time/weather changes and
their gameplay effects originate on the server. Pure visual replacements may
vary; collision/bounds or script-affecting resources belong to gameplay identity.
Multiple players require explicit active-region scheduling, not one player's
scene or menu state controlling the whole server.

**Script compatibility includes execution and state.** Reuse MWScript/OpenMW Lua
where appropriate with server bindings, explicit context, and resource limits.
Engine-supported serialized locals/Lua state and dynamic definitions are allowed;
the old requirement to rewrite all logic as catalog-declared variables is retired.
Do not serialize process pointers, raw memory, rendering objects, or live sessions.
Persist only supported semantic state under a validated server save boundary.

## Integrity and migration

**Atomic failure remains required.** Validate and bound external input before
allocation/mutation. Rejected commands, failed preparation, and failed durability
must not leave partial gameplay or publish success. Separate/stage script and
presentation effects as part of the operation. Copying engine objects or catching
exceptions is not proof of isolation. A legitimate failed cast or missed attack
may still have the resource costs prescribed by the game's rules.

**Persistence is coherent and server-owned.** Preserve the current
durability-before-install/publication guarantee during migration. Engine field
serializers may be reused inside a server-owned, content/version-bound format;
the existing independent schema is not sacred. Restore validates the complete
world/player relationship before installation. This pivot does not authorize
silently changing to checkpoint-only acknowledgment or losing saved progress.

**Determinism is explicit, not assumed.** Server-owned order/ticks and saved RNG
state remain important. Measure scheduling and stream consumption when reusing
engine code; do not require copying old PRNG/formula implementations for their own
sake or claim cross-platform bitwise replay without evidence. Network authority
does not require clients to run identical lockstep simulations.

**Preserve proven network boundaries.** Keep authentication/session separation,
stable instance identity, stale/retry rejection, reliable versus latest-state
traffic, bounded queues, backpressure, and secret-free evidence. Existing direct-IP
transport encryption does not authenticate server endpoint identity; the pivot
does not repair that inherited limitation. Protocol/development-save revisions
may change deliberately without maintaining two gameplay worlds.

**Retirement follows cutover.** Preserve the checkpoint and functioning old paths
until replacements cover their callers and relevant failure cases. Then delete
obsolete code/configuration/tests instead of retaining indefinite compatibility
branches. Small behavioral fixtures remain useful; old architectural assertions
must not force the new runtime back into the abandoned product.
