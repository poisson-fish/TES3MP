# Durable decisions

CURRENT.md records implementation. M4 runtime decisions are approved;
script-scoping proposals remain labeled.

## Product, authority and reuse

**OpenMW gameplay is the foundation.** Reuse engine content, mechanics, world and
scripting, including TR; no independent formulas, catalogs or quest language.
Keep baseline 0.51.0. TES3MP 0.8 compatibility is unnecessary; mod support requires
evidence.

**Independent networking; native runtime.** Keep components/tes3mp portable and
engine-independent. An app-local runtime may extract gameplay shared with stock
OpenMW callers. Preserve dependency checks.

**One gameplay loadout.** OpenMW resolves configuration, encoding, load order,
overrides/deletions and references. Bind plugins/scripts/settings/resources to
server/client/save identity. Python may package/hash/cache, not reinterpret ESM.
Unsupported behavior rejects visibly.

**One server authority.** The server owns actors, objects, player resources,
time/weather and outcomes in every story scope. Clients submit authenticated intent
and present commits. Prediction cannot author damage, rewards or world mutations.
Validate inherited movement for combat. Schedule the player-area union independently
of individual menus/scenes. NPC authority has no client ownership leases.

**M3 doors:** shared OpenMW rules own durable activation, reversal, angle and
replication. Player contact reports bind session, placement, sequence and tick;
expire after ten ticks; and invalidate on reconnect/reversal. They supplement
server physics until movement cutover. Latency may clip; preserve persistence.

**Presentation is separate.** Route UI, visual animation, audio and graphics to relevant
clients. Pure visual replacements may vary; collision/bounds and script-affecting
resources belong to gameplay identity. Reuse supported MWScript/Lua execution and
semantic serialization with explicit context and resource limits. Never persist
raw memory, process pointers, rendering objects or live sessions.

## M4 actor simulation

**Runtime ownership.** An app-local OpenMW actor runtime exposes owned commands/
snapshots and explicit activity/identity. AI, physics, combat, wear, charge, death
and loot share authoritative inventories. Preserve stock callers and dependency
checks; avoid World/UI wrappers.

**Gameplay animation.** Keep CPU movement, hit keys and projectile/spell releases;
clients render commits. Bind timing/collision resources to gameplay identity;
null presentation cannot suppress mechanics. V22 persists full-wind-up selection
and rechecks reach at the KF hit key. Inherited movement uses center reach until
native hulls are bound.

**Travel scheduling.** Simulate the player/traveler area union once per actor.
Bound work; persist destinations, completion and stock Travel guards. Saturation
pauses substeps without resetting travel. Stable placement IDs prevent duplicate
actors. Freeze offline players.

**Composed ticks.** One transaction stages intents, simulation, resources, effects,
wear, death/loot, RNG and receipts; persist before publication. WhenUsed pays at
launch, and source identities survive item movement. Door contact precedes physics.
Persist avoidance and private RNG.

**Casting authority and recovery.** Retain caster kind, ID and launch life through
flight, effects and death history. NPC respawn clears body effects, while attribution
on others survives. Trusted NPC commands never enter client input. Death cancels
NPC flights. New descriptor domains require fresh campaigns.

V39-V42 select the nearest visible living player with ID ties every eight ticks.
Stock ratings compare weapons/ammunition, equipped WhenUsed and spells; unsupported
plans reject. Persist source/life/target, payment, RNG, KF phases, effects and casts
together. Inactive areas pause; launch revalidates. T3C5/capability 23 and T3C2
events retain caster life. V46 projects swing sections without replaying gameplay
keys or Lua callbacks; clients never advance the authoritative clock.

**Equipped passive sources.** Candidate equipment selects constant effects by item
instance and effect ordinal before combat. Rolls persist while that instance stays
equipped; replacement and respawn install new sources in the same transaction.
Recovery checks arguments, source membership and magnitude bounds without rolling.
Modifiers overlay detached combat stats and never accumulate in saved base state.

**Combat latency.** Predict local swing/cast presentation only; the server owns
gameplay consequences. Start with server-time contacts; measure latency before adding
bounded historical actor/obstacle queries. Full-world rewind is not selected. Reuse
timestamped replication, reliable action/life events and latest-wins motion.
V31 uses a 32-unit player sphere at the active server position, comparing earliest
projectile contact with NPC/world collision until shared player hulls are bound.

**Melee state.** Negative fatigue prevents attacks and redirects unarmed damage to
health. Stock fatigue restoration continues while active. V50 persists authored
knockout/knockdown clock: exhaustion loops, restored fatigue lets the current clip
finish its get-up tail. Shared stock health-hit rolls select physical knockdown.
New hits do not restart an active knockdown; inactive actors pause, and death/respawn
clear body clocks. V34 shares shield visibility and CPU hit recovery. V43 binds
participant hit resources in the fingerprint; recovery validates timers against
their clips. Fresh campaigns are required.

V44 persists pre-release targets and clip recipes. Detached transactions equip
selected weapons; recovery never re-equips. Completion resumes selection; breakage
and passive-source removal commit together.

