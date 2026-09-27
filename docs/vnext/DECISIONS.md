# Durable decisions

CURRENT.md records implementation. M4 runtime decisions are approved;
script-scoping proposals remain labeled. Replace superseded rules; append no history.

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
replication. Player contact reports bind session/generation, placement, sequence
and observed tick; expire after ten ticks; and invalidate on reconnect/reversal.
They supplement server physics until movement cutover. No rewind/barrier exists;
latency may clip. Replacing reports must preserve persistence and replication.

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
null presentation cannot suppress mechanics. V22 selects an active player in
engine melee reach at full wind-up, rechecks server position at the KF hit key,
and persists selection/contact. Inherited player movement uses strict center
reach until native hulls are bound.

**Travel scheduling.** Simulate the player/traveler area union once per actor.
Bound cells/work; persist destinations, completion, inactivity and stock Travel
guards. No abstract fast-forward. V20 admission is atomic; saturation pauses both
substeps without resetting travel. Stable placement IDs suppress duplicate local
actors. Freeze offline players; exclude client velocity from legacy simulation.

**Composed ticks.** One isolated transaction owns ordered intents, simulation,
resources, effects, wear, death/loot, RNG and receipts. Persist before installation
and publication; measure overruns. WhenUsed pays at launch even on a later miss;
source identities survive item movement. V17 stages doors against committed NPC
hulls before both physics steps. Area images own angles; collision transforms
are derived. Player reports supplement sensing until movement cutover. V18
persists destination, avoidance, stuck position/direction and private RNG.

**Casting authority and recovery.** V38 retains caster kind, placement/player ID
and launch life through flight, effects and death history. Players remain life 1;
NPC respawn clears its body effects while attribution on others survives. Trusted
NPC commands never enter client input. Death cancels NPC flights. Older images
retain their layouts; new descriptor domains require fresh campaigns.

V39-V42 admit actions every eight committed ticks, selecting the nearest visible
living player with ID ties. Stock ratings compare weapons/ammunition, equipped
WhenUsed and spells; unsupported plans reject. Source/life/target, payment, RNG,
KF release/recovery, effects and concurrent player casts persist together. Inactive
areas pause; launch revalidates source/life/target/visibility. T3C5 requires capability
23; T3C2 events retain caster life. V46 projects committed swing sections/interruption;
reconnect samples without replaying gameplay keys or Lua completion callbacks.
Clients never advance the authoritative animation clock.

**Equipped passive sources.** Candidate equipment selects constant effects by item
instance and effect ordinal before combat. Rolls persist while that instance stays
equipped; replacement and respawn install new sources in the same transaction.
Recovery checks arguments, source membership and magnitude bounds without rolling.
Modifiers overlay detached combat stats and never accumulate in saved base state.

**Combat latency.** Predict local swing/cast presentation only; the server owns
gameplay consequences. Start with server-time contacts; measure latency before adding
bounded historical actor/obstacle queries. Full-world rewind is not selected. Reuse
timestamped replication, reliable action/life events and latest-wins motion.
V31 resolves projectile/player contact against active server player positions
with a 32-unit sphere at body center, choosing the earliest hit against NPC/world
collision. This bounded proxy remains until shared player hull physics is bound.

**Melee state.** Negative fatigue prevents attacks and redirects unarmed damage to
health. Stock fatigue restoration continues while active. V50 persists authored
knockout/knockdown clock: exhaustion loops, restored fatigue lets the current clip
finish its get-up tail. Shared stock health-hit rolls select physical knockdown.
New hits do not restart an active knockdown; inactive actors pause, and death/respawn
clear body clocks. Fresh campaigns are required.
V34 shares carried-left shield visibility and durable CPU hit recovery. V43 binds all three
participants' layered hit resources independently, including their bytes in the
campaign fingerprint. Recovery validates each saved timer against its own clips;
older descriptor domains retain the NPC resource for all participants.

V44 persists pre-release melee targets and clip recipes. Stock detached transactions
equip selected carried weapons; recovery never re-equips. NPC clips bind layered
groups/fallback, direction and speed. Completion resumes selection; breakage and
passive-source removal commit together.

**Movement smoothness (target).** Cut over after smooth replication and unified
engine collision. Until then validate inherited combat contacts on the server.
Start with stock physics; tune snapshot frequency separately. Interpolate remote
snapshots with bounded extrapolation, latest-wins motion and reliable events.
Predict locally with shared fixed steps; restore acknowledged physics and replay
pending input. Timestamp obstacles, separate collision correction from visual
blending, and reset on teleport, respawn or cell change. Measure jitter/loss,
correction size/frequency and overruns.

## Cooperative progression design

