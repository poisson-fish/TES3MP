# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). M2's bounded headless exit is met;
  retirement follows production caller cutover. Equipped-shirt drop is not an
  integration blocker.
- **Next action:** bind the native host's shared-container identity to an actual
  OpenMW placed reference and the existing desktop content map, then exercise
  plain-shirt put/take with two authenticated desktop clients. Preserve the
  installed production command/durability path; do not build another rehearsal.
- **Scope:** 2–3 related bounded slices sequentially; inspect, implement, verify
  narrowly, review and commit. This change continues fda7739deb15. No planning files.

## Production capability

`tes3mp_server` now accepts `native_inventory_file`, constructing an
[InventoryHost](../../apps/tes3mp-server/native/inventory_host.hpp) that owns loaded
OpenMW content/readers and one persistent
[InventoryService](../../apps/tes3mp-server/native/inventory_service.hpp).
The bounded trusted descriptor names two already registered established PlayerIds,
NPC bases, one plain shirt, an empty base container, initial counts and wire
mappings. Registered entity/appearance identity, role order, actual ordered content
fingerprints and encoding bind recovery. The operator explicitly attests that this
loadout corresponds to the authenticated manifest; automatic desktop-pack mapping
and complete gameplay-resource identity are still missing.

Authenticated intake resolves server-owned player/entity/session/generation.
`CanonicalCommandReducer` prepares the engine transaction after existing order,
retry and authority checks. The existing coherent engine image (both actors and
container) and command dispositions share one `CanonicalPersistenceFile` record.
File durability precedes native installation and canonical publication. Existing
engine field serializers and file replacement are reused; there is no parallel
snapshot or CanonicalInventoryWorld mirror. Join, resume, resync, rejected/empty
ticks and journal compaction retain the same native domain.

One native mutation may be accepted per tick; later native intents are durably
rejected in ingress order. Rejected durability permits retry; uncertainty closes
the native runtime. Stale/consumed preparations cannot reach durability. Existing
owned baselines/queues deliver staged native values only after commit.

Native mode rejects simultaneous legacy inventory, combat or character-creation
configuration. Old writers remain only for unmigrated compositions. Canonical save
format **6** rejects older development saves explicitly; no automatic migration or
silent reset exists. Native saves require the matching configured service.

## Verification

Windows MSVC `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`. Builds exit **0**: `tes3mp_native_loadout_tests`,
`tes3mp_server`, `openmw_tes3mp_desktop_providers`, and affected server-app,
canonical-persistence and server-scripting test executables. Logs in `build/logs`:
`native-final-build.log`, `native-production-build.log`, `native-client-build.log`,
`native-affected-tests-build.log`. The broad test executables were built, not run.

Individual native filters, all exit **0**; logs in `build/logs`:

| Filter | Evidence | Log |
|---|---|---|
| inventory-canonical | joint image/dispositions; ordering, retry/stale rejection, dual-writer/domain-loss guards, recovery/continuation | native-canonical-test.log |
| inventory-application | production authentication/registered credentials, put/take, late join, both deliveries, retry, resync, resume, compaction; fresh recovery and continued take | native-application-test.log |
| inventory-host | real Morrowind.esm, player/player, common_shirt_01, barrel_01 through the same host/application and recovery path | native-host-real.log |
| inventory-service-durability | safe retry, uncertain closure, coherent recovery | native-service-durability.log |
| inventory-equipment-container-prepared | detached staging, retry, consumed/stale rejection | native-prepared-test.log |
| inventory-equipment-container-state | PCSkipEquip, constant effects, abilities, initialized spells, unrelated stats, isolated locals, atomic recovery | native-service-state.log |
| inventory-equipment-container-allocations | 1,072 atomic failures; no retained/post-acceptance allocations | native-service-allocations.log |
| inventory-equipment-container-recovery | eight failure/recovery cases | native-service-recovery.log |
| inventory-equipment-connected | transfer/equip/return/recovery preserved | native-service-transfer.log |

Application evidence uses synthetic transport and registered profiles; the host
filter uses real engine content. Neither is actual socket/desktop presentation
evidence. No two-desktop or TR run is claimed. Documentation budget and local links
pass individually, exit **0** (`docs-budget.log`, `docs-links.log`). No full suites
or expensive gates.

Earlier setup/build failures were fixed and rerun: CMake dependency declaration
(1), startup declaration order (2), scheduler/test preparation and established
profile fixtures (1), synthetic queue time causing a crash (-1073741819), missing
authentication registry in the continuation fixture (-1073740791).

## Limits

The missing production operation is **placed-container bootstrap and matching
desktop reference/item mapping**. The descriptor seeds actor shirt counts and an
empty base container; it does not import placed references or base inventory lists.
Native actor bases are explicitly selected, not derived from character profiles.
Gameplay-active desktop rebuild retirement is still pending verified client cutover.
Wire intake covers plain-shirt put/take; broader scripts, categories, combat and
complete world saves remain outside scope. Inherited bounds include fixed actor
roles, bounded nodes/effects, actor-scoped spell IDs, private save directories,
rejected postponed physics, client-reported movement and broad headless link/provenance debt.
