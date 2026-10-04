# Durable decisions

## Product, authority and reuse

**OpenMW gameplay is the foundation.** Reuse engine content, mechanics, world and
scripting, including TR; no independent formulas, catalogs or quest language.
Keep baseline 0.51.0; mod support requires evidence.

**Independent networking; native runtime.** Keep components/tes3mp portable.
An app-local runtime may share gameplay with stock OpenMW. Preserve dependency
checks.

**One gameplay loadout.** OpenMW resolves content. Bind plugins/scripts/settings/
resources to campaign identity. Python packages/hashes, never reinterprets ESM.
Unsupported behavior rejects.

**One server authority.** Own actors/objects/resources/time/outcomes. Clients submit
authenticated intent and present commits. Validate inherited movement; schedule areas
independently of menus, without NPC client leases.

**M3 doors:** stock activation/reversal; session/placement/sequence/tick-bound contact
expires after ten ticks or reconnect/reversal. Contacts supplement physics until cutover.

**Presentation is separate.** Clients own UI/visuals/audio. Collision/scripts bind
gameplay identity. MWScript/Lua use bounded context/semantic serialization.
Never persist pointers, render objects or sessions.

## M4 actor simulation

**Runtime ownership.** An app-local OpenMW actor runtime exposes owned commands,
snapshots, activity and identity. AI, physics, combat and loot share authoritative
inventories; preserve stock callers and dependency checks.

**Gameplay animation.** Retain CPU movement, hit keys and releases. Bind timing/
collision resources; null presentation cannot suppress mechanics. V22 persists
wind-up selection and rechecks reach at the hit key.

**Travel scheduling.** Simulate the player/traveler area union once per actor.
Persist destinations/guards. Saturation pauses substeps; offline players freeze.

**Composed ticks.** Stage intents, simulation, resources, effects, wear, death/
loot, RNG and receipts; persist before publication. WhenUsed pays at launch;
source identity survives movement. Door contact precedes physics. Persist avoidance.

**Committed ground collision.** Inventory owns durable ground references. Detached
membership/transforms drive shared physics, projectile/visibility queries and navigation
before simulation. Durability swaps scenes without allocation. Recovery rebuilds from
validated inventory, preserving actor frames; world changes replan travel. Animated
ground hulls reject.

**Casting authority.** Retain caster kind/ID/life through flight, effects and
death. NPC respawn clears body effects; attribution survives. NPC commands never
enter client input. Death cancels flights.

Native player selection orders distance then ID; stock ratings gate plans.
Persist sources, lives, targets, payment, RNG, KF phases and effects together;
inactive areas pause, launch revalidates.

**Equipped passive sources.** Select constants by item and effect ordinal before
combat. Rolls persist while equipped; replacement and respawn install new sources.
Recovery validates them without rolling. Modifiers overlay detached stats.

**Connected consumers.** Stock awareness receives canonical sneak/facing context.
Life-bound jump controls, inertia and falling persist in movement scene image 3;
landing damage retains the grounded frame. Bind resources to fresh campaigns.
Abilities/constants share ordered preparation and retained rolls; suppressing
Invisibility preserves other ordinals. General scripts remain M5.

**Combat latency/presentation.** Predict presentation; server commits consequences.
Use server-time contacts, reliable action/life events and latest-wins motion.
Stock damage/knockout/recovery rules and actor-bound clips/targets/clocks persist.
Inactivity pauses; death clears. Recovery never repeats equipment; breakage removes
passives. Player release/payment/interruption commit atomically; offline casts pause.

**V64 aim.** T3MC bounds world aim; persist flight direction, authored-key payment
and swept contact. Misses use target life zero; receipts retain skipped-flight endpoints.
Reconnect suppresses seen cues; fresh campaign required.

T3C9/capability 27 feeds stock Light/NightEye/Detect; snapshots restore loops/expiry.
Actions suppress Invisibility without deleting sources/rerolling. V56/T3D0/capability 28
feeds eight inherited movement magnitudes with server-owned source/caster/deadline.
WaterBreathing index zero differs from legacy ResistMagicka by source identity.
M4 collision/combat acceptance precedes movement cutover.

**AI source navigation.** Stock target rules gate disposition per record. Command
pursues its living, life-matched caster. Expiry restores authored travel atomically;
rejection preserves path/position. Dialogue retains its owning context.

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

**V25 deadline rule:** descriptor-bound 30 Hz deadlines exclude downtime/time
skips. Initially living content placements restore actor/inventory with fresh item
identities. Capture delay: 27,000 ticks. Wider spawn policy awaits evidence.

| State | Ownership and lifetime |
|---|---|
| NPC body, combat/AI, corpse and inventory | Shared life; authoritative respawn. |
| Character credit, choices, relationships and rewards | Personal; survives NPC revival. |
| Placement identity and life generation | Server-owned; separates new deaths from replays. |

Reset health/effects, AI and placement through [stock respawn](../../apps/openmw/mwclass/npc.cpp);
preserve story locals. Never refill looted corpses or apply stale-life requests.
Renewable loot/camping need policies. [Death counting](../../apps/openmw/mwmechanics/actors.cpp)
aggregates record IDs; personal participation must distinguish party, summon,
environmental and pre-quest credit across revival.

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

