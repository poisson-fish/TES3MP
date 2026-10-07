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

**Closed, 2026-10-06:** owner sign-off includes combined encounters and V79 player
cutover. [CURRENT](CURRENT.md) retains evidence/bounds/attribution; older open
captures predate closure. Accepted native melee/ranged/magic, NPC navigation/
collision/doors, effects/wear, lives/respawn/corpse loot and coherent recovery.
Exploration preserves adjacent combat, split/regroup/unload/reentry/reconnect/restart
without duplicate simulation. V79 meets [reconciliation budgets](DECISIONS.md#m4-actor-simulation);
older campaigns retain migration.

**Retain as regression contract:** stock weapon/attack modes, unarmed
fatigue/knockout/health, armor/block/resistances, spell sources/ranges/areas/order,
143 selectors/141 lifecycles including ExtraSpell and stock placeholders,
enchantments, AI, projectiles/ammunition, equipment/summons, presentation and
actor/life identity. Preserve stock restrictions, single writers, bounded input,
atomic failed writes and durable resources/RNG/effects/deaths/loot together.

Combined acceptance covers sustained overlapping melee/ranged/magic, two desktops/NPCs, varied vanilla/TR equipment,
latency/jitter/loss, simultaneous impacts, interruption/death/loot contention/respawn,
disconnect/restart during flight/effects/get-up and convergence without duplicate
costs/damage/loot. Retain stock-speed body/casting/recovery sampling, callback
suppression and shared collision. Captures prove exercised paths; owner acceptance
closes combined encounters.

**Boundary:** M5 owns general scripts, full AI packages and personal quest
credit/rewards; M7 broadens compatibility. Authored corpses/summons never become
permanent spawns. Preserve the accepted bounded runtime and shared calculations;
retire replaced independent writers only after replacement evidence.

## M5 - Complete scripted quests independently and together

**Outcome:** unchanged engine scripts support shared exploration, independent
progression and optional parties with individual rewards. **Active:** initial
shared-script host scheduling implemented; ambient delivery/desktop proof pending. M4 is closed. The [M5 contract](DECISIONS.md#m5-implementation-contract)
owns behavior, party defaults, admission rules and acceptance; sequence bounded slices:

1. **Shared world first:** one context-free engine script executes once across two
   desktops. Prove atomic failure, presentation separation and reentry/restart.
   Inspect excluded Seyda Neen content; restore only proven operations.
2. **Context and transactions:** scope InterpreterContext, reference lookup, dialogue
   filters, journals/topics and cross-script state. Prove isolated characters and
   stock callers; introduce committed/deferred/rejected outcomes.
3. **Waits and death:** persist runtime dependencies, attributed life-event cursors
   and affected clocks. Prove foreign/own/pre-quest/helper credit, fair access after
   repeated foreign deaths, retry/restart and bounded diagnostics.
4. **Physical scope and acquisition:** implement reservations, durable activities,
   coherent instances across cells and provenance-based acquisition/depletion.
   Prove escorts/departures, stock AI packages, scoped inventory/spawns and
   recovery-copy restrictions.
5. **Lua and recovery:** carry context through bindings/storage/events/timers; prove
   rollback before admission. Generalize coherent recovery already required by earlier
   slices. No quest handlers, prerequisite graphs or inference of ambiguous origins.
6. **Content proof:** Seyda Neen activation/ring/departure/routines/taxman, then one
   unchanged TR quest. A finishes; B completes without A/items. Exercise concurrent
   dialogue, failed writes, waiting/reserved restart and retained M4 behavior.
7. **Party addition:** compatible enrollment, shared choices/events, helper roles,
   per-character costs/rewards and durable receipts. Prove late join, disagreement,
   offline/leader loss, leave/kick/rejoin and independent continuation. General
   progress merging stays deferred; parties do not delay the first shared-script slice.

**Acceptance:** satisfy the contract's proof obligations using arbitrary-name fixtures
and unchanged content; no mutation leaks, duplicate public loot/spawns/rewards or
authored-corpse revival. Run stock script harness/filtered native tests individually.

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
