# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Explicit-NPDT and auto-NPDT actors now
  share one scripted constant Fortify Luck shirt, including durable skip handling
  and fresh restart. M2/production integration remains incomplete.
- **Next action:** stage and relocate existing `LocalScripts` registrations for
  this same shirt through equipment preparation, durable installation and fresh
  restart, using the stock prepared-list/storage services already used by transfer.
  Preserve unrelated registrations and cursor state without executing scripts.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. Review and
  commit, then summarize capability, limitations, verification and commit and
  provide a ready-to-paste next 2–3-slice prompt carrying these requirements.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  implementation before the engine-backed pivot.

## Implemented behavior

[Item-local services](../../apps/openmw/mwscript/itemlocals.cpp) share stock
`InventoryWindow::useItem` and `InventoryStore` unequip handling with explicit
actor/player and declaration contexts. Stock local reads/writes share existing
conversions and missing-variable behavior. Normal use clears then sets
`OnPCEquip`; unequip resets it. `PCSkipEquip == 1` sets `OnPCEquip` and skips the
action, including when the shirt is already equipped. Stock book/ingredient/repair
exceptions remain in the shared mechanics.

[Equipment preparation](../../apps/openmw/mwworld/plainequipment.cpp) binds one
script through immutable stock-parsed declarations, copies locals into owned
candidate nodes, and validates current local identity, shape, values, content and
service binding before durability. The bounded scripted shirt requires single
items and integer equip/skip flags; executing item-script registrations reject
explicitly. No script instructions run. Unrelated short/long/float values remain
actor-local through staging, installation and restart.

The synthetic scripted variant retains actor A **40 → 49 → 40 → 49** Luck and
auto actor B **65 + 7 ability = 72 → 81 → 72 → 81**. The passive ability owns its
base contribution separately from the shirt modifier. Ordered initialized spells,
unrelated attribute and dynamic triples, signed counts and stable item identities
survive preparation and fresh recovery. Repeated refresh does not accumulate
effects. Skipped commands persist their local consequence and publish owned
`mSkipped` results with unchanged equipment/stats and no equipment-change event.

[Scripted equipment format 6](../../apps/tes3mp-server/native/equipment_codec.hpp)
adds stock `HLOC`/`LOCA` and typed local fields to the NPC stats/ability save.
Explicit declaration bindings allow at most 32 variables with 64-byte names.
Preflight validates exact names/order/counts/types and numeric ranges before
engine allocations. Restoration reuses strict `RefData`/`Locals` field readers.
Plain format 1, unscripted NPC format 5 and independent transfer format 4 retain
their layouts; incomplete NPC formats 2/3/4 still reject. No inferred migration.

Trusted caller matching, actor/content/registry checks and one canonical writer
remain intact. Durability precedes nonallocating installation and owned
publication. Safe failure permits retry; uncertainty preserves live state and
blocks both actors until fresh recovery. Restart replays no command success or
presentation event.

## Verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. Target `tes3mp_native_loadout_tests` final build
exit **0**: `build/logs/native-script-reviewed-build.log`. Individual filters below
all exit **0**; log names are relative to `build/logs`.

| Filter suffix (inventory-) | Evidence | Log |
|---|---|---|
| equipment-script-locals | shared stock local semantics | native-script-locals.log |
| equipment-scripted | 6 commits, 2 restarts, equipped/unequipped skips | native-scripted.log |
| equipment-scripted-guards | 17 atomic rejections, bounded preflight | native-scripted-guards.log |
| equipment-scripted-durability | 12 failures/recoveries/continuations | native-scripted-durability.log |
| equipment-scripted-allocations | 863 failures, 8 successes, zero leftovers | native-scripted-allocations.log |
| equipment-enchanted | 6 preserved commits | native-script-regression-enchanted.log |
| equipment-enchanted-guards | 70 rejections | native-script-regression-enchanted-guards.log |
| equipment-command | 8 plain commits | native-script-regression-plain.log |
| equipment-codec-guards | 3,350 rejections | native-script-regression-codec.log |
| transfer-command | 48 cases plus existing failure checks | native-script-regression-transfer.log |

Initial compile errors (exit 2) and test failures (exit 1: fixture types, old
script restriction, allocating assertions and retained plain export) were fixed
and rerun successfully. Valid prior verification was reused. Individual budget
and local-link checks exit **0**: `docs-budget.log`, `docs-links.log`.
No complete suites, expensive gates or upstream baseline tests ran.

## Limits

Declaration/local handling only: existing executing registrations, script
instructions, broader passive effects, other equipment categories, production
networking and complete world saves remain outside this operation. No real-loadout
scripted operation or live clients ran. Full NPC startup/saves remain unfinished.

Inherited limits include 64-node preparation/65-node saves, actor-scoped active
spell IDs, no durable request/notification deduplication, serialized writers,
stable immutable content/WorldModel lifetimes, private scratch files, rejected
postponed physics, broad headless linking/provenance debt and calling-thread C++
allocation tracking. Earlier M1/TR/networking evidence is unchanged.
