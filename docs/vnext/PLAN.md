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

**Closed, 2026-10-06:** completed acceptance and explicit owner sign-off in this
chat close M4, including combined encounter acceptance and V79 player cutover.
[CURRENT.md](CURRENT.md) preserves retained evidence, runtime bounds and attribution
of owner sign-off; older capture records leaving full M4 open predate closure.

**Accepted outcome:** server-owned OpenMW melee/ranged/magic mechanics, NPC
navigation/collision/door avoidance, effects/wear, life-indexed death/respawn,
shared corpse loot and coherent recovery for the declared bounded loadout.
Exploration retains adjacent-cell combat/visibility, split/regroup/unload and
reentry/reconnect/restart without duplicate simulation or outcomes. V79 shared
player physics meets the reconciliation budgets in
[DECISIONS.md](DECISIONS.md#m4-actor-simulation); older campaigns retain migration.

**Retain as regression contract:** stock weapon/attack modes, unarmed
fatigue/knockout/health, armor/block/resistances, spell sources/ranges/areas/order,
143 selectors/141 lifecycles including ExtraSpell and stock placeholders,
enchantments, AI, projectiles/ammunition, equipment/summons, presentation and
actor/life identity. Preserve stock restrictions, single writers, bounded input,
atomic failed writes and durable resources/RNG/effects/deaths/loot together.

Combined acceptance covers sustained overlapping melee/ranged/magic from both
desktops and NPCs with varied vanilla/TR equipment under latency/jitter/loss;
simultaneous impacts, interruption, death, corpse-loot contention, respawn,
disconnect/restart during flight/effects/get-up and convergence without duplicate
costs/damage/loot. Retain stock-speed render-frame body/casting/recovery sampling,
callback suppression and shared collision. Focused automated captures establish
their exercised paths; combined encounter closure includes owner acceptance.

**Boundary:** M5 owns general scripts, full AI packages and personal quest
credit/rewards; M7 broadens compatibility. Authored corpses/summons never become
permanent spawns. Preserve the accepted bounded runtime and shared calculations;
retire replaced independent writers only after replacement evidence.

## M5 - Complete scripted quests independently and together

**Outcome:** unchanged engine scripts support a living shared world and independent
progression. **Active, implementation unstarted:** M4 is closed. Begin with
the shared-world-first slice below. Follow [current direction](DECISIONS.md#scripted-quest-execution-contract):

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
