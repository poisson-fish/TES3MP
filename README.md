# TES3MP vNext

TES3MP vNext is a clean-break multiplayer architecture for Morrowind, built on
OpenMW 0.51. It replaces the TES3MP 0.8.x protocol, transport, server, and
scripting architecture rather than porting those systems forward.

The project is under active development and is not yet a playable replacement
for TES3MP 0.8.x. Phases 0–9 are complete: the headless, OpenMW desktop, and PC
VR vertical slices now share one bounded protocol, client runtime, and
authoritative server path. Phase 10 player/content identity discovery is next.

## Start here

- [vNext overview](docs/vnext/README.md) — product scope, architecture, current status, compatibility policy, and repository workflow
- [Rolling implementation plan](docs/vnext/IMPLEMENTATION_PLAN.md) — authoritative Now / Next / Later tracker
- [Implementation notes](docs/vnext/IMPLEMENTATION_NOTES.md) — chronological implementation, verification, and owner-review history
- [Local baseline build](docs/vnext/LOCAL_BASELINE_BUILD.md) — supported local configure, build, and test workflow
- [Legacy gameplay inventory](docs/vnext/LEGACY_GAMEPLAY_FEATURE_INVENTORY.md) — reference-only inventory of TES3MP 0.8.x behavior

## Current milestone

The first milestone is a dedicated server with two deterministic headless clients that can:

1. negotiate and authenticate;
2. join with distinct server-issued identities;
3. transition between fixed interior and exterior fixtures;
4. observe one another only while in the same fixture cell;
5. exchange semantic movement commands and authoritative snapshots; and
6. disconnect, resume within a bounded grace period, and expire cleanly.

That first flow, its OpenMW desktop composition, and the PC VR interoperability
gate are complete. The next pass replaces fixture-only player/content identity
assumptions without prematurely specifying the rest of the gameplay roadmap.

## Compatibility

vNext intentionally does not preserve TES3MP 0.8.x wire compatibility,
mixed-version peers, RakNet or CrabNet integration, the legacy server/CoreScripts
API, legacy saves, or the old engine patch set. Archived TES3MP code may be used
to understand historical gameplay requirements, but it is not an implementation
template.

## Baseline and license

The active source baseline is OpenMW 0.51.0 at
`f4bec41444214a7903bebd178389ca22ca13f646`. Intentional differences are tracked
by [`docs/vnext/BASELINE_PROVENANCE.json`](docs/vnext/BASELINE_PROVENANCE.json)
and verified with:

```sh
python scripts/verify_vnext_baseline.py
```

TES3MP vNext and its OpenMW baseline are distributed under the GNU General
Public License version 3. See [LICENSE](LICENSE). Third-party assets retain the
licenses documented alongside those assets.