Simulate once; scope scripts/spawns per character. Locals/cursors key by character,
script and stable reference, surviving respawn; reset lifecycle fields separately.
Scripted combat shares the action path; API coverage establishes mod support.

**Death/unavailability proposal:** replace shared OnDeath with character/script
cursors and attributed history, including pre-quest events. Credit survives revival
for initiators/eligible helpers; define summon/environmental causes. Others receive
no narrative death. Another's combat/death removes references from personal
liveness/lookup/enumeration while combat shows corpses. Defer without committing;
revalidate on lifecycle change. Persist bounded wake dependencies and pause affected
story deadlines while unavailable/offline. Never disable scripts; scoped removal
does not wait for respawn.

**Atomic execution:** stage globals/locals, journal, inventories/references, RNG,
events/timers and presentation together. Unavailable reads, stale lives, unsupported
opcodes, exhausted budgets or failed durability discard invocations. Deduplicate
retries; preserve recurrence/one-time rewards. Isolate Lua tables/closures/callbacks.

**Owning seams:** [interpreter context](../../apps/openmw/mwscript/interpretercontext.cpp),
[dialogue operations](../../apps/openmw/mwscript/dialogueextensions.cpp),
[actor queries/events](../../apps/openmw/mwscript/statsextensions.cpp),
[world operations](../../apps/openmw/mwscript/miscextensions.cpp) and
[script scheduling](../../apps/openmw/mwscript/scriptmanagerimp.cpp).
Environment/Lua access respects context; preserve engine execution/serialization.

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

**Native inventory cutover.** Session image/dispositions share one transaction.
One inventory intent composes with actor tick/wear; later intents reject in ingress
order. Fingerprints bind roles, placements/domain; IDs use record-plugin order.
Recovery preserves identity/condition/light-time/charge, never rerolls or auto-equips.

**Versioned domains.** Retain prior [layouts](../../apps/tes3mp-server/native/inventory_host.hpp).
Descriptor changes require fresh campaigns or explicit migration; recovery never
resets, rerolls loot or auto-equips.

**Time/weather (v12+).** Stock calendar/REGN/fallbacks advance ≤4,096 regions at
30 Hz independently of occupancy/menus. Bind RNG/clocks/transitions to content/settings/seed;
no wall-clock catch-up or client/legacy-script writers.

**Streaming (v14).** Bind 1–256 cells; retain canonical state and occupied interior/
3×3 exterior scenes. Unloaded doors freeze; teleports commit destination/epoch.
Baselines carry loot/doors/players/gear. Retain inherited movement and supported scripts.

**Leveled actors (v15).** Campaign-seeded stock RNG selects in placement order;
persist with inventory/doors, never reroll. Marker identity owns unscripted actors;
clients suppress spawning.

**Transfers/equipment.** Take All witnesses a source stack/both revisions and uses
stock order/stacking/corpse slot removal atomically on detached stores; it grants
no corpse disposal/living access. All 19 slots use identities; format 7 declares
payload mode. Accepted intents invalidate revisions; unavailable effects reject.
Appearance exposes equipped records, never private stacks/counts or authorization.
Ground baselines suppress the entire bound placement domain.

**Cross-actor effects (V51).** Absorb benefits bind the original caster life;
respawn never inherits them. Persist effects, benefits, resources and RNG together.
Dispel groups temporary spells by source/caster/life/launch, leaving enchanted
items and constants intact. T3C6/capability 24 carries authoritative paralysis;
inherited movement cannot bypass it. Existing campaigns do not silently upgrade.

**Detached movement (V56).** Stock rules/physics commit NPC water, inventory,
stats/effects. Inherited player movement awaits M4 cutover. The death schema uses
the NPC's life for drowning; generalization must distinguish environmental causes.

**V65 movement rules.** Stock EnableLevitation/DisableLevitation opcodes run through
a bounded trusted script request. Their world rule and effect removal commit in
the actor image; recovery validates sources. General script scheduling remains M5.
Bound neighbors use
their own movement stats/effects in the shared solver and own drowning damage.

**Object state.** Touch Lock/Open shares the streamed door/actor commit.
Normalize unlocked durable FLTV to zero; retain other authored bytes. Door contact
identifies durable angle, motion and lock independently of presentation counters;
revalidate before payment. Container lock/contact revisions remain separate from
stack revisions. Committed locks gate activation/take/put and observer baselines.

**AI decisions (V57).** Persist Flee target/deadline/destination with combat/RNG.
Bounded passive abilities/constants retain durable rolls. T3D1/capability 29 feeds
committed Charm into stock dialogue disposition.

**Player AI context (V58).** Persist bounded factions, crime/bounty, draw/werewolf
state and spell/item selection. Recovery validates content IDs/inventory; only
trusted updates write them. Share stock aggression with server authority.

**Social lifecycle (V59-V66).** Commit werewolf equipment/effects/stats, crime and
witness engagement together. At most 128 unscripted, unleveled witnesses share
collision; placement-bound bodies, resources, combat/lives and social targeting
recover without migration. Command requires an available living caster with
matching source life; older caps persist.

