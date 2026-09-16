# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). One explicit-NPDT actor and one
  eligible auto-NPDT actor now share the constant Fortify Luck shirt operation.
  Initialized spells and unrelated attributes/dynamics survive preparation,
  durable installation and fresh restart. M2/production integration is incomplete.
- **Next action:** extract stock passive race-ability discovery/activation from
  `ActiveSpells::update`/`initParams` with explicit actor/content services for one
  fixed self-targeted Fortify Luck ability alongside this same shirt.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. Review
  and commit, summarize capability, limitations, verification and commit, then
  provide a ready-to-paste next-session prompt carrying workspace, reading,
  concrete scope, verification, commit and these session-scope requirements.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  implementation before the engine-backed pivot.

## Implemented behavior

[Stock NPC startup](../../apps/openmw/mwclass/npc.cpp) shares auto-NPDT
attribute/skill initialization with [NpcStats](../../apps/openmw/mwmechanics/npcstats.cpp).
Race, sex, class bonuses, stock rounding, health, level, disposition and reputation
are retained. Explicit-NPDT initialization is unchanged. Explicit content and
magicka policy avoid Environment/world/UI services in the equipment context.

[Spell autocalculation](../../apps/openmw/mwmechanics/autocalcspell.cpp) and spell
cost accept explicit content/settings. Stock wrappers share the formulas; record
traversal order, school-cap behavior and eligibility remain stock. Fixed-cost
records retain their service-free path. A concrete synthetic fixture checks
attributes, skills, dynamic values and changed-GMST spell selection.

[EquipmentNpcStats](../../apps/openmw/mwworld/plainequipment.hpp) owns fresh stock
stats and instance spell membership, with actor lifetime/identity and content
bindings. The fixture remains the sole writer; NPC custom data rejects a competing
writer. Base spells, generated spells and race powers initialize in stock order,
without changing base records or subscribing to a shared SpellList. Only known
spells/powers are admitted; missing records and passive abilities reject explicitly.
Auto actors require race/class content, level >=1 and positive bounded race
attributes, preventing entry into unsupported death/time services.

Both actors retain all eight attribute base/modifier/damage triples and three
dynamic base/modifier/current triples except the intended Luck modifier.
Observed sequences remain **40 → 49 → 40 → 49** and **65 → 74 → 65 → 74**.
The auto actor's runtime Intelligence would exclude its generated spell if
selection were rerun from runtime stats; startup membership survives every stage.
Skills and other immutable startup fields reconstruct from the same content.

[Equipment format 4](../../apps/tes3mp-server/native/equipment_codec.hpp) adds
mandatory ordered initialized spell IDs (maximum 256) to NPC base identity and
attribute/dynamic arrays. Fixed preflight storage validates count, known IDs,
uniqueness and field order before engine allocations. Incomplete NPC formats 2/3
reject explicitly; no inferred initialization/default-value migration. Plain
format 1 and independent transfer format 4 retain their behavior. Restart compares
saved membership with fresh startup initialization before restoring runtime stats.

Trusted caller matching, actor/content/registry validation, stale-stat and spell
membership checks precede persistence. Durability precedes nonallocating swaps
and owned success publication. Safe I/O failures permit retry; uncertain writes
preserve live state and block both actors until fresh recovery. Restart neither
replays presentation nor publishes command success.

## Verification

Windows MSVC, `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-16. Target `tes3mp_native_loadout_tests` build exit
**0**, `build/logs/native-auto-final-build.log`. Individual filters, all exit **0**:

| Filter | Evidence | Log under build/logs |
|---|---|---|
| inventory-equipment-npc-initialization | concrete stats, spell eligibility/GMST | native-auto-initialization.log |
| inventory-equipment-enchanted | 6 isolated commits, preserved startup spells | native-auto-enchanted.log |
| inventory-equipment-enchanted-guards | 56 atomic rejections | native-auto-guards.log |
| inventory-equipment-enchanted-durability | 12 failures/recoveries/continuations | native-auto-durability.log |
| inventory-equipment-enchanted-allocations | 649 failures, 6 successes, zero leftovers | native-auto-allocations.log |
| inventory-equipment-command | 8 plain commits | native-auto-plain-command.log |
| inventory-equipment-codec-guards | 3,350 rejections | native-auto-plain-codec-guards.log |
| inventory-transfer-command | 48 cases plus existing failure checks | native-auto-transfer-command.log |

Stopped/fixed/reran: missing Race include (build exit 2), zero-duration fixture
spell failing to distinguish runtime regeneration (test exit 1). Valid checks
were reused after nonsemantic cleanup and the fixed-cost wrapper correction.
Individual documentation budget/local-link checks exit **0**:
`build/logs/docs-budget.log`, `build/logs/docs-links.log`.
No complete suites, expensive gates or upstream baseline tests ran.

## Limits

This is bounded stat/spell initialization, not full NPC startup. Passive
abilities/diseases/curses require stock activation services; casting, used-power
time state, mutable skills/known spells, factions, actor scripts, AI, death,
other equipment categories, production networking and complete actor/world saves
remain outside scope. No real-loadout enchanted operation or live clients ran.

Inherited limits: 64-node preparation/65-node saves, actor-scoped equipment spell
IDs, no durable request/notification deduplication, serialized writer, stable
immutable content and WorldModel lifetimes, private scratch files, rejected
postponed physics, broad headless linking/provenance debt, and calling-thread C++
allocation tracking. Earlier M1/TR/networking evidence is unchanged.
