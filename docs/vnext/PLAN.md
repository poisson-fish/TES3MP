# Implementation plan

This is the sole roadmap; CURRENT.md holds status and next action. Implement one
bounded slice of the active milestone per session. Preserve outcomes without
adding phase plans or diaries.

## M1 - Prove the native loadout and runtime seam

**Outcome:** an app-local headless probe reuses OpenMW configuration/content loading,
winning records and inventory behavior, including plain and scripted/enchanted cases.
Retain engine records; export bounded owned diagnostics with atomic rejection.

**Exit:** named narrow checks reproduce real-loadout enumeration, inventory operations
and independent stock calculation parity with changed skills/settings. Record startup,
RSS, dependency/service initialization and player assumptions under ignored `build/`;
identify temporary broad-link removal conditions and missing TR evidence. Loading
alone does not prove gameplay; unresolved coupling requires engine refactoring.

**Retire:** replaced raw baker semantics after consumer coverage; retain hash/pack
selection and malformed-input tests.

## M2 - Make engine mutations safe for two player contexts

**Outcome:** two explicit player contexts share native take, split/drop, equip and
enchanted/scripted item mechanics. Preserve instance identity, locals, soul, condition,
charge, equipment and dynamic records; separate presentation effects.

**Exit:** narrow headless checks prove intended-actor mutation, isolated staging,
coherent engine/identity save/restore and acknowledgment durability. Stale input,
contention or durability failure cannot leak mutation, script effects or success;
object copies alone do not establish isolation.

**Retire:** replaced independent inventory writers/client rebuilds after production
cutover; retain identity, validation and failure tests. General persistence follows M6.

## M3 - Connect two clients to a shared modded world

**Outcome:** two desktop clients share native references, actors, containers, doors,
time/weather through authentication, transport and interest. Bound player-area activity;
preserve identities/revisions, baselines, committed updates and unload semantics.

**Exit:** named TR loadout and observed client presentation prove shared container/door
changes, weather, late join/reconnect. Doors cover contact expiry, reversal, disconnect
and latency; server physics/NPC obstruction follows M4. Graphical acceptance precedes M4.

**Retire:** replaced hand-enumerated cells/references and fixture-only production
bootstrap; retain small test fixtures.

## M4 - Fight together using OpenMW mechanics

**Outcome:** players and actors use general OpenMW combat mechanics for the
declared gameplay loadout, with server-owned AI, hits, effects, wear, death and loot.

Follow [approved runtime decisions](DECISIONS.md#m4-actor-simulation), in order:

1. Prove one native NPC navigating/colliding in an interior, smoothly replicated
   to two clients under latency/jitter/loss and continuing when either disconnects.
2. Add server-owned NPC-door contact/avoidance using shared OpenMW physics.
3. Retain bounded simulation around active travelers after both players leave.
   Cover processing range, cell boundaries, unload/restart and preserved
   destinations/completion; never simulate an actor twice.
4. Generalize OpenMW combat through one cast lifecycle: prepare source/effects,
   pay spell cost or item charge, launch, resolve server contact, apply effects.
   Prove a resistible Target spell with durable projectile/outcome first, then
   enchantment charge/use on that path, then effect durations, areas and player
   targets. Broaden sources and allow concurrent projectiles next. Finish
   knockout, unarmed health damage, armor, block and remaining melee rules. Preserve wear and
   durability; validate misses, targeting, retries and two-client reconnect.
5. Switch player movement last, after unified collision/physics and smoothness
   verification, meeting DECISIONS.md's prediction/reconciliation budgets.
   Retain inherited movement until this cutover.

**Exit:** bounded slices are insufficient. All OpenMW-supported combat paths in
the gameplay loadout work generically across actors, players, records and targets:
armed weapon types/attack modes, unarmed fatigue/knockout/health,
armor/block/resistances, spell ranges, areas, effects and durations,
projectiles, enchantments, AI, death and loot. No unsupported combat
path remains for M4. Vary content/effect combinations and prove two-client
convergence, validated contacts, durable single outcomes and reconnect/restart.
Meet shared collision and movement smoothness budgets. Persist respawn
generations, deadlines and attributed deaths for M5; authored corpses and
summons never become permanent spawns.

**Retire:** replaced independent combat/AI/effect resolvers, hardcoded respawn
policy, duplicate settings and client gameplay writers. Retain shared
calculations and one canonical writer.

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
