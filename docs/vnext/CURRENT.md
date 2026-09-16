# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The app-local constant Fortify Luck
  shirt operation now uses explicit actor-bound stock NPC stat contexts. Two
  actors retain unrelated attributes and dynamic values through preparation,
  durable installation and fresh restart. M2 and production integration remain
  incomplete.
- **Next action:** extract stock autocalculated NPC attribute/skill initialization
  with explicit content/services for this same enchanted-shirt stat context;
  preserve generated-spell semantics or identify the exact remaining service
  blocker before admitting an auto-NPDT actor.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. Review
  and commit, summarize capability, limitations, verification and commit, then
  provide a ready-to-paste next-session prompt carrying workspace, reading,
  concrete scope, verification, commit and these session-scope requirements.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented behavior

[Stock NPC initialization](../../apps/openmw/mwclass/npc.cpp) now shares its
explicit-NPDT branch with [NpcStats](../../apps/openmw/mwmechanics/npcstats.cpp).
NpcStats accepts explicit content. Stock attribute/magicka recalculation accepts
an explicit multiplier, retaining the existing single-player wrappers. No
Environment, world, UI or null gameplay services are installed by this harness.

[EquipmentNpcStats](../../apps/openmw/mwworld/plainequipment.hpp) owns one fresh
NpcStats instance, an actor lifetime/identity and a content binding. It exposes
read-only stats; the fixture is its sole writer. NPC custom data is rejected to
prevent a competing writer. This context initializes living explicit NPDT stats,
including skills, level, disposition and reputation; it is not full NPC startup.

Preparation reconstructs protected stats through stock field readers and setters,
then derives actor/item-owned ActiveSpells and MagicEffects. All eight attribute
base/modifier/damage triples and three dynamic base/modifier/current triples are
preserved except the intended Luck modifier. Synthetic actors still observe
**40 → 49 → 40 → 49** and **65 → 74 → 65 → 74**. Repeated mechanics refresh does
not double the bonus. Fixtures include damaged/modified attributes,
above-maximum magicka and negative fatigue. The other actor retains exact
inventory, stats, effects and listeners.

[Equipment format 3](../../apps/tes3mp-server/native/equipment_codec.hpp) adds
mandatory NPC base identity and fixed attribute/dynamic arrays. Format 1 remains
plain-only; incomplete Luck-only format 2 is deliberately rejected, with no
inferred/default-value migration. Decode preflights bounded fields and known IDs
before engine allocation. Restart binds the saved NPC base to the receiving
actor, reconstructs the fixed effect, and checks both fresh-source and staged
stats before installation. Initial restart stats deliberately differ from the
saved runtime values. No presentation or success is replayed.

Trusted caller, actor/content, registry, inventory, stat and effect validation
precede persistence. Stale unrelated attributes/dynamics, changed actor ownership
and mismatched restart candidates reject atomically. Durability still precedes
nonallocating inventory/stat swaps and owned success publication. Safe I/O failure
permits retry; uncertain writes preserve live state and block both actors until
fresh recovery. Plain shirts, transfer format 4 and engine-independent networking
remain intact.

## Verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. `tes3mp_native_loadout_tests` build exit **0**,
`build/logs/native-npc-final-build.log`. Filters ran individually; valid earlier
checks were reused when subsequent edits did not affect their paths.

| Filter | Exit / evidence | Log under build/logs |
|---|---|---|
| inventory-equipment-enchanted | 0; 6 isolated commits, full stat preservation | native-npc-enchanted.log |
| inventory-equipment-enchanted-guards | 0; 40 atomic rejections | native-npc-guards.log |
| inventory-equipment-enchanted-durability | 0; 12 failures/recoveries/continuations | native-npc-durability.log |
| inventory-equipment-enchanted-allocations | 0; 607 failures, 6 successes, zero leftovers | native-npc-allocations.log |
| inventory-equipment-command | 0; 8 plain commits | native-npc-plain-command.log |
| inventory-equipment-codec-guards | 0; 3,350 rejections | native-npc-plain-codec-guards.log |
| inventory-equipment-command-recovery | 0; 32 recoveries, 128 continuation commits | native-npc-plain-recovery.log |
| inventory-transfer-command | 0; 48 cases plus existing failure checks | native-npc-transfer-command.log |

Failures were stopped, fixed and rerun: fixture ID compile errors (exit 2), GMST
variant and dormant-item fixture errors (exit 1), incremental-link LNK1163
(exit -1; deleting only the generated .ilk resolved it), and an allocating
assertion inside fault injection (exit 1). No complete suites, expensive gates or
upstream baseline tests ran. Individual documentation budget/local-link checks
exit **0**: `build/logs/docs-budget.log`, `build/logs/docs-links.log`.

## Limits

Autocalculated NPCs, death/time services, full NPC custom data, race powers,
known/generated spells, AI, scripts, other equipment categories, variable
enchantments/RNG, production networking and complete actor/world saves remain
outside this context. Skills and other initialized NPDT fields are not mutable
or durably saved here. No real-loadout enchanted operation or live clients ran.

Inherited limits: 64-node preparation/65-node saves, actor-scoped equipment spell
IDs, no durable request/notification deduplication, serialized writer, stable
content/WorldModel lifetimes, private scratch files, rejected postponed physics,
and broad headless linking/provenance debt. Allocation tracking covers C++
allocations on the calling thread. Earlier M1/TR/networking evidence is unchanged.
