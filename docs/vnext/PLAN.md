# Implementation plan

This is an ordered roadmap, not a growing task diary. CURRENT.md holds the sole
active status. Each session implements one bounded part of the active milestone;
a milestone may take several sessions. Preserve these outcomes and replace the
next action in CURRENT.md instead of adding nested phase plans or session notes.

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

**Exit:** an actual Tamriel Rebuilt location works with two clients; a shared
container/door change and regional weather transition converge after late join
and reconnect. The test names the loadout and observes client presentation.

**Retire:** hand-enumerated cells/reference catalogs and corresponding fixture-only
production bootstrap for migrated paths. Keep small fixtures as fast tests.

## M4 - Fight together using OpenMW mechanics

**Outcome:** two players engage the same actor, see consistent AI, hits, effects,
death, equipment wear, and loot, with the server deciding gameplay outcomes.

Reuse actor AI/combat/effects and selected physics/navigation services. Establish
server validation of movement/contact/targeting; client-reported positions alone
do not prove authoritative reach. Introduce movement correction/prediction as
needed. Migrate one melee/effect path at a time, including spells/projectiles and
enchantment lifecycle required by the chosen encounter. Account for missed
attacks, resource use, friendly targeting, and retry/reconnect behavior.

**Exit:** a recorded two-client encounter and narrow rejection tests establish
single damage/death/loot consequences, shared actor targeting, and convergence.

**Retire:** replaced independent combat/AI/effect resolvers, hardcoded respawn
policy, duplicate settings, and client gameplay writers. Keep already shared
calculations where appropriate. One live authority per migrated subsystem.

## M5 - Complete a real quest together

**Outcome:** the cooperative party completes an existing scripted quest using
OpenMW dialogue and script behavior, with shared progress/world consequences.

Run world/global gameplay scripts once on the server. Give dialogue, Player
operations, item locals, and relevant Lua bindings explicit player/party context.
Keep UI-only execution client-side. Meter execution, action queues, recursion,
and allocations; rejected operations cannot leak effects. Preserve applicable
OpenMW script serialization rather than requiring every quest to be rewritten
into the old custom command language. Bind dialogue to actor/conversation state.

**Exit:** one actual TR quest covers dialogue conditions, an item/script change,
progress, a reward, and a world consequence. Both players agree after retries,
reconnect, and concurrent interaction. Record the tested quest/semantics in the
fixture, including initiator-specific rewards and unique-object behavior.

**Retire:** bespoke per-quest catalogs, duplicate quest logic, and old module ABI
on migrated paths. Unsupported script APIs receive explicit diagnostics, not
silent local authority or an advertised compatibility claim.

## M6 - Restore the shared campaign

**Outcome:** restart resumes the same shared world and each player's character.

Extend M2 persistence across dynamic definitions, references, actor/AI/effect
state, inventories, quest/journal/faction state, script state, time/weather, and
RNG. Bind saves to content and runtime versions; never serialize process pointers
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
