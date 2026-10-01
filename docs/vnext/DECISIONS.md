# Durable decisions

CURRENT.md records implementation; script-scoping proposals remain labeled.

## Product, authority and reuse

**OpenMW gameplay is the foundation.** Reuse engine content, mechanics, world and
scripting, including TR; no independent formulas, catalogs or quest language.
Keep baseline 0.51.0; mod support requires evidence.

**Independent networking; native runtime.** Keep components/tes3mp portable.
An app-local runtime may share gameplay with stock OpenMW. Preserve dependency
checks.

**One gameplay loadout.** OpenMW resolves content. Bind plugins,
scripts, settings and resources to campaign identity. Python packages and hashes,
never reinterprets ESM. Unsupported behavior rejects.

**One server authority.** The server owns actors, objects, resources, time and
outcomes. Clients submit authenticated intent and present commits. Validate
inherited combat movement. Schedule player areas independently of menus; NPCs
have no client leases.

**M3 doors:** shared OpenMW rules own activation and reversal. Contact reports
bind session, placement, sequence and tick; expire after ten ticks and invalidate
on reconnect/reversal. They supplement physics until movement cutover.

**Presentation is separate.** Clients own UI, visuals and audio. Collision and
script resources enter gameplay identity. MWScript/Lua use bounded context and
semantic serialization. Never persist pointers, render objects or sessions.

## M4 actor simulation

**Runtime ownership.** An app-local OpenMW actor runtime exposes owned commands,
snapshots, activity and identity. AI, physics, combat and loot share authoritative
inventories. Preserve stock callers/dependency checks; avoid World/UI wrappers.

**Gameplay animation.** Retain CPU movement, hit keys and releases. Bind timing/
collision resources; null presentation cannot suppress mechanics. V22 persists
wind-up selection and rechecks reach at the hit key.

**Travel scheduling.** Simulate the player/traveler area union once per actor.
Persist destinations/guards. Saturation pauses substeps; offline players freeze.

**Composed ticks.** Stage intents, simulation, resources, effects, wear, death/
loot, RNG and receipts; persist before publication. WhenUsed pays at launch;
source identity survives movement. Door contact precedes physics. Persist avoidance.

**Casting authority.** Retain caster kind/ID/life through flight, effects and
death. NPC respawn clears body effects; attribution survives. NPC commands never
enter client input. Death cancels flights.

V39-V42 select the nearest visible living player every eight ticks, breaking ties
by ID. Stock weapon/ammunition/WhenUsed/spell ratings reject unsupported plans.
Persist source/life/target, payment, RNG, KF phases, effects and casts together;
inactive areas pause and launch revalidates. T3C5/capability 23 and T3C2 retain
caster life. V46 projects swings without gameplay callbacks or clock advancement.

**Equipped passive sources.** Select constants by item and effect ordinal before
combat. Rolls persist while equipped; replacement and respawn install new sources.
Recovery validates them without rolling. Modifiers overlay detached stats.

**Combat latency.** Predict swing/cast presentation; the server owns consequences.
Use server-time contacts pending measurements. Replicate reliable action/life
events and latest-wins motion. V31 uses a 32-unit sphere until shared hulls bind.

**Melee state.** Negative fatigue prevents attacks and routes unarmed damage to
health. V50 persists knockout/knockdown clocks and finishes get-up clips. Stock
health-hit rolls select knockdown; hits cannot restart it. Inactivity pauses and
death clears body clocks. V34 shares shield visibility/recovery; V43 fingerprints
hit clips and validates timers. Fresh campaigns are required.

V44 persists pre-release targets/clip recipes. Detached transactions equip weapons;
recovery never re-equips. Completion resumes selection; breakage removes passives.

**V52 actor presentation.** Clients sample committed motion/action without gameplay
callbacks. Persist clips/life; hold on pauses. T3C7/capability 25; fresh campaigns.

V53 adds participant-bound player casts to this clock. Release revalidation/payment
and interruption commit atomically; offline casts pause. T3C8/capability 26;
fresh campaigns.

**V64 physical aim.** T3MC carries bounded world aim. The server saves flight
direction, charges at the authored key and sweeps contact. World misses use target
life zero. Terminal receipts show skipped-flight endpoints; reconnect suppresses
seen cues. Fresh campaign required.

T3C9/capability 27 carries bounded aggregate visibility magnitudes with actor
presentation. Clients feed stock Light, NightEye and Detect consumers; snapshots
restore loops and expiry after reconnect. An action suppresses an equipped
Invisibility effect without deleting its durable source or rerolling magnitude.

V56/T3D0/capability 28 carries eight movement magnitudes consumed by inherited
OpenMW player movement. The server owns source, caster and deadline.
WaterBreathing's index zero differs from legacy ResistMagicka by source identity.
Movement cutover still requires M4 collision/combat acceptance.

**AI source navigation.** Stock target rules gate disposition effects per actor
record. Command stages pursuit of its living player or bound actor caster only
while caster life matches the source. Expiry restores authored travel in the same
tick; rejected writes retain path and position. Combat aggression, flee routes
and dialogue still need owning consumers.

**Movement smoothness (target).** After unified collision, use stock physics,
interpolation and bounded extrapolation. Restore acknowledged physics and replay
input. Reset on teleport, respawn or cell change; measure jitter.

## Cooperative progression design

**Requirement:** late joiners can complete campaigns independently. Others cannot
permanently block opportunities; personal choices retain consequences. Use OpenMW
APIs without mandatory quest edits.

### Shared world, personal progression

**Direction:** NPCs share one world and return after death. Temporary unavailability
is acceptable. Journals, relationships, faction progress, rewards and script
history are personal.

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
fairness need policies.

