# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). One explicit-NPDT actor and one
  auto-NPDT actor share the constant Fortify Luck shirt operation. The auto actor
  also retains one fixed self-targeted race ability through preparation, equip,
  unequip, repeated refresh and fresh restart. M2/production integration remains
  incomplete.
- **Next action:** extract stock scripted-shirt `OnPCEquip`/`PCSkipEquip` local
  handling from `InventoryWindow::useItem` and the unequip context into explicit
  actor/script-local services for this same shirt; retain its constant effect
  and the auto actor's passive ability.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. Review
  and commit, summarize capability, limitations, verification and commit, then
  provide a ready-to-paste next-session prompt carrying workspace, reading,
  concrete scope, verification, commit and these session-scope requirements.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  implementation before the engine-backed pivot.

## Implemented behavior

[ActiveSpells](../../apps/openmw/mwmechanics/activespells.cpp) shares stock passive
membership discovery and active-spell insertion with the detached actor/content
path. [Fortify Attribute](../../apps/openmw/mwmechanics/spelleffects.cpp) application
and removal share base/modifier mutation. The bounded ability uses stock
SpellStore/AffectsBaseValues ownership and resistance flags; the shirt retains
its separate item identity and modifier. Neither passive initialization nor
repeated equipment refresh casts initialized spells or consumes RNG.

[EquipmentNpcStats](../../apps/openmw/mwworld/plainequipment.cpp) initializes base
spells, generated instance spells and race powers in stock order, then activates
at most one supported race ability. Generated selection still uses startup stats
before activation; runtime Intelligence deliberately excludes the generated
fixture spell if selection is incorrectly repeated. Explicit/auto-NPDT formulas,
skills, dynamic initialization and immutable membership remain unchanged. Missing
content, other passive types, non-race abilities and competing NPC custom-data
writers reject explicitly.

Synthetic actor A retains **40 → 49 → 40 → 49** Luck. Actor B starts with
**65 + 7 ability = 72**, then **72 → 81 → 72 → 81**. The ability changes base
Luck; only the shirt changes its modifier. All unrelated attribute
base/modifier/damage and dynamic base/modifier/current triples survive preparation,
installation and restart. Refresh preserves both owners without accumulating
magnitudes.

[NPC equipment format 5](../../apps/tes3mp-server/native/equipment_codec.hpp)
requires `ABMG`, the already-applied ability magnitude, alongside ordered
initialized spell IDs and the existing NPC stat arrays. Its source is the single
race ability in membership. Fixed-storage preflight checks the witness against
content before engine allocations. Saved base Luck already includes that
contribution: fresh reconstruction retains the validated ability owner while
restoring saved bases, then reconstructs the shirt modifier. Formats 2/3/4 reject
explicitly; no guessed activation/default migration. Plain format 1 and independent
transfer format 4 retain their behavior.

Trusted caller matching, actor/content/registry validation and stale state checks
precede persistence. Durability precedes nonallocating installation and owned
success publication. Safe file failure permits retry; uncertainty preserves live
state and blocks both actors until fresh recovery. Restart emits no command
success or presentation replay.

## Verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. `tes3mp_native_loadout_tests` build exit **0**,
`build/logs/native-ability-final-build.log`. Individual filters all exit **0**;
logs below are under `build/logs`, prefixed `native-ability-`:

| Filter | Evidence | Log suffix |
|---|---|---|
| inventory-equipment-npc-initialization | stock initialization, passive refresh | initialization.log |
| inventory-equipment-enchanted | 6 isolated commits, combined Luck | enchanted.log |
| inventory-equipment-enchanted-guards | 70 atomic rejections | guards.log |
| inventory-equipment-enchanted-durability | 12 failures/recoveries/continuations | durability.log |
| inventory-equipment-enchanted-allocations | 671 failures, 6 successes, zero leftovers | allocations.log |
| inventory-equipment-command | 8 plain commits | plain-command.log |
| inventory-equipment-codec-guards | 3,350 rejections | plain-codec-guards.log |
| inventory-transfer-command | 48 cases plus existing failure checks | transfer-command.log |

No build/test failures occurred. Valid verification was reused after adding
focused guard cases. Individual documentation budget/local-link checks exit **0**:
`build/logs/docs-budget.log`, `build/logs/docs-links.log`.
No complete suites, expensive gates or upstream baseline tests ran.

## Limits

This supports one fixed self Fortify Luck race ability and shirt, not general
passive activation or full NPC startup/saves. Other abilities, diseases/curses,
casting, mutable skills/known spells, used-power time, factions, actor scripts,
AI/death, other equipment categories, production networking and complete world
saves remain outside scope. No real-loadout enchanted operation or live clients ran.

Inherited limits: 64-node preparation/65-node saves, actor-scoped active-spell IDs,
no durable request/notification deduplication, serialized writer, stable immutable
content/WorldModel lifetimes, private scratch files, rejected postponed physics,
broad headless linking/provenance debt and calling-thread C++ allocation tracking.
Earlier M1/TR/networking evidence is unchanged.
