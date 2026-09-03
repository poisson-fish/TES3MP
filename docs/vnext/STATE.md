# vNext working state

Updated: 2026-09-03

Read this first, then only the linked active material. The authoritative tracker
remains the [implementation plan](IMPLEMENTATION_PLAN.md#phase-8--openmw-desktop-vertical-slice).

## Current position

- Branch: `vnext`
- Phase 7: **Implemented**
- Phase 8: **In Progress**
- Slice 8.1: **Implemented**
- Slice 8.2: **In Progress** (owner demo remains)
- Slice 8.3: **In Progress**
- Slice 8.4: **In Progress**
- Slice 8.5: **In Progress** (owner desktop demo remains)
- Slice 8.6: **In Progress** (owner desktop demo remains)
- Slice 8.7: **In Progress** (content-backed owner demo remains)
- Governing decisions: [ADR-0007](adr/ADR-0007-openmw-hook-patch-queue-policy.md),
  [ADR-0047](adr/ADR-0047-phase8-adapter-lifecycle-and-provider-boundary.md),
  [ADR-0048](adr/ADR-0048-canonical-revision-and-simulation-tick-separation.md),
  [ADR-0049](adr/ADR-0049-phase8-desktop-connection-composition.md),
  [ADR-0050](adr/ADR-0050-phase8-cell-and-remote-presentation.md),
  [ADR-0051](adr/ADR-0051-phase8-provisional-spatial-intent-concurrency.md),
  [ADR-0052](adr/ADR-0052-phase8-remote-motion-smoothing.md),
  [ADR-0053](adr/ADR-0053-phase8-desktop-reconnect-and-automation-composition.md),
  [GDR-0013](gdr/GDR-0013-phase8-cell-transition-presentation.md), and
  [GDR-0014](gdr/GDR-0014-phase8-desktop-movement-and-correction.md), and
  [GDR-0015](gdr/GDR-0015-phase8-remote-motion-presentation.md), and
  [GDR-0016](gdr/GDR-0016-phase8-disconnect-and-resume-presentation.md)
- Latest implementation commit: `4735938383` (`Implement Phase 8 desktop reconnect automation`)

## Working synopsis

The complete Slice 7.6 implementation and real two-client process proof pass;
the project owner accepted the implementation demo on 2026-09-02.
Same-pump closes use one bounded canonical batch; disconnect captures current
canonical progress; resume preserves identity, generation progression, revision,
and acknowledgements; exact expiration rejects the old token and fresh join
creates a new identity. Credentials remain in memory and absent from evidence.

Slice 7.7 layered matrix, thresholds, and RSS-window rule are approved. The
bounded named profiles, seeded 10,000-tick matrix, paced 32-cycle reconnect,
queue high-water/zero-drain proof, and 60-second two-client real-process soak
pass replay, fault, convergence, identity/progress, matching-view, bounded RSS,
and queue contracts. An owner-approved two-pass application pump now coalesces
intermediate same-tick views before transport; the deterministic regression and
optimized lifecycle demo pass without weakening client contradiction rejection.
The project owner accepted the Slice 7.7 implementation demo and Phase 7 exit
gate on 2026-09-02. The owner approved the exact Slice 8.1 P8-001 through P8-003
hook inventory on 2026-09-02. Slice 8.2 two-provider architecture and
correct-then-command frame order are approved. The owner then approved breaking
Option D after caller migration exposed split orchestration ownership. The
runtime now owns protocol/session progression, binding/application, command
sequencing, bounded queues, and failure closure. The headless proof caller and
the concrete OpenMW coordinator core use that one path. Configuration-gated
executable attachment is implemented. The owner-confirmed `/MD` Windows network
runtime policy is now applied to the exact locked OpenSSL, Protobuf/Abseil, GNS,
and c-ares inputs. Both dependency proofs pass, the manifest is regenerated, and
the real-network RelWithDebInfo `openmw.exe` links. Sanitized runtime failures
now appear through the approved normal OpenMW message-box path. The owner demo
remains.

Slice 8.4 is now in progress. The owner approved the breaking corrective
package and P8-004 on 2026-09-03. Explicit snapshot target identity, tracked
cell-transition receipts with one coalesced deferred transition, explicit
fixture content mapping, typed provider failure closure, and renderer-only
remote avatar reconciliation are implemented. Automated gates pass; the
content-backed two-client owner demo remains.

Slice 8.5 is now in progress under owner-approved ADR-0051/GDR-0014. The two
provisional spatial intent bodies treat entity revision as observed context
while retaining session, generation, entity, authority-epoch, identity,
sequence, and domain validation. Desktop action input maps deterministically to
bounded world velocity; one pending plus one latest desired intent bounds
traffic; newer same-cell snapshots correct local position exactly. No new
OpenMW hook was needed. Automated gates pass; the owner desktop demo remains.

Slice 8.6 is now in progress under owner-approved ADR-0052/GDR-0015. A
provider-owned four-sample client-local buffer now advances each frame from the
injected monotonic clock, interpolates two ticks behind, extrapolates at most
three ticks before holding, blends bounded corrections, hard-snaps larger or
discontinuous state, clears with observation lifetime, and emits adapter-owned
typed metrics. Automated gates pass; the owner desktop demo remains. No
protocol, canonical-state, authority, or OpenMW hook changed.

Slice 8.7 is now in progress under ADR-0053/GDR-0016. The adapter owns one
bounded sequential-runtime reconnect supervisor, memory-only rotating
credentials, the original token deadline, one-second pre-auth retry pacing, and
a resumed complete-snapshot continuity barrier. Disconnect clears remotes and
suppresses multiplayer input until resume. A `BUILD_TESTING`-only typed driver
and external flow/reconnect/soak harness are implemented. Automated gates pass;
the content-backed run and owner demo remain. No new OpenMW hook, protocol,
canonical state, authority, or persistence was added.

The owner approved the GDR-0013 Option A demo mapping: interior
`Seyda Neen, Census and Excise Office`, worldspace `sys::default`, and avatar
NPC `player`. Licensed Steam content later appeared at the default path. The
runner now forwards explicit fallback archives; with the pinned dependency
runtime paths and `Morrowind.bsa`, both clients reach the approved interior but
close on remote presentation with avatar `player`. No alternate content record
or presentation behavior has been chosen. Phase 9 remains gated on diagnosis,
the complete content-backed run, and owner acceptance.

## Active files

- Governing policy: `docs/vnext/adr/ADR-0007-openmw-hook-patch-queue-policy.md`
- Adapter coordinator/target: `apps/openmw/tes3mp/{adapter.hpp,adapter.cpp,CMakeLists.txt}`
- Desktop composition: `apps/openmw/tes3mp/{desktop_connection.hpp,desktop_connection.cpp}`
- Reusable runtime: `components/tes3mp/{include/tes3mp/client_session_runtime.hpp,client_session/client_session_runtime.cpp,tests/headless_client_tests.cpp}`
- Provider/coordinator contracts: `apps/openmw/tes3mp/{providers.hpp,engine_coordinator.hpp,adapter_tests.cpp}`
- Candidate lifecycle/frame seam: `apps/openmw/engine.{hpp,cpp}`
- Executable/target composition: `apps/openmw/{main.cpp,CMakeLists.txt}`
- Desktop options: `apps/openmw/options.cpp`
- Desktop status presentation: `apps/openmw/main.cpp`
- Cell/presentation providers: `apps/openmw/tes3mp/{desktop_providers.hpp,desktop_providers.cpp}`
- Renderer-only avatar seam: `apps/openmw/mwrender/{transientactorpresentation.hpp,transientactorpresentation.cpp}`
- Movement gate seams:
  `apps/openmw/tes3mp/{providers.hpp,desktop_providers.cpp,adapter.cpp,movement_mapping.hpp,movement_mapping.cpp}` and
  `components/tes3mp/{client_session/client_session_runtime.cpp,server_core/server_command_reducer.cpp}`
- Remote smoothing gate seams:
  `apps/openmw/tes3mp/{providers.hpp,desktop_providers.cpp,adapter.cpp,remote_motion.hpp,remote_motion.cpp}` and
  `components/tes3mp/include/tes3mp/{command_primitives.hpp,observability.hpp}`
- Disconnect/resume and automation gate seams:
  `apps/openmw/tes3mp/{adapter.cpp,desktop_connection.cpp,desktop_automation.hpp,desktop_automation.cpp,providers.hpp}`,
  `components/tes3mp/{include/tes3mp/client_session_runtime.hpp,client_session/client_session_runtime.cpp}`,
  `scripts/run_phase8_desktop_demo.py`, and `scripts/tests/test_phase8_desktop_harness.py`
- Patch registry: `docs/vnext/OPENMW_PATCH_REGISTRY.json`,
  `scripts/verify_openmw_patch_registry.py`
- Evidence: [Phase 8 notes](IMPLEMENTATION_NOTES.md#phase-8--openmw-desktop-vertical-slice)

## Last verified

The first content-backed Slice 8.7 flow reaches the approved interior in two
real OpenMW processes after mounting `Morrowind.bsa`, then both connections
close with sanitized reason `remote player presentation failed` while using
avatar `player`. The focused four-test harness contract passes with explicit
fallback-archive forwarding. No complete evidence or owner-demo claim was made.

Slice 8.7 content-backed demo preflight rebuilt the MSVC 19.51 RelWithDebInfo
`tes3mp_server.exe` and passed all four focused desktop-harness Python contracts
on 2026-09-03. The matching `openmw.exe` is present. Licensed `Morrowind.esm`
was not found in the local Steam libraries, checked conventional install paths,
or an exact-name search across local C:, D:, F:, and G: drives. The owner
approved the exact Option A fixture mapping, but no content-backed result or
owner-demo claim was made.

Slice 8.7 focused MSVC 19.51 Debug reconnect/coordinator and token-recovery
contracts, full protocol aggregate, headless-client contracts, all 128
repository-owned Python tests, patch registry, staged baseline provenance with
331 intentional differences and 69 dependency inputs, staged legacy exclusion,
diff hygiene, and networking-enabled RelWithDebInfo `openmw.exe` links with
`BUILD_TESTING` both on and off pass on 2026-09-03. The content-backed desktop
harness and owner demo remain.

Slice 8.6 focused MSVC 19.51 Debug adapter/smoother contracts, full protocol
aggregate, headless-client contracts, all 124 repository-owned Python tests,
patch registry, staged baseline provenance with 325 intentional differences and
69 dependency inputs, staged legacy exclusion, diff hygiene, and the full
networking-enabled RelWithDebInfo `openmw.exe` build/link pass on 2026-09-03.

Slice 8.5's focused reducer/property and movement/coordinator contracts, full
protocol aggregate, headless-client contracts, adapter contracts, patch
registry, all 124 repository-owned Python tests, diff hygiene, staged baseline
provenance with 321 intentional differences and 69 dependency inputs, and full
RelWithDebInfo `openmw.exe` build/link pass on 2026-09-03.

Fresh MSVC 19.51 `/MD` GameNetworkingSockets and c-ares dependency proofs pass,
their exact transport manifest is regenerated, and direct object inspection
reports `MD_DynamicRelease`/`MSVCRT`. The full RelWithDebInfo OpenMW tree builds
with networking enabled and links `build/slice82-openmw-full/RelWithDebInfo/openmw.exe`;
the adapter, GNS transport, and credential-crypto contracts pass from that tree.
The same target recompiles and links with sanitized runtime failures presented
through the normal OpenMW message-box path. All 124 repository-owned Python
tests and patch-registry verification pass. The dependency/runtime link and
visible-status gates are closed. Earlier verified Phase 7 evidence remains:
MSVC headless runtime, server-app, full protocol, and pinned FlatBuffers proof.
The real-process lifecycle proof passes movement, convergence, stale-view,
32-cycle reconnect, expiration, fresh identity, and zero final queue depth. Staged
baseline provenance passes with 299 intentional differences and 69 dependency
inputs; staged legacy exclusion, patch-registry verification, and diff checks
pass. A fresh MSVC 19.51 full configuration confirms the pinned Bullet 3.25
bundle uses double precision, and the RelWithDebInfo `openmw.exe` build passes.

Refresh this file whenever implementation work changes the active slice,
governing decision, completed boundary, next work, relevant files, or evidence.
