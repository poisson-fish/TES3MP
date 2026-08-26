# Local OpenMW baseline build and test

This is the repository-owned local entry point for Phase 1 Slice 1.4. It
configures and builds the three upstream OpenMW 0.51 unit-test executables from
the pinned baseline, runs them directly, and writes a machine-readable
environment and dependency inventory under the ignored `build/` directory.

The default preset intentionally follows OpenMW's upstream test-only build
shape. Full desktop application builds, installs, and platform artifact capture
are the separate Linux, Windows, and macOS CI slices (1.5 through 1.7). The
Linux CI path uses `python scripts/run_vnext_baseline.py all --ci`, which selects
the inherited `vnext-baseline-linux-ci` preset, builds the full upstream desktop
target set, runs the same tests, and installs under
`build/vnext-baseline-install`. The Windows CI path uses the same `all --ci`
entry point with the inherited `vnext-baseline-windows-ci` full-build preset
after running the normal pinned `provision` command.

On Windows, configuration also enables the existing `openmw-cs` executable
target because the pinned upstream CMake file unconditionally assigns its
Windows manifest whenever `WIN32` is true. The build preset still selects only
the three test targets; this avoids patching baseline source for a configure-only
upstream quirk.

## Common commands

Run these from the repository root:

```sh
python scripts/run_vnext_baseline.py doctor
python scripts/run_vnext_baseline.py all
```

The `all` command fails closed if baseline provenance has drifted, then uses the
host's `vnext-baseline-<platform>` configure and build presets, runs
`components-tests`, `openmw-tests`, and `openmw-cs-tests`, and writes:

- `build/vnext-baseline/components-tests.json`
- `build/vnext-baseline/openmw-tests.json`
- `build/vnext-baseline/openmw-cs-tests.json`
- `build/vnext-baseline/vnext-baseline-evidence.json`

Individual `configure`, `build`, `test`, and `evidence` commands are available
for iteration. `--index` makes the configure-time provenance check inspect the
staged tree and is intended only for pre-commit verification.

## Windows x86-64

Use an **x64 Native Tools Command Prompt for Visual Studio 2022**. The runner
rejects other Visual Studio major versions so a newer installed IDE cannot
silently replace ADR-0002's MSVC 2022/v143 baseline.

The inherited OpenMW dependency bundle and Qt inputs are pinned in
`scripts/vnext_baseline_dependencies.json`. From the developer prompt, a clean
checkout can provision them into the ignored `deps/` directory and run the
suite with:

```bat
python scripts/run_vnext_baseline.py provision
python scripts/run_vnext_baseline.py all
```

Provisioning verifies the commit-pinned OpenMW dependency manifest, the bundle's
SHA-512, and the aqtinstall executable's SHA-256 before extraction or execution.
Qt is requested as exact version 6.6.3 and target `win64_msvc2019_64`. The
evidence file records the resolved compiler/CMake/Ninja versions, all installed
vcpkg package-list filenames, the count of retained vcpkg copyright files, and
the Qt version reported by `qmake`.

Slice 1.6's GitHub Actions workflow pins Windows Server 2022 and the action
commits used to enter the Visual Studio 2022 x64 environment. It runs the same
provisioning and baseline entry points, then retains the installed tree, JSON
test results, runner/toolchain metadata, the pinned dependency manifest and
lock, vcpkg package lists and licenses, and the pinned Qt license reference.

By default the dependency roots are:

- `VNEXT_VCPKG_ROOT=<repository>/deps`
- `VNEXT_QT_ROOT=<repository>/deps/Qt/6.6.3/msvc2019_64`

Set either environment variable before running the script to use an existing
verified installation. `VNEXT_CMAKE` and `VNEXT_NINJA` can name exact tool
executables when they are not on `PATH`.

## Linux x86-64

Install the OpenMW 0.51 packages named by `CI/install_debian_deps.sh`, using GCC
13 or Clang 18 and Ninja, then run the common commands. The Linux preset keeps
OpenMW's system Recast Navigation and TinyXML choices. Slice 1.5's GitHub Actions
workflow pins Ubuntu 24.04, runs both approved compiler gates through the same
script, and retains the resolved package/license inventory and installed tree.

## macOS arm64 and x86-64

Install the dependency bundle and Qt with OpenMW's macOS dependency flow, set
`VNEXT_VCPKG_ROOT`, `VNEXT_QT_ROOT`, and `VNEXT_VCPKG_TRIPLET`, and run the
common commands with AppleClang from Xcode 16 and Ninja. Use
`arm64-osx-dynamic` on arm64 and `x64-osx-dynamic` on Intel. Slice 1.7 will make
that acquisition path fully commit-pinned and retain the resolved
package/license inventories for both architectures.

## What a passing local run proves

A passing run proves the active tree still matches the recorded OpenMW baseline
apart from enumerated vNext files, the selected platform preset configures, the
three upstream unit-test targets compile, and every upstream unit test passes.
It does not complete the Phase 1 CI matrix or compiled legacy-exclusion proof;
those remain Slices 1.5 through 1.8.
