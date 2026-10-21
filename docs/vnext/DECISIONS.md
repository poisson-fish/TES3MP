# Durable decisions

## Product, authority and reuse

**OpenMW gameplay is the foundation.** Reuse engine content, mechanics, world and
scripting, including TR; no independent formulas, catalogs or quest language.
Keep baseline 0.51.0; mod support requires evidence.

**Independent networking; native runtime.** Portable components/tes3mp;
app-local OpenMW runtime. Preserve dependency checks.

**One gameplay loadout.** OpenMW resolves content. Bind plugins/scripts/settings/
resources to campaign identity. Python packages/hashes, never reinterprets ESM.
Unsupported behavior rejects.

**One server authority.** Own actors/objects/resources/time/outcomes. Clients submit
authenticated intent and present commits. Schedule independently of menus; no NPC leases.

**M3 doors:** stock activation/reversal; authenticated placement contact expires
after ten ticks or reconnect/reversal, pending physics cutover.

**Presentation is separate.** Clients own UI/visuals/audio. Bind gameplay identity
and bounded script contexts; never persist pointers, rendering or sessions.

## M4 actor simulation

**Runtime/ticks.** App-local OpenMW owns AI/physics/combat/loot; preserve stock
callers/boundaries. Stage intents/resources/effects/wear/deaths/loot/RNG/receipts;
persist before publication. WhenUsed pays at launch; source identity survives
movement. Door contact precedes physics; avoidance persists. Simulate the
player/traveler area union once per actor; persist destinations/guards.
Saturation pauses substeps; offline players freeze.

**Gameplay animation.** CPU movement/hit/release/collision survives null presentation.
V22 persists wind-up selection and rechecks hit-key reach.

**Ground collision.** Durable inventory membership/transforms drive physics,
projectile/visibility/navigation before simulation. Durability swaps scenes without
allocation. Recovery preserves actor frames; world changes replan travel. Animated
ground hulls reject.

**Casting.** Retain caster kind/ID/life through flight/effects/death; respawn clears
body effects, not attribution. Death cancels flights; NPC commands never enter
client input. Selection orders distance then ID; stock ratings gate plans.
Sources/lives/targets/payment/RNG/KF/effects persist together; inactive areas pause,
launch revalidates.

**Passives.** Select constants by item/effect ordinal before combat; retain equipped
rolls, replace sources on replacement/respawn. Recovery never rerolls. Modifiers
overlay detached stats.

**Consumers.** Stock awareness uses canonical sneak/facing. Movement image 3 retains
life-bound jump/inertia/falling and grounded landing-damage frames; fresh campaigns
bind resources. Abilities/constants share ordered preparation/rolls; suppressing
Invisibility preserves other ordinals.

**Presentation.** Predict visuals; server-time contacts, reliable action/life events
and latest-wins motion govern outcomes. Persist stock damage/knockout/recovery,
actor-bound clips/targets/clocks; inactivity pauses, death clears. Recovery never
reequips; breakage removes passives. Release/payment/interruption commit atomically;
offline casts pause.

**V64 aim.** T3MC bounds world aim; persist flight direction, authored-key payment
and swept contact. Misses use target life zero; receipts retain skipped-flight endpoints.
Reconnect suppresses seen cues; fresh campaign required.

T3C9/capability 27 feeds stock Light/NightEye/Detect; snapshots restore loops/expiry.
Actions suppress Invisibility without deleting sources/rerolling. V56/T3D0/capability 28
feeds eight inherited movement magnitudes with server-owned source/caster/deadline.
WaterBreathing index zero differs from legacy ResistMagicka by source identity.

**AI source navigation.** Stock target rules gate disposition per record. Command
pursues its living, life-matched caster. Expiry restores authored travel atomically;
rejection preserves path/position. Dialogue retains its owning context.

**V79 player physics.** Capability 38 admits independently sequenced latest-wins
controls/acknowledgements; inventory/combat remain reliable. Two stock 60 Hz steps
per 30 Hz commit; bursts buy no simulation. Inputs expire after 12 ticks.
Recovery uses completed world ticks. Hulls/frames transact with combat/resources;
authentication stays ephemeral. Clients restore frames/replay bounded input through
stock physics. Inert peer/NPC hulls also constrain live prediction. Blend 67ms;
snap at 256 units/epoch changes. Older campaigns retain inherited movement;
movement skills await M5. At 100ms ±25ms/10% loss, corrections allow max 35/mean 8
units with one fixed-step tolerance; exclude discontinuities.

