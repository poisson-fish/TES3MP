# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). M2's bounded headless exit is met;
  retirement follows production caller cutover.
- **Next action:** replace the operator-selected base container/position and manual
  desktop mapping with an OpenMW-derived placed reference and matching content
  identity. Preserve the now-verified native production put/take, reconnect and
  durable restart path. M3's TR/shared-world exit remains unfinished.
- **Scope:** one bounded slice at a time; inspect, implement, verify narrowly,
  review and commit. No additional planning files.

## Implemented behavior

`tes3mp_server` accepts `native_inventory_file` and owns loaded OpenMW content plus
one persistent [InventoryService](../../apps/tes3mp-server/native/inventory_service.hpp).
Authenticated intake binds server-owned player/entity/session/generation. Engine
inventory images and command dispositions share the existing canonical file
transaction; durability precedes installation and publication. Native mode excludes
legacy inventory/combat/character-creation writers. Format **6** requires matching
native configuration and explicitly rejects older development saves.

Registered players can now join in either order: canonical insertion preserves
sorted PlayerIds. Restart rebases scheduler deadlines to the new monotonic epoch
while preserving durable tick numbers, 30 Hz cadence, bounded catch-up and checked
exhaustion. Previously, recovery at tick 20,032 delayed simulation for about eleven
minutes despite successful joins. Fixed-label connection diagnostics expose failures
without credentials or payloads.

Desktop put/take recognizes the visible inventory's proxy models through their
existing ownership query. Native reach validation now converts the bounded
384-unit radius to the desktop protocol's 1024 position quanta per OpenMW unit.
The prior leaf-model cast prevented intent capture; the prior reach constant
rejected ordinary nearby interactions.

## Verification

Windows MSVC `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, desktop automation enabled. Affected `openmw`,
`tes3mp_server`, `tes3mp_native_loadout_tests` and
`tes3mp_deterministic_facilities_tests` builds exit **0**. Build logs under
`build/logs`: `native-gui-build.log`, `native-recovery-build.log`,
`native-reach-build.log` and `native-application-rebuild.log`.

Individual checks, all exit **0**:

| Check | Evidence | Log in build/logs |
|---|---|---|
| deterministic-facilities `scheduler` | fresh recovery epoch, cadence, bounded catch-up, overflow/exhaustion | native-scheduler-test.log |
| native `inventory-application` | reverse join order, production authentication, recovery with a fresh clock, continued delivery | native-desktop-application.log |
| native `inventory-service` | trusted binding, reach boundary/rejection, transfer and recovery | native-desktop-service.log |
| desktop `transfer` | actual GUI put/take and both session resumes | native-desktop-transfer.log |
| desktop `restart` | both relaunched clients recover and continue taking | native-desktop-restart.log |
| original mapped save `restart` | accumulated-tick recovery without resetting that save | native-desktop-original-restart.log |
| documentation budget / local links | individual methods | docs-budget.log / docs-links.log |

The [capture driver](../../scripts/capture_native_inventory_desktop.py) runs actual
OpenMW clients over localhost transport against the production native server.
Loadout: Morrowind.esm, Imperial Prison Ship, common_shirt_01, barrel_01 placed
reference **299164:1**, wire item/container **70/90**. The testing-only driver opens
the real container window and invokes item selection, quantity-dialog and drop
callbacks through the normal desktop input interceptor. It checks visible GUI
item models against committed baselines; it uses no screen control or screenshots.
Pixel layout and manual mouse interaction are not claimed.

Evidence under `build/native-desktop-mapped/transfer-final` shows initial player
counts **3/5**, empty barrel; putting two gives **1/5/2**; taking one gives
**1/6/1**. Both clients resume once with stable identity and those counts.
`restart-final` starts fresh processes using the same canonical save (matching
before/after SHA-256 evidence), observes **1/6/1**, then reaches **1/7/0**.
The server is terminated between phases; no shutdown save is needed.
`original-restart-final` also passes against the original mapped save.
Only the repeatable transfer campaign uses a separate prepared runtime directory;
the original committed save was preserved and continued.

## Limits

The server still seeds actor counts and an empty base container from a trusted
operator descriptor; the desktop maps that identity to a real placed barrel.
Automatic placed-reference import, base inventory population and desktop-pack
mapping remain missing. Actor bases are selected explicitly; complete gameplay
resource identity is unfinished. No TR or full M3 world proof is claimed.

Inherited native preparation/durability/atomic-failure probes remain applicable
but were not broadly rerun. Gameplay-active desktop inventory rebuild retirement,
broader items/scripts/combat/world saves, authoritative movement and headless
link/provenance cleanup remain pending. No complete suites or expensive gates ran.
