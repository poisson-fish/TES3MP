# TES3MP vNext

TES3MP vNext is a clean-break multiplayer architecture for Morrowind, built on
OpenMW 0.51. It replaces the TES3MP 0.8.x protocol, transport, server, and
scripting architecture rather than porting those systems forward.

The project is under active development and is not yet a playable replacement
for TES3MP 0.8.x. Phases 0–6 are complete. Phase 7 is building the first
end-to-end headless multiplayer flow; disconnect/resume composition and the
adverse-network test matrix are the remaining slices.

## Start here

- [vNext overview](docs/vnext/README.md) — product scope, architecture, current status, compatibility policy, and repository workflow
- [Implementation plan](docs/vnext/IMPLEMENTATION_PLAN.md) — authoritative phase and slice tracker, decision register, and exit gates
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

The flow must pass deterministic adverse-network and soak tests before Phase 7
can close. OpenMW desktop integration begins in Phase 8; PC VR interoperability
follows in Phase 9.

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
