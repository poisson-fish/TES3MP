# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Active milestone:** M1, native loadout and headless runtime probe in
  [PLAN.md](PLAN.md). Not started.
- **Next action:** inspect the existing load/configuration path and CMake owners
  below; add the smallest app-local executable that loads the real configured
  TES3 loadout through OpenMW and enumerates normalized winning records without
  constructing rendering or UI. Begin with loading, not protocol redesign.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the previous
  timed/area-magic implementation and its documentation before this pivot.
- **Current changes:** the old roadmap and content-format manual are replaced;
  root guidance and the documentation guard now route fresh sessions here.
  Production gameplay/build behavior has not changed in this preparation.
- **Blockers:** none established for starting code work. Discover the user's
  actual OpenMW configuration/content paths locally; do not assume Tamriel
  Rebuilt is installed. Missing content prevents claiming the real-loadout proof,
  but does not prevent implementing the loader or synthetic failure cases.

## Existing code to reuse or migrate

| Owner | Actual state / intended treatment |
|---|---|
| [Independent libraries](../../components/tes3mp/CMakeLists.txt) | Protocol, transport, authentication, sessions, IDs/revisions, command intake, queues, snapshots, and reconnect are reusable foundations. |
| [Server application](../../apps/tes3mp-server/server_application.cpp) | Currently composes separate canonical gameplay worlds and staged output/durability. Keep working while introducing the engine runtime. |
| [Inventory world](../../components/tes3mp/server_core/inventory_world.cpp), [combat world](../../components/tes3mp/server_core/combat_world.cpp) | Implement a bounded subset, including inventory transfers/equipment and melee/timed magic. Migration scaffolding, not the new source of general OpenMW behavior. |
| [Reducer](../../components/tes3mp/server_core/server_command_reducer.cpp) | Prepares candidate worlds and requires durability before install/publication. Preserve the contract; do not assume copied OpenMW objects provide the same isolation. |
| [Desktop provider](../../apps/openmw/tes3mp/desktop_providers.cpp) | Captures intent and reconstructs committed state in OpenMW. Retain connection/presentation integration; replace gameplay-active reconstruction as authority moves. |
| [Baker](../../scripts/bake_tes3mp_content.py), [fixture content](../../files/data/tes3mp) | Raw ESM/config parsing and selected catalogs support the old runtime. Keep packaging/hash utilities; replace semantic parsing and hand-enumerated gameplay inputs after M1. |
| [Server scripting](../../components/tes3mp/server_core/server_scripting.cpp) | Bounded custom command/variable runtime exists. It is not MWScript/OpenMW Lua compatibility and must not become a second quest implementation. |

Current limitations include client-authoritative player positions, a narrow
fixture world, partial actor AI/effects, incomplete resource identity, and no
demonstrated general mod-script or shared-quest compatibility. The new native
server runtime and Tamriel Rebuilt two-player proof do not exist yet.

## M1 source entry points

- [ConfigurationManager](../../components/files/configurationmanager.cpp),
  [World::loadData/loadContentFiles](../../apps/openmw/mwworld/worldimp.cpp),
  [EsmLoader](../../apps/openmw/mwworld/esmloader.cpp),
  [ESMStore](../../apps/openmw/mwworld/esmstore.cpp).
- [Store initialization test](../../apps/openmw_tests/mwworld/teststore.cpp):
  demonstrates loading without constructing a rendered World; its broad link
  target does not establish low dependency weight.
- [OpenMW CMake](../../apps/openmw/CMakeLists.txt),
  [components CMake](../../components/CMakeLists.txt),
  [server CMake](../../apps/tes3mp-server/CMakeLists.txt): existing targets have
  different dependency constraints; add an explicit runtime/probe leaf.
- [ContainerStore](../../apps/openmw/mwworld/containerstore.cpp),
  [InventoryStore](../../apps/openmw/mwworld/inventorystore.cpp),
  [ActiveSpells](../../apps/openmw/mwmechanics/activespells.cpp),
  [spell formulas](../../apps/openmw/mwmechanics/spellutil.cpp): next probe owners.

Source inspection found that inventory addition touches the local player,
scripts, and UI; constant enchantments depend on listener presence; copies retain
external references. Null stubs or a successful plain-item transfer cannot prove
behavior-preserving isolation. OpenMW already calls several independent melee
helpers in [melee_combat.cpp](../../components/tes3mp/protocol/melee_combat.cpp).

## Verification

The preparation checkpoint was not built or tested in this session. Its old
documentation reported Windows magic tests/build/live evidence; that historical
claim is not fresh validation of either this checkout or the new architecture.
Fresh validation on 2026-09-13: both individual methods in
`scripts/tests/test_vnext_documentation.py` passed (exit 0): the document
allowlist/word-budget check and the local-link check. Logs are local ignored
`build/logs/vnext-pivot-docs-budget.log` and `vnext-pivot-docs-links.log`.
No C++ tests, builds, full suites, or baseline gates were run for this reset.

Known inherited issue: baseline provenance coverage was previously reported
stale, including retired workflow paths. No broad baseline gate was rerun.
Keep machine registry edits scoped to touched paths; do not label the baseline
green or launch a full suite just to reconstruct old evidence.

Replace this handoff after each implementation slice. Keep only current behavior,
unresolved blockers, exact narrow verification results, and the next action.
This file is capped at 850 words; completed-session history belongs in Git.
