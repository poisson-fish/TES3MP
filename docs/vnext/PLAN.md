# Implementation plan

This is the sole roadmap; CURRENT.md holds status and next action. Implement one
bounded slice of the active milestone per session. Preserve outcomes without
adding phase plans or diaries.

## M1 - Prove the native loadout and runtime seam

**Outcome:** a small app-local headless probe uses OpenMW's real content path,
with executable evidence showing what can be reused and what must be separated.

1. Resolve actual OpenMW configuration layers, data paths, encoding, masters,
   and ordered TES3 plugins using OpenMW code. Enumerate winning item/spell/
   enchantment/GMST records after applicable setup/normalization. Handle ignored
   and deleted records and report load errors; Python must not reinterpret ESM.
2. Translate a bounded sample to owned diagnostic/network-facing values. Keep
   the loaded engine records available to the runtime; this is not a new manual
   catalog requirement for each item or quest. Test malformed/oversized export
   rejection without partial publication.
3. Probe an actual OpenMW inventory operation without initialized UI/rendering.
   Expose the required service/player dependencies with a scripted or enchanted
   case, as well as a plain item. Do not substitute CanonicalInventoryWorld and
   call that engine reuse. If the operation is coupled, make the smallest useful
   service separation and report the remaining blocker accurately.
4. Compare an unshared calculation, such as live-actor spell school/chance or
   enchantment cost, with normal OpenMW using the same loadout/settings. Include
   a changed actor skill or GMST. Two calls to the same helper are not parity
   evidence for adapter inputs or effect execution.
5. Record startup/RSS, build/link dependencies, and exercised singleton/player
   assumptions in a bounded artifact under ignored `build/`. Distinguish linked
   libraries from services actually initialized. Broad offline/probe links may
   be temporary; document their specific removal condition in code/CURRENT.

**Exit:** real-loadout enumeration plus the operation/calculation evidence are
reproducible through a named narrow target/command. If coupling prevents the
operation, keep M1 incomplete and identify the exact next refactor. Do not quietly
return to independent gameplay formulas or declare a loader-only result a server.
Use Morrowind content while TR is unavailable, but record TR validation missing.

**Retire:** raw baker semantics only after the replacement covers their consumers;
retain hash/pack selection utilities and useful malformed-input tests.

## M2 - Make engine mutations safe for two player contexts

**Outcome:** two distinct server-controlled player actors share an engine-backed
container and item lifecycle without borrowing one global player's identity.

Implement take, split/drop, equip, and one enchanted/scripted item operation with
explicit initiator, stable instance identity, and separated presentation effects.
Route stock OpenMW through shared mechanics where code is extracted. Preserve
script locals, soul/condition/charge, equipment, and relevant dynamic records.
Exercise stale input, contention, and durability failure: no partial mutation,
duplicate script consequence, or emitted success. Prove isolated staging/effect
handling; copying Ptr/ContainerStore/RefData is not sufficient by itself.

**Exit:** a narrow headless test changes the intended actor only and verifies a
coherent inventory save/restore using engine state plus multiplayer identity.
Choose the concrete persistence adapter here; retain the current acknowledgment
guarantee. General persistence coverage follows in M6.

**Retire:** migrated independent inventory mutations and gameplay-active client
rebuilds once their production callers use the engine path. Preserve useful
identity, validation, and failure tests against the replacement.

## M3 - Connect two clients to a shared modded world

**Outcome:** two desktop clients enter a real modded cell and observe one
server-owned set of references, actors, doors, containers (barrels and chests),
time, and weather.

Connect the runtime to existing authentication, command intake, transport, and
interest. Derive content and placed references from OpenMW instead of fixture
recipes. Activate the union of player areas with bounded scheduling; define
unload/background behavior. Establish stable reference/dynamic-record mappings,
ownership/revisions, initial baselines, and committed incremental updates.
Separate authoritative state from client input, prediction, animation, and UI.
Ordinary doors use server motion with client player-contact reports. Verify
expiry/reversal/disconnect and latency; defer server physics/NPC obstruction.

**Exit:** an actual Tamriel Rebuilt location works with two clients; a shared
container/door change and regional weather transition converge after late join
and reconnect. The test names the loadout and observes client presentation.
Complete M3 integration/evidence before M4; deferred graphical testing leaves M3 open.

**Retire:** hand-enumerated cells/reference catalogs and corresponding fixture-only
production bootstrap for migrated paths. Keep small fixtures as fast tests.

## M4 - Fight together using OpenMW mechanics

**Outcome:** two players engage the same actor, see consistent AI, hits, effects,
death, equipment wear, and loot, with the server deciding gameplay outcomes.

After M3, first prove one server-controlled NPC navigating/colliding in one
interior, replicated smoothly to two clients and continuing when either disconnects.
Retain current player movement until physics prediction/reconciliation meets
[DECISIONS.md](DECISIONS.md)'s cutover requirements. Reuse OpenMW movement,
navigation, AI, collision and combat/effects; extend to NPC-door contact, then one melee/effect
path including spells/projectiles and enchantments. Validate authoritative reach,
misses, resources, friendly targeting and retries/reconnect.

