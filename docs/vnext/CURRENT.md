# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The app-local harness now equips and
  unequips a constant-effect Fortify Luck shirt for two distinct actors, with
  durable, isolated gameplay consequences. M2 and production integration remain
  incomplete.
- **Next action:** replace the Luck-only fixture stat composition with an explicit
  actor-bound stock NPC stat context for this same enchanted-shirt operation;
  preserve unrelated attributes/dynamic values through preparation, installation
  and restart. Inspect NPC initialization before extracting its required services.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize capability, limitations, verification and commit,
  then provide a ready-to-paste next-session prompt carrying workspace, reading,
  concrete scope, verification, commit and these session-scope requirements.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented behavior

[ActiveSpells](../../apps/openmw/mwmechanics/activespells.cpp) shares stock
constant-equipment discovery and equipped-item membership checks with its explicit
actor service. Stock FortifyAttribute and the bounded service share the actual
[attribute modifier](../../apps/openmw/mwmechanics/spelleffects.cpp), including
CreatureStats mutation. CreatureStats now accepts explicit content; its existing
single-player constructor delegates to that path.

The supported operation is one non-scripted shirt with one positive, fixed,
self-targeted constant Fortify Luck effect. The synthetic harness observes actor
A's Luck **40 → 49 → 40 → 49** and actor B's **65 → 74 → 65 → 74**, with matching
MagicEffects and actor/item-owned ActiveSpells. Mechanics refresh does not apply
the bonus again. Unequip removes the effect using its applied magnitude. Other
actors retain exact inventory, stat, effect and listener state.

[PreparedPlainEquipment](../../apps/openmw/mwworld/plainequipment.cpp) opts into
protected Luck stats while retaining the existing plain path. Fresh candidate
stats and active effects are isolated from canonical state. Trusted caller,
registry, inventory, content, actor-stat ownership and applied-effect validation
precede durability. The existing file commit installs inventory and stats using
nonallocating swaps, then publishes owned IDs and Luck values through
[EquipmentSuccess](../../apps/tes3mp-server/native/equipment_command.hpp).
No borrowed pointers or iterators cross publication or persistence boundaries.

[Equipment codec](../../apps/tes3mp-server/native/equipment_codec.hpp) keeps format
1 plain-only. Explicit format **2** adds mandatory Luck base/modifier/damage;
its modifier must match the equipped fixed enchantment. Restart derives the
active effect from saved equipment and bound content, verifies the saved stat,
and installs without replaying presentation/success. This is a bounded equipment
and Luck save, not a complete actor save. Variable magnitudes/RNG are unsupported.

Safe I/O failure permits retry. Uncertain writes preserve live state and block
both actors, including fresh sinks; recovery destroys the old composition before
restoring a complete prior/new file and continuing. Plain-shirt behavior, transfer
format 4, one canonical writer and engine-independent networking remain intact.

## Fresh verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. Final `tes3mp_native_loadout_tests` build exited
**0**: `build/logs/native-enchanted-final-build.log`. Filters ran individually:

| Filter | Exit / evidence | Log under build/logs |
|---|---|---|
| inventory-equipment-enchanted | 0; 6 isolated commits and repeated refresh | native-enchanted.log |
| inventory-equipment-enchanted-guards | 0; 23 atomic rejections | native-enchanted-guards.log |
| inventory-equipment-enchanted-durability | 0; 12 failures, fresh recoveries and continuations | native-enchanted-durability.log |
| inventory-equipment-enchanted-allocations | 0; 471 injected failures, zero leftovers, 6 successes | native-enchanted-allocations.log |
| inventory-equipment-command | 0; 8 plain commits | native-enchanted-plain-command.log |
| inventory-equipment-codec-guards | 0; 3,350 rejections | native-enchanted-plain-codec-guards.log |
| inventory-equipment-command-recovery | 0; 32 recoveries, 128 continuation commits | native-enchanted-plain-recovery.log |
| inventory-transfer-command | 0; 48 cases and existing failure checks | native-enchanted-transfer-command.log |

Earlier failures were fixed before proceeding: missing test include (build 2),
missing scratch argument (test 1), a stale generated object causing LNK1163
(build -1; recompilation passed), and a guard that incorrectly used readState to
clear active spells (test 1). No complete suites, expensive gates or upstream
baseline tests ran.

Documentation budget and local links: individual checks, exit **0**, logs
`build/logs/docs-budget.log` and `build/logs/docs-links.log`.

## Limits and inherited evidence

Test-owned actor stats cover Luck only. Full NPC initialization/state, other
spells, variable enchantments, scripts, other equipment categories, production
networking and complete world saves remain deferred. Active-spell IDs are scoped
to the actor's single equipment source; this is not global spell-ID allocation.
No real-loadout enchanted operation or live clients were exercised here.

Inherited limits remain: 64-node preparation/65-node saves, no durable request
or notification deduplication, serialized writer and stable content/WorldModel
lifetimes, private scratch storage, rejected postponed physics, and broad
headless linking/provenance debt. Allocation tracking covers C++ allocations on
the calling thread. Earlier M1/TR and networking evidence remains unchanged.