**Requirement:** characters, including late joiners, can complete campaigns
independently. Others cannot permanently block opportunities; one's own mutually
exclusive choices retain consequences. Support content through OpenMW APIs and
engine rules, without mandatory quest adaptations.

### Shared world, personal progression

**Direction:** NPCs share one world and return after death. Temporary unavailability
is acceptable. Personal state owns journals, choices, relationships, faction
progress, rewards and script history. NPC death does not default to private campaigns.

**Lifecycle contract (M4).** Stable placement/life generations, attributed death
events, corpse inventories and deadlines survive unload/restart. Old requests
cannot affect new lives. OpenMW supplies reconstruction data; authored corpses,
summons, scripted spawns and deleted placements are not permanent spawn points.
M4 records causal events; M5 owns personal quest credit.

**V25 deadline rule:** one initially living content placement respawns after a
descriptor-bound delay of authoritative 30 Hz ticks (capture: 27,000). Game-time
skips and downtime do not count. Restore actor/inventory together with fresh item
identities. Wider spawn policy awaits evidence.

Separate three kinds of state:

| State | Ownership and lifetime |
|---|---|
| NPC body, combat/AI, current corpse and inventory | Shared current life; reconstructed through an authoritative respawn transaction. |
| Character's credit, choices, relationship and reward history | Personal and durable; never cleared merely because the NPC returns. |
| Stable placed identity plus life generation | Server-owned; distinguishes another legitimate kill from replaying the previous death. |

Reset health/effects, AI and placement through engine behavior. Preserve personal
story locals. Define corpse cleanup/loot explicitly: no refilling looted corpses or
targeting new lives with old requests; fresh loot gets fresh identities. Renewable
loot is an economy choice; quest rewards remain one-time. Camping can still delay
access indefinitely and needs a separate fairness policy.

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

For B's personal script, a reference temporarily lost to another's combat/death is
unavailable. Health/liveness, lookup, existence and enumeration queries must not
silently expose it as a permanent missing/dead dependency. Defer the invocation
before committing anything; wake it on lifecycle change, revalidate and rerun from
the last committed state. This covers polling as well as event handlers. Physical
rendering/combat still show the real corpse. B's own attributable failures retain
normal consequences. Do not infer success/failure from numeric journal indices.

Deferral is distinct from failure: it must not disable a global/local script.
Persist the wake dependency and event identity; bound queues/retries and report
unresolved waits rather than spin. Pause affected story deadlines during enforced
unavailability/offline suspension; raw time reads must use a consistent story clock.
Persistent scripted removal uses the scoped rule above, not an endless respawn wait.
Indirect observations require coverage.

**Atomic execution:** stage globals, locals, journal, inventories, reference changes,
RNG, consumed events, timers and presentation together. A late unavailable read,
stale life, unsupported opcode, budget exhaustion or failed durability discards the
whole invocation. Receipts deduplicate retries of an invocation, not all executions
of that script; legitimate recurring events still run. Content's preserved flags
govern one-time rewards. Lua tables/closures/callback state need demonstrable isolation;
catching an exception or staging only engine calls is insufficient.

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

**Atomic coherent persistence.** Bound and validate input before allocation/mutation.
Stage script/presentation effects; rejected preparation or durability failure cannot
leak effects or success. Copying engine objects or catching exceptions is not isolation.
Legitimate missed attacks/failed casts may retain prescribed costs. Durability
precedes installation, which precedes publication. Restore the complete content/
version-bound world/player relationship off to the side. No checkpoint-only
acknowledgment, silent resets or parallel canonical files.

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
scenes; unloading freezes doors. Positions select adjacent bound exteriors;
teleports commit destination/epoch. Bounded baselines carry loot/doors/players/
equipment. Doors share inventory durability. Capability excludes older clients.
Bootstrap requires established identities and engine environment, retaining
inherited movement; unsupported scripts remain unauthorized.

**Initial leveled actors (v15).** A separate campaign-seeded OpenMW RNG stream and
loot level select NPC/creature records in stable cell/reference order. Persist
marker selections, including chance-none, with inventory/doors; recovery never
rerolls. Marker identity owns the actor; clients suppress local spawning.
Only initially living unscripted actors are supported. Fresh campaigns and the
leveled-actor capability are required.

**Transfers and equipment.** Take All binds a witnessed source stack and both
revisions, uses stock order/stacking and corpse slot removal on detached stores,
and commits all or nothing. No corpse disposal or living access is implied.
All 19 equipment slots use item identities, including ammunition; format 7 has
explicit payload mode while shirt-only 1/5/6 encodings retain meaning. Accepted
equipment intents invalidate revisions even without splitting; unavailable effects
reject. Living appearance exposes only owner and equipped record IDs; private
stacks/counts stay server-side. Presentation copies never become command identities.
Ground baselines suppress the entire bound placement domain, including absent loot.

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