**Player travel (V67/V68).** Each player owns a durable Mark. Recall commits
payment/destination/epoch; rejection retries, storage failure closes until coherent
restart. V68 persists bounded Enable/DisableTeleporting. Disabled/unmarked casts
pay without teleporting.

**Object/travel (V69).** Explicit-player
stock intervention search caps interiors at 256 cells and rejects unbound
destinations. Shared reach/Lock/Open rules, collision identity and ordered effects
join the existing commit. Stock gem selection captures once per new creature
death with matching caster life; gem/death/revision/payment persist together.
T3D2/capability 30 feeds Telekinesis into stock focus; the server owns reach/access/sight.

**Equipment ownership (V70).** Shared stock
bound/ExtraSpell handlers own temporary identities and acyclic restoration links;
ordinary items remain independent. Forbid temporary stacking/transfers; splice
expired links; manual changes win. Effects/constants, wear, draw and counters join
the actor commit. Rejection installs nothing; failed permanent attempts remain
dormant until source changes. Recovery validates ownership. General scripts remain M5.

**Dynamic actors (V71).** Namespace 3 shares the inventory counter. Stage collision/
inventory membership before durability; owner/source/life forms an acyclic forest.
Remove descendants without respawn. Recovery validates sources/selectors/identities/lives/
animation hashes. Reuse stock writers; scripts remain M5. Collision follows committed
active areas. Wait/rest composes effects/resources/cleanup with game time, preserving action/respawn ticks.

**Player lives (V72).** Wrap V71. Persist ID/generation/birth/spawn and attributed deaths;
zero killer is environmental. Death clears body/sources/actions; external effects retain
attribution. Command/Absorb/Soultrap require living matching casters. Online respawn
restores configured stats/spawn, preserves ordinary inventory/social/Mark, increments
generation. Cleanup/RNG/membership/relocation/zero velocity/epoch commit together.
Recovery validates history/baselines; old-life timestamps reject. Capability 32 gates lives;
37 admits a dead-life respawn request through that same transaction before the automatic
deadline. UI waits for committed revival. Quest credit/wider spawn policy remain M5.

**Committed bodies (V73).** Persist stock death action/group/frame and RNG. Sample
active clocks without client death cleanup; capability 33 gates recipes. Publish
capability-30 aliases once. Projectile WhenStrikes application and consumption
remain in the inventory transaction.

**Spell sources (V74).** Persist canonical 24-hour power deadlines. Reuse stock
sources/launch/Dispel rules. Travel composes recipient/ordinal relocations before
publication; ignored entries preserve other ordinals. Scripts/spellbooks remain M5.

**Passive Self (V75).** Bound witnesses/Corprus clocks; retain stock resistance,
base-stat rules/no-ops. Cures quench ordinals; removal undoes harmful worsening,
preserving benefits. Dormant actions wait; recovery never replays. Commit metadata/mutations together.

**Target recipients (V76).** Zero-recipient Self launches by release facing;
persist direction/caster life/time. Reuse stock effect transactions; reject recipients
born after release, retain named lives/object revisions. Discover intervention cells
before manifest binding; budget exhaustion rejects. Capability 35 admits ordinals
beyond seven; older peers omit those transient applications.

**Spell presentation (capability 34).** Publish committed windup, failed school and
ordinal results with caster/target lives. Reuse stock cast/hit/protection/object/area
helpers; snapshots own continuous loops/cleanup. Admit bounded cue chunks atomically;
gameplay events occur once. Reconnect restores loops without transient replay.
Capability 33 retains legacy cues; presentation never enters saves.

**Shared cell combat (V77).** Connected exteriors share physics/navigation;
interiors/disconnected regions stay separate. Merge domains before allocation.
Startup bounds: 256 cells, 8,192 references/region; navigation follows actors within
256 cached tiles. Automatic discovery/geometry streaming remains pending.
One campaign transaction simulates occupancy once; empty-cell clocks freeze.
Retain flight collision; unload without demand. Persist flight cells/static cast clocks/dynamic spawn cells.
Unsupported script/leveled/corpse services reject at startup; full AI packages remain M5.

**Determinism and network boundaries.** Save order/ticks and RNG;
measure consumption across platforms. Preserve session authentication,
identities, stale/retry rejection, bounded traffic and secret-free evidence.
Direct-IP encryption lacks server identity authentication.

**Native chargen (V78).** Wrap V77 with two bounded profiles/resource signatures.
Own bodies/custom classes; preserve winning content. Actor/inventory transactions
install items, stock stats/spells/resources after durability. Fresh players cannot simulate.
Recovery rederives bodies/baselines and validates profiles/resources. Bootstrap uses
empty inventory/one safe point. Capability 36 carries established bodies with cell-filtered gear;
fresh profiles have none. Replicas own NPC copies through gear/cell rebuilds,
preserving winning records and private profiles.
V78 requires capabilities 6 and 36; older peers reject during negotiation.

**Character installation:** committed profile revisions bind coherent installation;
newer motion cannot raise inventory floors. Delayed lanes resync; public equipment
presents independently of private delivery.