## M5 implementation contract

Others may temporarily interrupt opportunities, never permanently consume them.
Personal choices retain consequences. Unchanged scripts use proven OpenMW APIs;
quest edits/bespoke handlers are not required. [PLAN](PLAN.md#m5---complete-scripted-quests-independently-and-together)
owns sequencing; parties are a later addition.

### Ownership and physical scope

| Scope | Canonical state |
|---|---|
| Shared world | Ordinary exploration, NPC lives/combat, public loot, time/weather. |
| Character/story | Journals/topics/choices, relationships/factions, script globals/locals/instances/cursors, acquisition rights and rewards. |
| Story encounter | Incompatible physical outcomes; actors/inventories/AI, doors/geometry, collision/navigation, projectiles/summons and scripts. |

World globals/scripts need explicit ownership. Every physical instance has one
server writer. Characters inhabit one physical scope at a time; clients present
commits and never simulate canonical scripts.

Before a story's removal, relocation, spawn, inventory/AI edit or access change
would deny others an opportunity, allocate a coherent scope. Discovering this
mid-invocation rolls back and retries after preparation. Fork from validated
committed state and proven source reconstruction; never rewind live public combat.
Persist A's escorted NPC at its destination while B retains B's starting opportunity.
Never teleport A's outcome home or merge scoped mutations into public state.

Scope includes every interacting dependency across affected cells, including
exterior escorts. Lookups, enumeration, dialogue, collision and combat resolve
the same scope; no cross-scope hits, transfers or invisible blockers. Expand before
crossing; saturation suspends safely. Scope creation/unload/rejoin never renews
loot. Return players to shared exploration at a safe boundary; retain consequences.
Show encounter ownership, separation and helper status.
Player-supplied inventory retains original identities; forks never copy it.

### Scripted quest execution contract

Carry campaign/story/physical-scope, character, source reference/life, invocation
and causal-event identities through calls, callbacks, dialogue and timers. Personal
access without context rejects; no fallback to the single-player protagonist.
Run shared scripts once per scheduled instance, independent of observer count.

Results are **committed**, **deferred**, or **rejected**. Stage locals/globals,
inventories/effects, RNG, events/cursors, timers and presentation together; discard
everything on defer/rejection. Persist before publication. A retry consumes neither
costs nor events twice; distinct legitimate occurrences remain executable. Prove
Lua storage/state/effect rollback before admitting bindings/events/timers.

Discover dependencies during execution; persist wake conditions and revalidate.
Distinguish temporary unavailability from absence caused by this story's own
outcome. Never deactivate waiting scripts or return invented health/existence.
Pause affected story deadlines during system waits; shared time continues.
Unsupported clock arithmetic rejects rather than silently extending all timers.
Show wait reasons and authoritative respawn estimates only when known.

Declare numeric instruction/allocation, dependency, scope/cell/actor, queue,
participant and diagnostic limits before admission. Unsupported operations,
ambiguous origins or unbounded physical dependencies reject atomically. Report
script/instruction, scope/reference/life, reads/effects and outcome. Compatibility
is evidenced API coverage, not a promise to infer arbitrary author intent.

### Acquisition, rewards and death

Public loot remains shared. Finite quest resources need evidenced routes and durable
rights keyed by character/story and source origin/occurrence, not item name or absence.
Loose/container quest items mint per-player rights on pickup/take while staying
available for others. Provenance locks quest items against drop and container/player
transfer while ordinary items stay droppable; scripted `RemoveItem` remains available.
Sale/consumption/surrender spends the right; restart, parties and scopes never refresh
it. Ambiguous routes and GetItemCount zero reject automatic grants.

Preserve provenance through stack/split and conversions; unsupported conversions
reject. Copied encounter loot creates no extra public items; committed scripted
rewards may leave. Persist depletion and reward receipts; scene recreation never
repeats content.

M4 placement/life/death/corpse/deadline identity remains authoritative. Stock
respawn restores initially living placements, preserving story locals; deadlines
exclude downtime/time skips (V25 capture: 27,000 ticks at 30 Hz). Authored corpses,
summons, deleted placements and scripted spawns are not public respawn sources.
Never refill looted corpses or apply old-life requests.

Personal OnDeath cursors consume attributed history only on commit. Own failures
persist; foreign deaths wait. Credit requires causal participation or explicit
encounter enrollment, never proximity; preserve summon ownership, environmental
attribution and eligible pre-quest events. Help-only events do not advance the
helper's story. Reserve a waiting story's next eligible opportunity and isolate
it if necessary, preventing endless obstruction by repeated foreign kills.

### Activities and party addition

Reservations serialize short conflicting operations with fair bounded acquisition
and deadlock prevention. Long activities belong to durable scopes, not global
locks. Package completion is not quest completion. Solo owner disconnect suspends
coherently; helpers may leave. Resume never resets progress, costs or rewards.

**Party addition (V96):** persistent opt-in membership coordinates compatible personal
stories in one encounter without merging histories. Validate physical
state, choices and prerequisites before enrollment; incompatible players catch up
independently or join as helpers. Enrollment delegates shared choices to a selected controller; dissenters withdraw
or split safely before commitment. Helpers use real health/gear/resources and
receive no story advancement/reward. Party size respects runtime limits; broader scale remains M7.

Freeze participating characters/stories and membership revision for each causal
step. Execute its shared physical outcome once; evaluate personal conditions,
costs, progression and reward effects separately for every eligible participant.
Shared objectives happen once; personal acquisition rights/costs remain per-character,
and enrolled party members share kill XP.
Each eligible player receives their own stock-context reward, never a divided
pool or a copy of the leader's entire result. Differing personal outcomes may
coexist; incompatible physical writes split/defer before commitment. Prove effect
ownership; do not guess that identical AddItem calls are all rewards.

Commit the shared outcome, all admitted personal results and per-character reward
receipts atomically. Receipt identity includes causal occurrence/effect/recipient;
changing party or instance cannot repay it. Late enrollment grants no past rewards.
Disconnect preserves earned entitlements but suspends enrollment in future steps;
catch-up requires compatibility. Remaining participants may select a controller
at a safe boundary; otherwise suspend. Offline rewards already earned remain
durable. Leave/kick/dissolve preserves each character's committed consequences,
pending rewards and independent continuation without duplicating items/scenes.

### Recovery and proof obligations

Persist scopes/source origins, lives, acquisition/depletion, invocations/waits,
reservations, clocks/RNG, script state, party/enrollment/controller revisions and
reward receipts coherently with gameplay. Bind content/policy/runtime versions;
exclude pointers/sessions. Recovery stages and validates before installation;
explicit migration or fresh campaigns, never silent reset. M6 generalizes this;
M5 requires recovery from its first slice. Preserve engine serialization/v8 until
explicit migration.

Prove arbitrary-name escort/departure/divergence, foreign-held versus voluntarily
spent items, indirect/polling dependencies, pre-quest/helper/stale-life credit,
repeated foreign deaths, clock waits, contention, exterior scope isolation and
failed writes after local/item mutations. Party checks cover separate rewards,
personal costs, incompatible choices, late join, offline recipients, leader loss,
leave/kick/rejoin and retry/restart without extra rewards or public loot. Then
prove unchanged Seyda Neen branches and a TR quest: A finishes, B completes
without A/items; compatible party members share events and each receive rewards.

Refactor owning [context](../../apps/openmw/mwscript/interpretercontext.cpp),
[dialogue](../../apps/openmw/mwscript/dialogueextensions.cpp),
[filters](../../apps/openmw/mwdialogue/filter.cpp),
[references](../../apps/openmw/mwscript/ref.cpp),
[queries](../../apps/openmw/mwscript/statsextensions.cpp),
[items](../../apps/openmw/mwscript/containerextensions.cpp),
[world operations](../../apps/openmw/mwscript/miscextensions.cpp) and
[scheduler](../../apps/openmw/mwscript/scriptmanagerimp.cpp). Preserve stock callers,
native authority and engine-independent networking; retire replaced writers only
after focused evidence.

## Integrity and migration

**Scoped dialogue (V84).** Explicit content and staged stock filters/results commit
Quest/Topic, banks and contextual receipts with gameplay. Bind bounded winning
DIAL/INFO; revalidate speaker scope/life/script. Unstaged services and Lua/UI callbacks reject.

**Authenticated dialogue (V85).** Banks/revisions/choices bind living unscripted
source/life; two intents within sixteen invocations. Detached stock stats/resources
feed filters; stat writes reject. Substitutions use authenticated characters/speakers,
staged social state/globals and authoritative cells. Disposition thresholds 0–100;
source-only ModDisposition owns ≤64 private placement bases. Records/lives validate;
NPC/banks/progression/receipts/gameplay fail together. Journal text/calendar freezes
at invocation; notices share durability. Payload v2 rejects prior frames/images.
Early baselines and other-story choices install silently, without expansion/restamping/replay.
V85 scene edits stay closed.

**Terminal encounters (V86–88).** One dry interior, living unscripted NPC and static
geometry. Stock source Disable discovery rolls back/retries the same occurrence,
charging every attempt. Scope/origins join gameplay without new loot/lives.
Lookup/presentation/Bullet/combat agree; failed queries restore masks; terminal banks
freeze. V87 returns grounded bodies beyond quiescent NPC/peer hulls, retaining outcomes.
V88 fixed original-life Enable (≤16 instructions/one call) commits restoration/occupancy.
Fresh images/capability 41; silent recovery; general restoration stays closed.

**Source relocation (V89).** Stock source-only Position/PositionCell/GetPos/GetAngle
share [bounded staged state](DEVELOPMENT.md). Inert pose/origin/banks/receipts commit
together. Validate overlap before durability and geometry before recovery; borrowed
transforms restore on failure. Occupied scopes, fresh images/capability 42, silent
recovery; no mixed removal/relocation.

**Relocated return (V90).** Grounded online living bodies clear quiescent original/
destination NPC and shared peer hull boundaries; require living original source life.
Occupancy commits while outcomes/receipts remain dormant. Preserve source/inventory/life;
rejection installs nothing, uncertainty closes authority. Fresh images/capability 42;
silent recovery. V94 adds bounded stock escort/travel policy and durable private physics frames;
arrival retains the destination without quest credit. Broader mutable AI remains later.

**Persistence.** Validate before allocation/mutation. Stage gameplay/scripts/presentation;
preserve miss/failure costs. Persist before publication; restore content/version-bound
state atomically. No checkpoint-only acknowledgments or parallel files.

**Borrowed lifetime.** Ptr copies retain destruction witnesses; assignment preserves
destinations. Validate lifetime then ownership; addresses cannot authorize installation.

**Inventory.** Commit session state, one intent and actor tick/wear together;
reject later intents in ingress order. Recover bound identities and item state
without rerolls/auto-equip.

**Versioned domains.** Retain prior [layouts](../../apps/tes3mp-server/native/inventory_host.hpp);
changed descriptors require fresh campaigns or explicit migration.

**Time/weather (v12+).** Stock calendar/REGN/fallbacks advance ≤4,096 regions at
30 Hz independently of menus/occupancy. Bind content/RNG/clocks; no downtime catch-up.

**Streaming (v14).** Bind 1–256 cells and occupied interior/3×3 exterior scenes.
Unloaded doors freeze; teleports commit destination/epoch. Baselines retain canonical
loot/doors/players/gear.

**Leveled actors (v15).** Campaign-seeded stock RNG selects in placement order;
persist without rerolls. Markers own unscripted actors; clients suppress spawning.

**Transfers/equipment.** Detached Take All validates revisions and stock stacking/corpse
slots; no living access/disposal. Identity-bound 19 slots use format 7. Unavailable
effects reject; equipped appearance and ground suppression follow commits.

**Cross-actor effects (V51).** Absorb binds the original caster life. Effects/resources/
RNG commit together. Dispel keys source/caster/life/launch, retaining enchantments/constants.
Capability 24 carries authoritative paralysis; movement cannot bypass it. Fresh campaigns.

**Detached movement.** Stock physics commits water, inventory/stats/effects and
landing/drowning damage. Player environmental deaths have no actor killer.

**V65 movement rules.** Bounded trusted EnableLevitation/DisableLevitation requests
commit world rules/effect removal; recovery validates sources. Shared solvers use
each neighbor's movement stats/effects/drowning damage. Script scheduling remains M5.

**Object state.** Touch Lock/Open commits with streamed doors/actors. Normalize
unlocked FLTV to zero; preserve other authored bytes. Revalidate durable angle/
motion/lock before payment, independently of presentation counters. Separate container
lock/contact and stack revisions. Locks gate activation/take/put/baselines.

**AI decisions (V57).** Persist Flee target/deadline/destination with combat/RNG.
Bounded passive abilities/constants retain durable rolls. T3D1/capability 29 feeds
committed Charm into stock dialogue disposition.

**Player AI context (V58).** Persist bounded factions, crime/bounty, draw/werewolf
state and spell/item selection. Recovery validates content IDs/inventory; only
trusted updates write them. Share stock aggression with server authority.

**Social lifecycle (V59–66).** Werewolf/crime/witness effects commit together.
≤128 unscripted/unleveled witnesses retain placement-bound simulation/lives.
Command requires an available living matching caster life; older caps persist.

**Player travel (V67/V68).** Each player owns a durable Mark. Recall commits
payment/destination/epoch; rejection retries, storage failure closes until coherent
restart. V68 persists bounded Enable/DisableTeleporting. Disabled/unmarked casts
pay without teleporting.

**Object/travel (V69).** Explicit-player stock intervention caps interiors at 256;
unbound destinations reject. Reach/Lock/Open, collision and effects commit together.
Stock gem selection captures once per matching caster/creature life; gem/death/payment
persist together. Capability 30 carries authoritative Telekinesis reach.

**Equipment ownership (V70).** Stock bound/ExtraSpell handlers own temporary identities/
acyclic restoration links; ordinary items stay independent. Forbid temporary stacking/
transfers; splice expired links; manual changes win. Effects/constants/wear/draw/counters
commit together. Rejections install nothing; failed permanent attempts wait for source
changes. Recovery validates ownership.

**Dynamic actors (V71).** Namespace 3 shares the inventory counter. Collision/inventory
membership commits together; owner/source/life forms an acyclic forest. Remove descendants
without respawn; recovery validates content/identity/life/animations. Stock writers own
active-area collision. Wait/rest preserves action/respawn ticks while advancing game time.

**Player lives (V72).** Persist identity/generation/spawn/attributed deaths; zero killer
is environmental. Death clears body/actions/sources while retaining attribution.
Stock online respawn preserves inventory/social/Mark and increments generation.
Cleanup/RNG/membership/epoch commit together; stale lives reject. Capabilities 32/37
gate lives/early revival; UI waits for durability.

**Committed bodies (V73).** Persist stock death action/group/frame/RNG; sample active
clocks without client cleanup. Capability 33 gates recipes; publish capability-30 aliases
once. Projectile WhenStrikes application/consumption shares the inventory transaction.

**Spell sources (V74).** Persist stock power deadlines and source/launch/Dispel rules.
Travel commits recipient/ordinal relocations; ignored entries preserve other ordinals.
Scripts/spellbooks remain M5.

**Passive Self (V75).** Bound witnesses/Corprus clocks; retain stock resistance,
base-stat rules/no-ops. Cures quench ordinals; removal undoes harmful worsening,
preserving benefits. Dormant actions wait; recovery never replays. Commit metadata/mutations together.

**Target recipients (V76).** Zero-recipient Self launches by release facing;
persist direction/caster life/time. Reuse stock effect transactions; reject recipients
born after release, retain named lives/object revisions. Discover intervention cells
before manifest binding; budget exhaustion rejects. Capability 35 admits ordinals
beyond seven; older peers omit those transient applications.

**Spell presentation (capability 34).** Stock helpers publish committed life-bound
windup/failed-school/ordinal cues. Atomic snapshots restore loops without transient
replay. Capability 33 retains legacy cues; presentation never persists.

**Shared cells (V77).** Connected exteriors share physics/navigation; other regions
isolate. Caps: 256 cells, 37 actors/summons, 8,192 references/region, 256 navigation
tiles/actor. Radius 0–7 anchors share budgets without recursive discovery. Demand
includes adjacent geometry/frozen hulls/contacts/support. Fingerprint-validate swaps.
Combat/visibility follows actor cells; simulate occupancy once, freeze empty cells.
Persist flight/cast/spawn cells; unsupported scripts/leveled/corpses reject.

**Determinism and network boundaries.** Save order/ticks and RNG;
measure consumption across platforms. Preserve session authentication,
identities, stale/retry rejection, bounded traffic and secret-free evidence.
Direct-IP encryption lacks server identity authentication.

**Native chargen (V78).** Persist two profiles/winning bodies/custom classes/resource
signatures with stock inventory/stats/spells. Fresh profiles cannot simulate; bounded
starters grant once. Omission stays empty; one safe point. Recovery validates resources.
Gear/cell rebuilds preserve bodies/profiles; capabilities 6/36 gate creation and public gear.

**Character installation:** Profile revisions bind coherent installation; motion cannot
raise inventory floors. Delayed lanes resync; gear presents independently.

**Resume inactivity:** live authenticated bindings renew bounded token expiry;
validated snapshots renew client deadlines. Disconnect stops renewal. Expired tokens
and old generations reject; rotation/replay rules remain unchanged.

**Modded chargen:** allow 8,192 pairs/race and 65,536 total; validate sizes before allocation.
