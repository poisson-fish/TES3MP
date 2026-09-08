# TES3MP vNext development workflow

This is the single development-process document. Work one useful milestone at a
time, keep the branch buildable, and scale verification to the risk of the
change.

## Source-of-truth order

1. Production code and executable tests
2. Machine manifests and registries
3. [CURRENT.md](CURRENT.md)
4. [DECISIONS.md](DECISIONS.md)
5. Git history

Do not treat commit messages, old branches, generated build output, or archived
TES3MP code as current design authority.

## Change loop

1. Read the relevant `CURRENT.md` section and inspect its named source/tests.
2. State one observable milestone outcome and its failure boundaries.
3. If an expensive-to-reverse architecture, authority, compatibility, security,
   persistence, or player-facing rule is genuinely undecided, agree on it and
   add a compact `DECISIONS.md` entry. Routine implementation judgment belongs
   in code and tests.
4. Implement the smallest complete vertical change. Bound external input before
   allocation or mutation and preserve atomic failure.
5. Run focused tests while iterating, then affected target/integration gates.
6. Run broad platform, provenance, fuzz, or soak gates when the change could
   affect them or at a milestone/release boundary.
7. Update `CURRENT.md` only if capabilities, limitations, next work, or the last
   verification result changed. Do not append a chronological diary.

A milestone is done when its behavior works, relevant failure paths are bounded,
tests prove the important contract, active prose matches code, and the branch is
left buildable.

## Dependency boundaries

`components/tes3mp` is independently buildable. Its production targets must not
include or expose OpenMW, renderer, SDL, OSG, OpenXR, operating-system,
FlatBuffers-generated, GameNetworkingSockets, or test-support types outside the
private adapter that owns them.

The intended production dependency graph is below; arrows point from a
dependency to its consumer:

```text
tes3mp_protocol -> tes3mp_transport
tes3mp_protocol -> tes3mp_server_core
tes3mp_protocol + tes3mp_transport -> tes3mp_client_session
tes3mp_client_session + OpenMW -> openmw_tes3mp_adapter
protocol + transport + server_core -> tes3mp_server_app
```

`tes3mp_test_support` may use all four engine-independent libraries. Production
targets must not depend on it. CMake verifies allowed direct links and forbidden
include families.

## Common verification

Run commands from the repository root unless noted.

### Fast repository checks

```sh
python -m unittest discover -s scripts/tests -v
python scripts/verify_openmw_patch_registry.py
python scripts/verify_vnext_baseline.py
```

Use `--index` with provenance/exclusion tools only for staged pre-commit checks.

### Engine-independent C++ contracts

From `components/tes3mp` in a compiler environment with CMake and Ninja:

```sh
cmake --preset tes3mp-standalone --fresh
cmake --build --preset tes3mp-standalone --parallel 4
```

The build preset runs `tes3mp_protocol_tests_run`, which aggregates the portable
contract executables. Networking-enabled server/headless and GNS targets require
the verified transport dependency manifest produced by
`scripts/provision_vnext_transport.py`.

Runtime-safety entry points are:

```sh
python scripts/run_tes3mp_runtime_safety.py --profile asan-ubsan --fuzz-seconds 30
python scripts/run_tes3mp_runtime_safety.py --profile tsan
```

### OpenMW baseline

```sh
python scripts/run_vnext_baseline.py doctor
python scripts/run_vnext_baseline.py all
```

`all` verifies provenance, configures the platform preset, builds the upstream
test targets, runs `components-tests`, `openmw-tests`, and `openmw-cs-tests`, and
writes machine evidence under ignored `build/vnext-baseline`.

On Windows, run from an x64 Native Tools Command Prompt for Visual Studio 2022.
A clean checkout can provision pinned dependencies and Qt 6.6.3 with:

```bat
python scripts/run_vnext_baseline.py provision
python scripts/run_vnext_baseline.py all
```

Linux uses the packages in `CI/install_debian_deps.sh` with the approved GCC or
Clang toolchain. macOS uses the approved Xcode/AppleClang toolchain and sets
`VNEXT_VCPKG_TRIPLET` to `arm64-osx-dynamic` or `x64-osx-dynamic` before the
same provision/all flow.

### Full OpenMW/TES3MP integration

When adapter or engine patches change, build the full `openmw` and
`openmw_tes3mp_adapter_tests` targets in the configured full-tree build and run
the adapter executable. Run affected live/headless flows when behavior crosses
the real transport, OpenMW presentation, reconnect, or content-loading boundary.
Do not claim a hardware or platform result that was not actually run.

## Machine-owned evidence

- [BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json) enumerates every intended
  difference from the pinned OpenMW tree.
- [OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json) maps engine changes to
  rationale and tests.
- `scripts/vnext_*proof.json` pins third-party proof inputs.
- `docs/vnext/proofs` contains executable dependency proofs and their narrow run
  instructions.

Keep these machine assets accurate. Do not duplicate their inventories in prose.

## Documentation budget

The five Markdown files directly under `docs/vnext` are the entire active vNext
documentation set and must remain below 10,000 words combined. Each has one job:

- `README.md`: stable orientation
- `CURRENT.md`: implemented/partial/missing truth and next milestone
- `DEVELOPMENT.md`: working and verification process
- `DECISIONS.md`: non-obvious durable constraints
- `CONTENT_FORMATS.md`: specialist server input contract

Do not add status trackers, phase plans, discovery packets, implementation
notes, review transcripts, or document-only tests. Git preserves history.