**V52 actor presentation.** Clients sample committed motion/action without gameplay
callbacks. Persist swing/body clips and life boundaries; hold on pauses or starvation.
T3C7/capability 25; fresh campaigns. Budget four latest-state streams per pump.

V53 adds participant-bound player casts to this clock. Release revalidation/payment
and interruption commit atomically; offline casts pause. T3C8/capability 26;
fresh campaigns.

T3C9/capability 27 carries bounded aggregate visibility magnitudes with actor
presentation. Clients feed stock Light, NightEye and Detect consumers; snapshots
restore loops and expiry after reconnect. An action suppresses an equipped
Invisibility effect without deleting its durable source or rerolling magnitude.

V56/T3D0/capability 28 adds eight bounded movement magnitudes to the committed
actor presentation snapshot. The inherited player movement path consumes them in
OpenMW; the server retains source, caster and deadline ownership. WaterBreathing's
effect index zero is distinguished from legacy ResistMagicka by its source identity.
The later player movement authority cutover remains subject to M4 collision and
combat acceptance.

**Movement smoothness (target).** Cut over after smooth replication and unified
engine collision. Until then validate inherited combat contacts on the server.
Start with stock physics; tune snapshot frequency separately. Interpolate remote
snapshots with bounded extrapolation. Predict locally with shared fixed steps;
restore acknowledged physics and replay input. Timestamp obstacles, blend visual
corrections, reset on teleport/respawn/cell change, and measure jitter and
correction overruns.

## Cooperative progression design

**Requirement:** characters, including late joiners, can complete campaigns
independently. Others cannot permanently block opportunities; personal choices
retain consequences. Use OpenMW APIs and engine rules without mandatory quest edits.

### Shared world, personal progression

**Direction:** NPCs share one world and return after death. Temporary unavailability
is acceptable. Journals, choices, relationships, faction progress, rewards and
script history are personal. NPC death does not create private campaigns.

**Lifecycle contract (M4).** Stable placement/life generations, attributed death
events, corpse inventories and deadlines survive unload/restart. Old requests
cannot affect new lives. OpenMW supplies reconstruction data; authored corpses,
summons, scripted spawns and deleted placements are not permanent spawn points.
M4 records causal events; M5 owns personal quest credit.

**V25 deadline rule:** one initially living content placement respawns after a
descriptor-bound delay of authoritative 30 Hz ticks (capture: 27,000). Game-time
skips and downtime do not count. Restore actor/inventory together with fresh item
identities. Wider spawn policy awaits evidence.

| State | Ownership and lifetime |
|---|---|
| NPC body, combat/AI, corpse and inventory | Shared life; authoritative respawn. |
| Character credit, choices, relationships and rewards | Personal; survives NPC revival. |
| Placement identity and life generation | Server-owned; separates new deaths from replays. |

Reset health/effects, AI and placement through engine behavior; preserve story locals.
Never refill looted corpses or apply stale-life requests. Renewable loot and camping
fairness need policies; quest rewards remain one-time.

[Stock NPC respawn](../../apps/openmw/mwclass/npc.cpp) checks flags/delays and restores
actor data/placement; V25 uses bounded placement and its own durable deadline.
[Death counting](../../apps/openmw/mwmechanics/actors.cpp) aggregates by record ID;
quest-facing history needs character/participation context. A keeps credit through
revival; B can earn their own later. Define party, summon, environmental and pre-quest
credit generically; neither credit everybody nor require an already-active quest.

### Scripted quest execution contract

**Proposed engine fix:** run unchanged supported scripts with explicit character,
story scope, source reference/life and causal event identity. Background scripts,
timers and Lua callbacks retain that context; never select the first connected
player. Stock single-player uses one default context. Scope rules apply to engine operations, never quest names.

| Operation/state | Default rule |
|---|---|
| Journal, topics, choices, faction, relationship, player inventory | Read/write the initiating character. |
| Legacy globals, script locals, running/stopped scripts, event cursors | Personal story namespace; cross-script lookups retain it. Engine-owned world globals have explicit contracts. |
| Combat, native activation, shared AI and actor respawn | One physical writer; authenticated, attributed events. |
| Script Disable/Delete, relocation, non-player inventory/AI edits, persistent spawns | Scoped reference changes; never destroy another character's access. Materialize a coherent scene variant when physics diverges. |
| Unsupported or ambiguous mixed effects | Reject atomically with script/API/context diagnostics; never silently run globally. |

Shared simulation runs once; personal scripts/spawns are scoped per character.
Locals key by character/script/stable reference and survive respawn with cursors;
lifecycle fields reset separately. Scripted combat enters the shared action path.
API coverage establishes mod support without quest rewrites.

**Death and temporary unavailability:** replace the consumable shared OnDeath flag
with per-character/script event cursors and attributed death history. Record events
even before quest acceptance. Proposed credit includes the initiating character and
eligible consenting helpers; define summons/environmental attribution explicitly.
A keeps credit after revival; uninvolved B receives no narrative death event.

For B's script, another's combat/death makes a reference temporarily unavailable.
Health/liveness, lookup, existence and enumeration must preserve that distinction.
Defer polling/handlers without committing; wake on lifecycle change, revalidate and
rerun from committed state. Rendering/combat show the corpse; B's own failures retain
normal consequences. Journal numbers cannot establish success/failure.

