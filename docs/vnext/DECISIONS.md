# Durable decisions

These rules define the target; CURRENT.md identifies implemented behavior.
Independent progression, engine-level mod support and shared respawning NPCs define
the direction; lifecycle and script-scoping details below remain proposals.
Replace superseded rules rather than appending session history.

## Product, authority and reuse

**OpenMW gameplay is the foundation.** Reuse/refactor content, mechanics, world
and scripting on a dedicated authoritative server, including for Tamriel Rebuilt.
Do not grow independent gameplay formulas, manual content catalogs or a custom
quest language. Keep the 0.51.0 baseline until an explicit upgrade. TES3MP 0.8
wire/API/save compatibility is not required; existing code may be reused when
inspection/tests justify it. MWSE/native-engine-only behavior is not automatic
compatibility. Mod support requires evidence.

**Independent networking; native runtime.** Keep components/tes3mp portable with
owned integration values. A distinct app-local server-runtime leaf may use OpenMW
types internally and extract shared gameplay for stock callers. Preserve dependency
checks; no engine-wide cleanup is prerequisite to a vertical slice.

**One gameplay loadout.** OpenMW resolves configuration, encoding, load order,
overrides/deletions and references. Bind plugins, scripts, settings and relevant
resources to server/client/save identity. Python may package/hash/cache, not
reinterpret ESM. No manually curated item catalog is required. Unsupported behavior
fails visibly, never through silent client authority.

**One server authority.** The server owns actors, objects, player resources,
time/weather and gameplay outcomes, including every personal story scope.
Clients submit authenticated intent and present committed results. Prediction
cannot author damage, rewards or world mutations. Replace inherited movement
authority with validation for combat. Schedule the union of player areas;
one player's menu/scene cannot control the whole simulation.
NPC simulation stays server-owned; no client ownership leases.

**M3 doors:** server transactions own activation, reversal, angle, direction and
persistence through shared OpenMW rules. Clients apply committed angles and report
only their own player's contacts. Complete committed door images present independently
of inventory/equipment revision alignment, with session and active-cell guards.
Reports bind authenticated session/generation,
placement, motion, sequence and observed tick; any fresh block stalls progress.
Expiry is ten ticks. Reports/motion IDs are transient; reversal/reconnect invalidates
old reports. Collision is trusted like inherited movement. Latency can clip before
a stall; no response barrier or rewind is promised. Defer server physics and NPC
obstruction ownership to shared actor simulation; replacing reports must preserve
transactions, persistence and replication.

**Presentation is separate.** Route UI, animation, audio and graphics to relevant
clients. Pure visual replacements may vary; collision/bounds and script-affecting
resources belong to gameplay identity. Reuse supported MWScript/Lua execution and
semantic serialization with explicit context and resource limits. Never persist
raw memory, process pointers, rendering objects or live sessions.

**Movement smoothness (target).** Server NPC physics precedes player movement
cutover. Interpolate timestamped remote snapshots with bounded extrapolation;
use latest-wins movement snapshots and reliable durable events. Local players
predict immediately using shared movement/collision rules and compatible fixed
steps. Restore authoritative physics state at acknowledged input, then replay
unacknowledged inputs. Define dynamic-obstacle timestamps/revisions and sufficient
history for contact reconciliation. Separate collision correction from visual
blending; reset history on teleports, respawns and cell transitions. Exercise
latency/jitter/loss from the first NPC slice; require correction size/frequency
and tick-overrun budgets before player cutover. Existing smoothing/input-history scaffolding
does not establish OpenMW physics replay.

## Cooperative progression design

**Requirement:** each character can complete supported campaigns independently,
including after joining late. Another's actions, absence or possessions cannot
permanently remove their opportunities. Their own mutually exclusive choices still
have consequences. Support arbitrary content within supported OpenMW APIs through
engine rules, not a mandatory quest-adaptation catalog. This is a design target.

### Shared world, personal progression

