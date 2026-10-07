# Implementation plan

Sole roadmap; CURRENT.md holds status/next action. Advance verified slices.

## M1 - Prove the native loadout and runtime seam

**Outcome/exit:** headless OpenMW loading, winning records and plain/scripted/
enchanted inventories; atomic bounded diagnostics, real-loadout operations and
stock calculation parity under changed skills/settings. Keep startup/RSS,
dependencies, player assumptions, broad-link removal conditions and TR gaps in
ignored `build/`. Refactor unresolved coupling; loading alone is insufficient.

**Retire:** baker semantics after consumer coverage; retain hash/pack/malformed-input checks.

## M2 - Make engine mutations safe for two player contexts

**Outcome/exit:** two explicit players share native take/split/drop/equip and
enchanted/scripted mechanics, preserving identity, locals, soul, condition,
charge, equipment and dynamic records; separate presentation. Prove actor
isolation, coherent recovery and durable acknowledgments; stale input,
contention and failed writes leak no mutation, script effects or success.

**Retire:** independent inventory writers/client rebuilds after cutover; retain
identity/validation/failure tests. M6 generalizes persistence.

## M3 - Connect two clients to a shared modded world

**Outcome/exit:** two authenticated desktops share references/actors/containers/
doors/time/weather with bounded interest, identities/revisions, baselines,
commits and unload. Named TR graphical evidence covers containers/doors/weather,
late join/reconnect, contact expiry/reversal, disconnect/latency before M4's
server physics/NPC obstruction.

**Retire:** hand-enumerated cells/references and fixture-only production bootstrap;
retain small fixtures.

## M4 - Fight together using OpenMW mechanics

**Outcome:** players and actors use general OpenMW combat mechanics for the
declared gameplay loadout, with server-owned AI, hits, effects, wear, death and loot.

**Before M5:** verify bounded exploration: expand exterior
discovery/streaming; prove adjacent-cell movement/combat/visibility;
two players separate and regroup through unload, reconnect and restart under
delay/jitter/loss. Preserve reference/life identity, state and access across areas,
without duplicate simulation/outcomes. Require two-desktop travel evidence;
synthetic crossings are insufficient. Then begin M5 scripting without
waiting for every remaining M4 combat feature; full M4 sign-off still requires
its exit criteria.