Deferral is distinct from failure: it must not disable a global/local script.
Persist the wake dependency and event identity; bound queues/retries and report
unresolved waits rather than spin. Pause affected story deadlines during enforced
unavailability/offline suspension; raw time reads must use a consistent story clock.
Persistent scripted removal uses the scoped rule above, not an endless respawn wait.
Indirect observations require coverage.

**Atomic execution:** stage globals, locals, journal, inventories, references, RNG,
events, timers and presentation together. Unavailable reads, stale lives, unsupported
opcodes, exhausted budgets or failed durability discard the invocation. Deduplicate
invocation retries; preserve legitimate recurrence and content's one-time reward
flags. Isolate Lua tables/closures/callbacks, not merely engine calls or exceptions.

**Owning seams:** [interpreter context](../../apps/openmw/mwscript/interpretercontext.cpp),
[dialogue operations](../../apps/openmw/mwscript/dialogueextensions.cpp),
[actor queries/events](../../apps/openmw/mwscript/statsextensions.cpp),
[world operations](../../apps/openmw/mwscript/miscextensions.cpp) and
[script scheduling](../../apps/openmw/mwscript/scriptmanagerimp.cpp).
All direct Environment accesses and Lua equivalents must respect the boundary.
Preserve engine execution/serialization; no substitute quest language.

M5 must prove these contracts. Shared loot requires renewable access/transfer
rules, not an assumed quest-item classifier. Preserve v8 until explicit migration.

## Integrity and migration

**Atomic coherent persistence.** Validate before allocation/mutation. Stage gameplay,
scripts and presentation; rejection leaks nothing. Copies/exception handling alone
cannot isolate mutations. Misses/failed casts retain prescribed costs. Persist before
installation/publication; restore content/version-bound world/player state atomically.
No checkpoint-only acknowledgments, silent resets or parallel canonical files.

**Borrowed lifetime.** Ptr copies retain weak destruction witnesses. Construction
starts a new lifetime; assignment preserves the destination. Check witnesses
before dereferencing, then registry/script ownership. Addresses/serialized checks
cannot establish lifetime or authorize installation.

**Native inventory cutover.** Session image and dispositions share one file transaction,
never CanonicalInventoryWorld. One player inventory intent composes with the native
actor tick and hit wear; later intents reject in ingress order. Fingerprints bind
roles, winning placements and domain.
Reference IDs use record-plugin order, not reader slots. One registry/counter and
stock loot stream initialize players then placements in stable order. Recovery never
reloads/rerolls/auto-equips. Identities preserve raw condition/light-time/charge bits.

**Versioned domains.** V3–V13 retain their documented
[meanings](../../apps/tes3mp-server/native/inventory_host.hpp). Descriptor changes
require fresh campaigns or explicit migration; recovery never resets, rerolls loot
or auto-equips.

**Native time/weather (v12+).** OpenMW calendar, REGN and fallbacks advance
4,096 regions maximum at 30 Hz regardless of occupancy/menus. Persist RNG, clock,
timers and transitions with content/settings/seed binding. No wall-clock catch-up,
client writers or legacy scripts.

**Player-area streaming (v14).** OpenMW discovers 1–256 cells; canonical state
stays resident. Occupied interiors and player 3×3 exterior neighborhoods retain
scenes; unloading freezes doors. Teleports commit destination/epoch. Baselines
carry loot/doors/players/equipment. Bootstrap requires established identities,
retains inherited movement and rejects unsupported scripts.

**Initial leveled actors (v15).** A separate campaign-seeded OpenMW RNG stream and
loot level select records in stable placement order. Persist chance-none and
other selections with inventory/doors; recovery never rerolls. Marker identity
owns initially living unscripted actors; clients suppress local spawning.

**Transfers and equipment.** Take All binds a witnessed source stack and both
revisions, uses stock order/stacking and corpse slot removal on detached stores,
and commits atomically. No corpse disposal or living access is implied. All 19
slots use item identities; format 7 has explicit payload mode. Accepted intents
invalidate revisions even without splitting; unavailable effects reject. Living
appearance exposes equipped records, keeping private stacks/counts server-side.
Presentation copies cannot authorize commands. Ground baselines suppress the
entire bound placement domain.

**Cross-actor effects (V51).** Absorb benefits bind the original caster life;
respawn never inherits them. Persist effects, benefits, resources and RNG together.
Dispel groups temporary spells by source/caster/life/launch, leaving enchanted
items and constants intact. T3C6/capability 24 carries authoritative paralysis;
inherited movement cannot bypass it. Existing campaigns do not silently upgrade.

**Determinism and network boundaries.** Save server order/ticks and RNG state;
measure stream consumption instead of assuming cross-platform replay. Preserve
authentication/session separation, stable identities, stale/retry rejection,
reliable/latest-state traffic, bounded queues, backpressure and secret-free evidence.
Existing direct-IP encryption does not authenticate server endpoint identity.
Protocol/save changes may be deliberate without maintaining competing authorities.