**Direction:** NPCs normally inhabit one shared world: anyone can fight/kill them,
and they return for later players. Temporary unavailability is acceptable; permanent
loss of another character's progression is not. Personal campaign state primarily
owns journals, choices, relationships, faction progress, rewards and script history.
Whole private campaigns are not the default response to an NPC death.

**Generic respawn proposal:** configurable 15-minute real-time delay for initially
living content-placed NPCs, including mod placements regardless of single-player
respawn flags. OpenMW's resolved loadout supplies identity, origin and reconstruction
data; no NPC list. Persist deadlines across unload/restart; game-time skips cannot
accelerate them. Authored corpses, summons, scripted spawns and deleted placements
require lifecycle rules, not new permanent spawn points.

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
actor data/placement; multiplayer respawn is unimplemented.
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

Shared simulation runs once. Personal scripts execute per eligible character;
their spawns remain scoped. Saved locals and cursors survive NPC respawn. Personal
locals key by character, script and stable reference, independently of life
generation; lifecycle fields reset separately. Script-generated combat intents
must explicitly enter the shared action path. API coverage, rather than per-quest
rewrites, establishes which mods can use these rules.

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

M5 must prove these unimplemented contracts. Ordinary shared loot still requires
renewable access/transfer rules; no automatic quest-item classifier is assumed.
Preserve v8 campaigns until explicit versioning/migration.

### System coverage

The enumeration remains an engine checklist, not a per-quest adaptation catalog.

| Systems | Required behavior |
|---|---|
| 1–5: creation/tutorial; journals/branches; dialogue/topics; disposition; factions/titles | Personal startup, choices, history, relationships and eligibility. |
| 6–9: crime/reputation; NPC death/departure; combat/bosses; escorts/followers | Personal attribution; shared lives; scoped scripted departures and follower ownership. |
| 10–14: quest items/keys; artifacts; rewards/training; loot/crafting/merchants; doors/traps/travel | Personal grants/turn-ins, shared economy/mechanisms, renewable access; scarcity remains an explicit policy. |
| 15–18: puzzles/triggers; buildings/strongholds; endings; diseases/transformations | Scoped irreversible changes and personal effects; coherent physical variants where necessary. |
| 19–22: script state; spawns/ambushes; time/rest; menus/dreams/cutscenes | Contextual execution, lifecycle identity, bounded scheduling, protected deadlines, correct UI recipient. |
| 23–25: death/recovery; parties/late joins; persistence/concurrency | No world rollback or copied journals; coherent recovery and replay-safe effects. |

### Concrete regression examples