Cover Travel AI beyond processing range, both players' cell boundaries, unload/restart,
and preserved destinations/completion state. Define inactive travel policy and
prevent duplicate simulation.

**Exit:** a recorded two-client encounter and narrow rejection tests establish
single damage/death/loot consequences, shared actor targeting, and convergence.
Prove persisted respawn deadlines, life generations and attributed death events
for M5; authored corpses and transient summons must not become permanent spawns.

**Retire:** replaced independent combat/AI/effect resolvers, hardcoded respawn
policy, duplicate settings, and client gameplay writers. Keep already shared
calculations where appropriate. One live authority per migrated subsystem.

## M5 - Complete scripted quests independently and together

**Outcome:** personal progression over shared NPC lifecycles using unchanged engine
scripts, without quest-specific handlers. Follow DECISIONS.md and these ordered slices.

1. **Context first:** refactor InterpreterContext, dialogue filters and Journal/
   AddTopic around explicit character/story identity. First bounded slice: execute
   one journal/topic script for two characters, proving separate state and unchanged
   stock single-player behavior. Missing identity rejects before mutation.
2. **State and execution:** namespace globals, locals, global-script instances,
   cross-script access and callbacks. Add bounded transactional script execution:
   commit state/effects/event consumption together. Distinguish committed, deferred
   and rejected outcomes in ScriptManager; a wait must not deactivate the script.
3. **Shared death, personal consequences:** connect M4 life generations to
   attributed death history and OnDeath cursors. Implement unavailable-aware actor
   queries, including GetHealth/existence/enumeration. A foreign kill defers B's
   dependent invocation until respawn; B's own failure is preserved. Pause affected
   deadlines, persist wake dependencies and revalidate on retry.
4. **Persistent effects:** route Disable/Delete, relocation, scripted inventory/AI
   changes and spawns through scoped references. Prove A's departure/removal leaves
   B's interaction available. Physical divergence requires a coherent instance;
   unsupported operations reject atomically. Shared combat remains a single writer.
5. **Lua and recovery:** propagate context through Lua bindings, events, storage
   and timers. Prove rollback of script-owned state as well as engine effects before
   enabling gameplay handlers. Extend the durable image for personal state,
   invocations, waits and scoped references; do not recreate today's singleton access.
6. **Content proof:** run one unchanged TR quest separately and cooperatively.
   A finishes first; B later completes without A's presence/items. Verify personal
   rewards, shared encounters, respawn, concurrent dialogue and reconnect. Broader
   compatibility follows exercised API coverage, not this single quest.

**Acceptance:** narrow fixtures use arbitrary script/reference names. Exercise
OnDeath and polling; write a local/grant an item before an unavailable read and
prove zero partial effects. Cover indirect cross-script dependencies, foreign
versus own kills, pre-quest/party credit, Disable, stale generations, late joins,
duplicate callbacks, legitimate repeats, interrupted writes and restart while
waiting. No duplicate public spawns or authored-corpse resurrection. Reuse the stock
script-test harness and filtered native tests; one relevant check at a time.

**Retire:** migrated bespoke quest catalogs, duplicate logic, old module ABI and
unscoped production callers only after replacement/failure evidence. Keep engine
serialization and explicit unsupported-API diagnostics.

## M6 - Restore the world and personal progression

**Outcome:** restart resumes the shared world, NPC lifecycles and personal progression.

Extend M2 persistence across dynamic definitions, references, actor/AI/effect
state, inventories, personal quest/journal/faction state, reward receipts, story
scopes, respawn deadlines/generations, script state, time/weather, and RNG. Bind saves
to content, progression policy and runtime versions; never serialize process pointers
or live network sessions. Reuse engine field serializers behind a coherent
server save boundary. Restore off to the side and install only after validation.

**Exit:** save/restart after the M4 encounter and M5 quest restores both clients'
views. Interrupted writes, mismatched content, and duplicate reconnect requests
preserve a complete prior/new state and never duplicate rewards or items.

**Retire:** replaced canonical-domain serializers and parallel persistence paths.
Development save/protocol compatibility is not a reason to maintain two worlds;
make any required reset explicit rather than silently discarding progress.

## M7 - Establish the supported modded release

**Outcome:** repeatable cooperative play across representative TR locations,
quests, combat, and OpenMW-compatible graphical mods.

Finish gameplay-resource identity and visual-only classification; test exterior/
interior transitions, separate player regions, late joins, loadout mismatch, and
save continuation. Measure content size, active-cell work, memory, network queues,
and long-session stability. Validate affected desktop platforms deliberately.
Publish measured compatibility and remaining exceptions, not arbitrary-mod claims.

**Exit:** the end-to-end scenario remains playable after restart with a graphical
replacement installed, with agreed server performance/player limits. Remove all
unused old-path code, switches, content files, schemas, and obsolete tests after
checking callers; keep the same compact documentation set.