[Stock NPC respawn](../../apps/openmw/mwclass/npc.cpp) restores actor data/placement;
V25 uses a durable deadline. [Death counting](../../apps/openmw/mwmechanics/actors.cpp)
aggregates by record ID; quests need personal participation history across revival.
Define party, summon, environmental and pre-quest credit generically.

### Scripted quest execution contract

**Proposed engine fix:** run supported scripts with character, story scope,
source reference/life and causal event identity. Background scripts, timers and
Lua callbacks retain context. Stock single-player uses a default context. Scope
engine operations, never quest names.

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

**Death and temporary unavailability:** replace shared OnDeath with character/script
cursors and attributed history, including pre-quest events. Proposed credit covers
the initiator and eligible helpers; define summon/environmental causes. Credit
survives revival; uninvolved characters receive no narrative death event.

Another's combat/death temporarily removes a reference from personal scripts'
liveness, lookup and enumeration. Defer handlers without committing; revalidate
on lifecycle change. Combat still shows the corpse.

Deferral cannot disable a script. Persist bounded wake dependencies; pause affected
story deadlines while unavailable/offline. Scoped removal is not a respawn wait.

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

M5 must prove these contracts. Shared loot needs renewable access/transfer rules.
Preserve v8 until explicit migration.

## Integrity and migration

**Atomic coherent persistence.** Validate before allocation/mutation. Stage gameplay,
scripts and presentation; rejection leaks nothing. Misses/failed casts retain
prescribed costs. Persist before publication; restore content/version-bound state
atomically. No checkpoint-only acknowledgments or parallel canonical files.

**Borrowed lifetime.** Ptr copies retain weak destruction witnesses. Construction
starts a new lifetime; assignment preserves the destination. Check witnesses
before dereferencing, then registry/script ownership. Addresses/serialized checks
cannot establish lifetime or authorize installation.

**Native inventory cutover.** Session image and dispositions share one file
transaction. One inventory intent composes with actor tick/hit wear; later intents
reject in ingress order. Fingerprints bind roles, placements and domain.
Reference IDs use record-plugin order. Recovery never rerolls or auto-equips;
identities preserve condition/light-time/charge bits.

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

**Initial leveled actors (v15).** A campaign-seeded OpenMW RNG stream selects
records in placement order. Persist selections with inventory/doors; recovery
never rerolls. Marker identity owns unscripted actors; clients suppress spawning.

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

**Detached movement (V56).** Commit NPC movement from content water, inventory,
stats and effect sources through shared OpenMW rules and the stock physics solver.
Keep inherited player movement until the M4 collision/combat cutover. The current
death schema requires an actor life, so drowning records the NPC's own life as
the environmental cause; distinguish environment explicitly when death ownership
is generalized.

**V65 movement rules.** Stock EnableLevitation/DisableLevitation opcodes run through
a bounded trusted script request. Their world rule and effect removal commit in
the actor image; recovery validates the rule before installing sources. V65 needs
a fresh campaign. General script scheduling remains M5 work. Bound neighbors use
their own movement stats/effects in the shared solver and own drowning damage.

**Initial object magic.** Trusted and player Touch Lock/Open use the streamed
ordinary-door image inside the actor commit. Stock ESM saves FLTV only while
locked; Open stores zero because negative transient levels have no durable
meaning. Recovery accepts bounded FLTV and fixes all other authored bytes.
Spell contact uses an exact identity of the durable door angle, motion state
and lock, independent of the transient presentation motion counter; revalidate
at release before payment. Baselines project the committed lock to clients.
V56c binds one ordinary unkeyed container. Its bounded lock level and contact
revision persist in the area image beside doors; inventory stack revision stays
separate. A direct take/put checks the committed lock, while Touch casting
revalidates contact before cost. Container baselines carry both lock fields
through chunk assembly so stock activation follows the server state.

**AI decisions (V57).** Persist the selected actor's Flee target, deadline and
navigation destination with combat and RNG. Reconcile bounded AI-only passive
abilities and equipped constants as durable sources; never reroll them on recovery.
T3D1/capability 29 adds Charm to actor presentation so stock dialogue disposition
consumes committed magnitude. Earlier campaign and wire versions retain their
layouts; fresh V57 campaigns are required.

**Player AI context (V58).** Store bounded player faction membership, crime,
bounty, draw state, werewolf flags and spell/item selection in the actor campaign;
validate faction/spell IDs against bound content and item selection against the
committed inventory on recovery. Only trusted state updates may write these
fields. Share stock numeric aggression terms while keeping the server the sole
gameplay writer. V57 remains readable under its prior descriptor; V58 requires
a fresh campaign rather than implicit migration.

**Social lifecycle (V59–V66).** Commit werewolf equipment, effects, stats, crime
and witness engagement together. Bind at most 128 unscripted, unleveled
witnesses. Neighbor bodies, paths and pursuit share one collision world.
Engagement keys by placement/player; no implicit migration. V61 binds
stats/body/hit resources by placement on recovery. V62 gives two neighbors
combat/lives; V63 gives three placement/life-bound spells/projectiles; V66 gives
four with shared social targeting. Player Command follows an available, living
caster; older caps persist.

**Player travel (V67/V68).** Each player owns one durable Mark. Recall commits
payment, bound destination and authority epoch together. Rejection permits retry;
storage failure closes the runtime until coherent restart. V68 persists stock
Enable/DisableTeleporting through bounded opcodes. Disabled spells and unmarked
Recall retain payment without teleporting. Each version requires a fresh campaign;
V67's layout remains unchanged.

**Determinism and network boundaries.** Save order/ticks and RNG;
measure consumption across platforms. Preserve session authentication,
identities, stale/retry rejection, bounded traffic and secret-free evidence.
Direct-IP encryption lacks server identity authentication.