Unverified content examples:
[Fargoth's Ring](https://elderscrolls.fandom.com/wiki/Fargoth%27s_Ring) (items/relationships),
[The Code Book](https://strategywiki.org/wiki/The_Elder_Scrolls_III%3A_Morrowind/Fighters_Guild) (faction choices),
[Caius/Mehra Milo](https://elderscrolls.fandom.com/wiki/Mehra_Milo_and_the_Lost_Prophecies) (departure),
[Corprus](https://help.bethesda.net/app/answers/detail/a_id/17714/~/how-do-i-cure-corprus-disease) (personal effects),
[Telvanni councilors](https://elderscrolls.fandom.com/wiki/Kill_the_Telvanni_Councilors) (death/respawn),
[Ahemmusa](https://elderscrolls.fandom.com/wiki/Ahemmusa_Nerevarine) (escort),
[Redoran stronghold](https://elderscrolls.fandom.com/wiki/Redoran_Stronghold) (construction),
[the finale](https://en.wikipedia.org/wiki/The_Elder_Scrolls_III%3A_Morrowind) (tools/aftermath).
M5 in PLAN.md owns implementation order and acceptance; no quest-ID whitelist may
make its tests pass.

## Integrity and migration

**Atomic coherent persistence.** Bound and validate input before allocation/mutation.
Stage script/presentation effects; rejected preparation or durability failure cannot
leak effects or success. Copying engine objects or catching exceptions is not isolation.
Legitimate missed attacks/failed casts may retain prescribed costs. Durability
precedes installation, which precedes publication. Restore the complete content/
version-bound world/player relationship off to the side. No checkpoint-only
acknowledgment, silent resets or parallel canonical files.

**Borrowed lifetime.** Ptr carries a weak reference-lifetime witness; copies preserve
it, destruction invalidates it, reference copy/move construction creates a new
lifetime and assignment preserves the destination lifetime. Check witnesses before
pointer access, then registry/script ownership. Address equality is insufficient.
Checks assume serialized engine access and neither pin nor authorize installation.

**Native inventory cutover.** Session image and command dispositions share one file
transaction; never mirror into CanonicalInventoryWorld. The initial host accepts
one native mutation per tick, durably rejecting later native intents in ingress
order. Descriptor/content fingerprints bind roles, winning placements and the
complete domain. Reference IDs use record-plugin order, not script reader slots.
One registry/counter and stock initial-loot stream cover players then placements
in stable order. Recovery never reloads, rerolls or auto-equips initial loot.
Record-derived identities preserve raw condition/light-time/charge bits.

**Versioned domains.** V3–V13 retain their documented
[meanings](../../apps/tes3mp-server/native/inventory_host.hpp). Descriptor changes
require fresh campaigns or explicit migration; recovery never resets, rerolls loot
or auto-equips.

**Native time/weather (v12+).** OpenMW calendar, REGN and fallbacks advance up to
4,096 regions at 30 Hz regardless of occupancy/menus. RNG, clock, timers and queued
transitions share durability and content/settings/seed binding. No wall-clock
catch-up, client writers or legacy script modules.

**Player-area streaming (v14).** OpenMW discovers 1–256 cells. Canonical state stays
resident; occupied interiors and player 3×3 exterior neighborhoods retain scenes.
Unloading freezes doors without losing inventories. Positions select adjacent
bound exteriors; teleports commit destination/epoch. Neighborhood baselines carry
loot, doors, player visibility and equipment. Ordinary doors share inventory
durability. References, payloads and persistence remain bounded. A new capability
excludes older clients. Fixture-free bootstrap requires established identities and
uses engine environment with inherited client movement; physics remains M4.
It does not authorize unsupported scripts. M3 still requires TR acceptance and
latency measurements.

**Initial leveled actors (v15).** A separate campaign-seeded OpenMW RNG stream and
explicit loot level select initial NPC/creature records in stable cell/reference
order. Persist every marker's selected record or chance-none outcome in the same
inventory/door image. Recovery reads choices without rolling. Marker identity owns
the selected actor; clients suppress local spawning and render the committed
selection. This supports initially living unscripted actors, not respawn, AI or
leveled authored corpses. V15 requires fresh campaigns and the leveled-actor capability.

**Transfers and equipment.** Take All binds a witnessed source stack and both
revisions, uses stock order/stacking and corpse slot removal on detached stores,
and commits all or nothing. No corpse disposal or living access is implied.
All 19 equipment slots use item identities, including ammunition; format 7 has
explicit payload mode while shirt-only 1/5/6 encodings retain meaning. Accepted
equipment intents invalidate revisions even without splitting; unavailable effects
reject. Living appearance exposes only owner and equipped record IDs; private
stacks/counts stay server-side. Presentation copies never become command identities.
Ground baselines suppress the entire bound placement domain, including absent loot.

**Determinism and network boundaries.** Save server order/ticks and RNG state;
measure stream consumption instead of assuming cross-platform replay. Preserve
authentication/session separation, stable identities, stale/retry rejection,
reliable/latest-state traffic, bounded queues, backpressure and secret-free evidence.
Existing direct-IP encryption does not authenticate server endpoint identity.
Protocol/save changes may be deliberate without maintaining competing authorities.

**Retire after cutover.** Preserve working paths until replacements cover production
callers and failure cases, then remove obsolete code/configuration/tests. Keep useful
behavioral evidence without retaining the abandoned independent gameplay design.
