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

### Everyday product builds

The product preset builds only the two shipping runtime targets: the OpenMW
client and the TES3MP dedicated server. It disables the launcher, editor,
conversion tools, benchmarks, and upstream test suites.

On Windows, the wrapper initializes MSVC, provisions missing pinned transport
dependencies, configures, and builds. It is quiet by default: stage markers and
a short filtered tail go to the console, while the complete output is retained
under `build/logs`. Use `-VerboseOutput` only for interactive diagnosis.

```bat
build_windows.bat
```

The first build takes longer because it compiles the verified OpenSSL,
Protobuf, GameNetworkingSockets, and c-ares inputs. Later builds reuse them.
Use `-Clean` to refresh CMake configuration without deleting or rebuilding
those dependency inputs.

A clean Windows machine also needs native Strawberry Perl to build OpenSSL.
Git's bundled MSYS Perl is not compatible. Install it from an elevated terminal
before the first build:

```bat
winget install --id StrawberryPerl.StrawberryPerl --exact
```

To provision the transport inputs separately, or to force their proofs to run
again, use:

```sh
python scripts/provision_vnext_transport.py
python scripts/provision_vnext_transport.py --refresh
```

`product` is the default. Focused targets use the standalone tree and do not
build OpenMW; desktop evidence has a persistent instrumented tree:

```bat
build_windows.bat
build_windows.bat -Target client
build_windows.bat -Target server
build_windows.bat -Target headless
build_windows.bat -Target protocol
build_windows.bat -Target server-logic
build_windows.bat -Target adapter-tests
build_windows.bat -Target desktop-evidence
```

`contracts`, `checks`, `baseline`, and `-VerboseOutput` remain explicit broader
or diagnostic gates. The product preset pins desktop automation off. The
desktop-evidence preset alone enables `TES3MP_ENABLE_DESKTOP_AUTOMATION`, keeps
`BUILD_TESTING` off, and writes to `build/vnext-desktop-evidence`.

CI and other supported desktop platforms use the matching root preset:

```sh
cmake --preset vnext-product-linux --fresh
cmake --build --preset vnext-product-linux --parallel 4
```

Use `vnext-product-macos` on macOS. The headless client and focused test
executables are development tools and are deliberately absent from the product
preset. GitHub CI runs these bounded Linux, macOS, and Windows product builds,
plus the Linux ASan/UBSan and fuzz smoke profile. The full OpenMW baseline below
is an explicit release or regression investigation gate, not the normal CI or
edit-build loop.

### Fast repository checks

```sh
python -m unittest discover -s scripts/tests -v
python scripts/verify_openmw_patch_registry.py
python scripts/verify_vnext_baseline.py
```

### Deterministic content baking

Prepare a mapping layer containing only `tes3mp-content-appearance-record` and
the repeatable `tes3mp-content-*-map` assignments, then bake an immutable pack:

```sh
python scripts/bake_tes3mp_content.py bake \
  --openmw-config /path/to/openmw.cfg \
  --server-config files/data/tes3mp/server.cfg \
  --client-mappings files/data/tes3mp/vanilla-client-mappings.cfg \
  --derived-pack-recipe files/data/tes3mp/vanilla-derived-pack.json \
  --output build/tes3mp-content
python scripts/bake_tes3mp_content.py verify \
  build/tes3mp-content/packs/<manifest-id>
```

Repeat `--openmw-config` in increasing priority order when the effective
loadout is composed from several configuration layers. List every TES3 master
once and before its dependents; the baker validates the bounded `MAST` graph but
does not discover or insert missing plugins. The graph is recorded in
`pack.json` for diagnostics. Use the generated pack's `openmw.cfg` as an OpenMW
configuration layer and its `server.cfg` for the dedicated server. Provision
`join-password.txt` in the output root; the server creates and updates
`players.txt` there. Do not edit a manifest-addressed pack. Omit
`--derived-pack-recipe` only when validating a fully authored custom pack; the
packaged vanilla configuration uses the recipe so inventory, combat, actor, and
collision catalogs are regenerated from its resolved loadout. V2 does not yet
bind archives or loose resources and is not a complete general-modpack packager.

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

Use `build_windows.bat -Target adapter-tests` for adapter-only changes. Use
`-Target desktop-evidence` plus the affected live capture when behavior crosses
OpenMW presentation, reconnect, or content loading. Do not claim an unrun result.

For the content-backed two-client weather/reconnect/slow-peer capture, first
build `desktop-evidence`, then use a fresh artifact directory:

```sh
python scripts/run_weather_reconnect_capture.py \
  --server build/vnext-desktop-evidence/tes3mp_server.exe \
  --openmw build/vnext-desktop-evidence/openmw.exe \
  --openmw-config build/weather-source-openmw.cfg \
  --resources build/vnext-desktop-evidence/resources \
  --data "/path/to/Morrowind/Data Files" \
  --fallback-archive Morrowind.bsa --content Morrowind.esm \
  --artifacts build/weather-reconnect-evidence
```

The runner bakes the real loadout and captures bounded logs, transition/resume
samples, RSS, queue high-water/drain state, and `summary.json`. It rejects a
nonempty artifact directory.

The wait/rest rollover, reconnect-across-jump, and slow-peer capture uses the
same dedicated evidence binaries and loadout arguments:

```sh
python scripts/run_wait_rest_capture.py \
  --server build/vnext-desktop-evidence/tes3mp_server.exe \
  --openmw build/vnext-desktop-evidence/openmw.exe \
  --openmw-config build/weather-source-openmw.cfg \
  --resources build/vnext-desktop-evidence/resources \
  --data "/path/to/Morrowind/Data Files" \
  --fallback-archive Morrowind.bsa --content Morrowind.esm \
  --artifacts build/wait-rest-evidence
```

It bakes a December 30 23:00 fixture, requests one matching two-hour rest per
active client, and rejects non-identical rollover revisions, duplicate world-time
presentation, failed resume convergence, undrained queues, or unbounded RSS.
`run_security_capture.py` uses the same arguments and records lockpick/probe
wear, progression, object mutation, resume convergence, and queue drain.
`run_magic_use_capture.py` uses the same arguments for a real-loadout
when-used enchantment and records the intent, reliable event presentation,
charge consumption, target effect, Enchant progress, one durable resume, and
queue drain:

```sh
python scripts/run_magic_use_capture.py \
  --server build/vnext-desktop-evidence/tes3mp_server.exe \
  --openmw build/vnext-desktop-evidence/openmw.exe \
  --openmw-config build/weather-source-openmw.cfg \
  --resources build/vnext-desktop-evidence/resources \
  --data "/path/to/Morrowind/Data Files" \
  --fallback-archive Morrowind.bsa --content Morrowind.esm \
  --artifacts build/magic-use-evidence
```

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