Follow [approved runtime decisions](DECISIONS.md#m4-actor-simulation), in order:

1. Prove one native NPC navigating/colliding in an interior, smoothly replicated
   to two clients under latency/jitter/loss and continuing when either disconnects.
2. Add server-owned NPC-door contact/avoidance using shared OpenMW physics.
3. Retain bounded simulation around active travelers after both players leave.
   Cover processing range, cell boundaries, unload/restart and preserved
   destinations/completion; never simulate an actor twice.
4. Preserve autonomous casting through shared OpenMW selection, equipped WhenUsed,
   visibility, animation-key release/recovery and interruption. Retain durable
   source/caster/life, RNG, effects, expiry and two concurrent player casts;
   broaden remaining unsupported effects before M4 exit.
5. Complete combat before formal acceptance, retaining the verified swing base.
   Retain stock zero-base-fatigue knockout, interrupted-cast cancellation without
   unrelated tick rollback, and complete life-indexed death attribution within the
   save byte budget. Preserve focused failure/recovery and death/loot deduplication
   checks; M5 owns personal quest credit and rewards.
   Retain the bounded **actor presentation smoothness** slice and extend body coverage:
   attacks, hit reactions, falls, knockout loops and get-up
   for self, remote players, one NPC and one creature; extend custom-body and casting
   coverage through the same timeline. Measure render cadence, snapshot gaps
   and clip duration against stock at matching weapon speed. Sample visual poses
   each render frame between server timestamps;
   preserve authoritative timing, pauses, interruption and callback suppression.
   Accept with 60-FPS video/frame traces on both desktops showing smooth progression
   and stock-speed recovery under loss/delay/jitter and reconnect/restart, without
   duplicate hits or premature recovery. Keep movement cutover separate.
   Finish unarmed damage while down, recovery, armor, block, resistances and wear.
   Complete 143 built-in effects (CURRENT), including stock ExtraSpell.
   Cover applicable spells/powers/abilities/diseases/enchantments, AI and targets,
   ranges/areas/order, stacking, defenses/cures/expiry, derived stats, equipment,
   controls, visuals, life ownership, atomic retry and reconnect/restart.
   Preserve stock restrictions; admission alone is insufficient. Use shared stock/server
   handlers and parameterized family lifecycle checks. Follow dependencies through
   shields/stats/resources, conditions, concealment/detection, movement, AI,
   objects/travel, bound equipment and multi-actor summons.
   Match all stock OpenMW combat visuals/audio: casts, composite bolts, bolt
   light/sound, hit/area effects, persistent loops, enchanted glow,
   visibility/lighting and animation timing. Use committed events/active snapshots;
   reconnect restores loops without replaying impacts. Distinguish gameplay,
   source and presentation evidence. Check family members and semantic
   exceptions; share stacking/expiry, atomic failure, reconnect and restart fixtures.
   Generalize beyond one selected NPC: simulate neighboring combatants once each
   and persist player death/respawn generations with stale-life rejection.
   Then complete ranged execution: durable receipt recycling, ammunition recovery,
   validated player contacts, NPC bows/crossbows/thrown weapons, and desktop input
   and projectile presentation. Replace plain-source restrictions with generic
   loadout support; preserve weapon selection, attack modes and animation bindings.
   Prove sustained, overlapping melee/ranged/magic from both desktops and NPCs,
   using varied vanilla/TR weapons, armor and effects under latency/jitter/loss.
   Cover simultaneous impacts, interruption, death, corpse-loot contention, respawn,
   disconnect and restart during flight/effects/get-up. Sequential headless actions
   establish transaction evidence only; they cannot satisfy this live requirement.
   Persist resources, swings, flight, recovery, knockout, attributed deaths and
   loot together. Failed writes leak no resources, RNG, damage or successful events.
   **Finish line:** both clients converge after reconnect/restart without duplicated
   ammunition costs, damage or loot. Focused acceptance may run incrementally;
   full M4 sign-off requires all implementation gaps and exit criteria closed.
6. Accept V76 Target/destination/size/capability and committed collision on both
   desktops: drop/pickup physics/projectile/visibility/navigation, rollback,
   reload/restart and smoothness under latency/jitter/loss. Before player cutover,
   accept V77 NPCs: door entry, two occupants, split interiors, combat,
   flight isolation and state after reentry/reconnect/restart. Switch player
   movement last using shared physics (V79), meeting DECISIONS.md's reconciliation
   budgets. Retain inherited movement for older campaigns.

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

**Retire:** independent combat/AI/effect resolvers, hardcoded respawn policy,
duplicate settings and client gameplay writers; retain shared calculations/one canonical writer.

## M5 - Complete scripted quests independently and together

**Outcome:** unchanged engine scripts support a living shared world and independent
progression. Start after accepted exploration; remaining M4 exit criteria
stay open. Follow [current direction](DECISIONS.md#scripted-quest-execution-contract):

1. **Shared world first:** execute one context-free world script once for two
   desktops, preserving state through reentry/restart. Broaden ambient/object/door/NPC
   scripts by supported operations; separate presentation. Inspect excluded Seyda
   Neen content; restore only proven services.
2. **Context and transactions:** refactor InterpreterContext, dialogue filters,
   Journal/AddTopic and cross-script access. Prove separate journals/topics and stock
   single-player behavior. Namespace story globals/locals/script instances/callbacks;
   personal operations require character/story identity. Bound execution and commit
   state, effects, RNG and event consumption together. ScriptManager distinguishes
   committed/deferred/rejected; waits never deactivate scripts.
3. **Runtime waits and diagnostics:** discover dependencies during execution;
   persist bounded wake records and revalidate on changes. Log script/instruction,
   context/reference/life, reads, effects and outcome; fix API semantics with
   arbitrary-name regressions, without quest-specific handlers or prerequisite graphs.
4. **Shared death:** consume attributed M4 life history through personal OnDeath
   cursors and unavailable-aware health/existence/enumeration. Foreign kills defer
   dependent work until respawn; preserve own failures and pause affected deadlines.
   Explain waits; show authoritative remaining respawn time only when known.
5. **NPC activities and divergence:** reserve conflicting scripted control for one
   story while server AI/combat continues. Define completion, disconnect, death and
   recovery rules before admission. Release alone cannot restore departed NPCs.
   Scoped Disable/Delete, inventory/AI edits, relocation and spawns preserve others'
   access; persistent physical divergence needs coherent instances across affected
   cells. Restoration/item-access policies remain unresolved; never reset rewards.
6. **Lua and recovery:** propagate context through bindings/storage/events/timers;
   prove Lua-state and engine-effect rollback before admission. Persist personal state,
   invocations/waits/reservations/scoped references coherently. Keep story identity ready
   for optional shared-party stories; party formation/progress merging remain deferred.
7. **Content proof:** advance Seyda Neen activation, ring rewards, departure, scripted
   routines and taxman branches; then one unchanged TR quest independently/together.
   A finishes; B completes without A/items. Verify concurrent dialogue/recovery;
   API coverage bounds compatibility.

**Acceptance:** arbitrary names; OnDeath/polling, indirect dependencies, foreign/own
and pre-quest/helper credit, stale lives, late joins, legitimate repeats, duplicate
callbacks, failed writes and restart while waiting/reserved. Local/item writes before
unavailable reads leak nothing. No duplicate public spawns/authored-corpse revival.
Prove reservation contention and instance isolation. Use stock script harness/filtered
native tests individually.

**Retire:** bespoke quests, duplicate logic, old ABI/unscoped callers after replacement
evidence; retain engine serialization and unsupported-API diagnostics.

## M6 - Restore the world and personal progression

**Outcome:** restart restores shared world/NPC lives/personal progression.

Persist dynamic definitions, references, actor/AI/effects, inventories, personal
quest/journal/faction state, reward receipts, story scopes, respawn deadlines/generations,
scripts, time/weather and RNG. Bind saves to content/policy/runtime versions;
exclude pointers/sessions. Reuse engine serializers; stage and validate before
installing coherent recovery.

**Exit:** save/restart after the M4 encounter and M5 quest restores both clients'
views. Interrupted writes, mismatched content, and duplicate reconnect requests
preserve a complete prior/new state and never duplicate rewards or items.

**Retire:** replaced serializers and parallel persistence. Require explicit resets;
development compatibility cannot justify two worlds or silent progress loss.

## M7 - Establish the supported modded release

**Outcome/exit:** repeatable TR exploration/quests/combat with graphical mods and
restart; gameplay/visual resource classification. Test transitions, separate
regions, late joins, loadout mismatch and save continuation. Measure content size,
active-cell work, memory, queues and long-session stability across affected
platforms; agree performance/player limits. Publish evidenced compatibility;
retire unused code/switches/content/schemas/tests after checking callers.
