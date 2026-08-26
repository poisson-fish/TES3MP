#!/usr/bin/env python3
"""Run the reproducible local OpenMW 0.51 baseline test path."""

from __future__ import annotations

import argparse
import datetime as dt
import glob
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "vnext-baseline"
INSTALL_DIR = ROOT / "build" / "vnext-baseline-install"
LOCK_PATH = ROOT / "scripts" / "vnext_baseline_dependencies.json"
DOWNLOAD_DIR = ROOT / "deps" / "downloads"
EVIDENCE_PATH = BUILD_DIR / "vnext-baseline-evidence.json"
LEGACY_EXCLUSION_EVIDENCE_PATH = BUILD_DIR / "vnext-legacy-exclusion.json"
TEST_NAMES = ("components-tests", "openmw-tests", "openmw-cs-tests")
PRESETS = {
    "Windows": "vnext-baseline-windows",
    "Linux": "vnext-baseline-linux",
    "Darwin": "vnext-baseline-macos",
}
CI_PRESETS = {
    "Windows": "vnext-baseline-windows-ci",
    "Linux": "vnext-baseline-linux-ci",
    "Darwin": "vnext-baseline-macos-ci",
}


class BaselineError(RuntimeError):
    pass


def load_lock(path: pathlib.Path = LOCK_PATH) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or not {"windows_x86_64", "macos"} <= data.keys():
        raise BaselineError(f"Unsupported dependency lock: {path}")
    return data


def hash_file(path: pathlib.Path, algorithm: str) -> str:
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_tool(name: str, env_name: str) -> str:
    configured = os.environ.get(env_name)
    if configured:
        path = pathlib.Path(configured)
        if path.is_file():
            return str(path)
        raise BaselineError(f"{env_name} does not name a file: {configured}")
    resolved = shutil.which(name)
    if not resolved:
        raise BaselineError(f"Required tool '{name}' is not on PATH (or set {env_name}).")
    return resolved


def run(command: list[str], *, env: dict[str, str], cwd: pathlib.Path = ROOT, capture: bool = False) -> str:
    print("+", subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if result.returncode != 0:
        output = result.stdout.strip() if capture and result.stdout else ""
        raise BaselineError(f"Command failed with exit code {result.returncode}: {output}")
    return result.stdout.strip() if capture and result.stdout else ""


def download(url: str, destination: pathlib.Path, algorithm: str, expected: str) -> None:
    if destination.exists():
        actual = hash_file(destination, algorithm)
        if actual != expected:
            raise BaselineError(f"Existing download has wrong {algorithm}: {destination} ({actual})")
        print(f"Using verified download: {destination}")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    print(f"Downloading {url}")
    try:
        with urllib.request.urlopen(url) as response, temporary.open("wb") as stream:
            shutil.copyfileobj(response, stream)
        actual = hash_file(temporary, algorithm)
        if actual != expected:
            raise BaselineError(f"Downloaded file has wrong {algorithm}: {actual}")
        temporary.replace(destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def provision_windows(cmake: str, env: dict[str, str]) -> None:
    lock = load_lock()["windows_x86_64"]
    deps_root = pathlib.Path(env["VNEXT_VCPKG_ROOT"])
    qt_parent = pathlib.Path(env["VNEXT_QT_ROOT"]).parents[1]
    bundle = lock["vcpkg_bundle"]
    manifest = deps_root / "downloads" / "openmw-vcpkg-manifest.txt"
    archive = deps_root / bundle["archive_name"]
    download(bundle["manifest_url"], manifest, "sha256", bundle["manifest_sha256"])
    manifest_lines = manifest.read_text(encoding="utf-8").splitlines()
    if len(manifest_lines) != 2 or manifest_lines[0] != bundle["archive_url"] or bundle["archive_sha512"] not in manifest_lines[1]:
        raise BaselineError("Pinned OpenMW dependency manifest content does not match the dependency lock.")
    download(bundle["archive_url"], archive, "sha512", bundle["archive_sha512"])
    toolchain = deps_root / "scripts" / "buildsystems" / "vcpkg.cmake"
    if not toolchain.exists():
        deps_root.mkdir(parents=True, exist_ok=True)
        run([cmake, "-E", "tar", "xf", str(archive)], env=env, cwd=deps_root)
    if not toolchain.is_file():
        raise BaselineError(f"Dependency bundle did not provide {toolchain}")

    qt = lock["qt"]
    qmake = pathlib.Path(env["VNEXT_QT_ROOT"]) / "bin" / "qmake.exe"
    if not qmake.exists():
        aqt = qt_parent / f"aqt_x64-{qt['aqt_version']}.exe"
        download(qt["aqt_url"], aqt, "sha256", qt["aqt_sha256"])
        run(
            [str(aqt), "install-qt", "windows", "desktop", qt["version"], qt["target"], "--outputdir", str(qt_parent)],
            env=env,
            cwd=qt_parent,
        )
    if not qmake.is_file():
        raise BaselineError(f"Qt installation did not provide {qmake}")


def provision_macos_prerequisites(env: dict[str, str]) -> None:
    if env.get("GITHUB_ACTIONS") == "true":
        candidates = sorted(pathlib.Path("/Applications").glob("Xcode_16*.app"), reverse=True)
        if not candidates:
            raise BaselineError("The macOS runner does not provide an Xcode 16 application bundle.")
        developer_dir = candidates[0] / "Contents" / "Developer"
        run(["sudo", "xcode-select", "-s", str(developer_dir)], env=env)
        xcode_version = run(["xcodebuild", "-version"], env=env, capture=True).splitlines()[0]
        if not xcode_version.startswith("Xcode 16."):
            raise BaselineError(f"Expected Xcode 16 after selection, got {xcode_version!r}.")
    brew = find_tool("brew", "VNEXT_BREW")
    brew_env = env.copy()
    brew_env["HOMEBREW_NO_AUTO_UPDATE"] = "1"
    formulas = load_lock()["macos"]["homebrew"]["formulas"]
    run([brew, "install", *formulas], env=brew_env)


def extract_macos_bundle(cmake: str, archive: pathlib.Path, destination: pathlib.Path, env: dict[str, str]) -> None:
    toolchain = destination / "scripts" / "buildsystems" / "vcpkg.cmake"
    if toolchain.is_file():
        print(f"Using existing macOS dependency bundle: {destination}")
        return
    if destination.exists():
        raise BaselineError(
            f"Incomplete macOS dependency directory exists: {destination}. Remove that exact directory and retry."
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vnext-macos-deps-", dir=destination.parent) as directory:
        staging = pathlib.Path(directory)
        run([cmake, "-E", "tar", "xf", str(archive)], env=env, cwd=staging)
        entries = list(staging.iterdir())
        source = entries[0] if len(entries) == 1 and entries[0].is_dir() else staging
        destination.mkdir()
        for path in source.iterdir():
            shutil.move(str(path), destination / path.name)
    if not toolchain.is_file():
        raise BaselineError(f"macOS dependency bundle did not provide {toolchain}")


def provision_macos(cmake: str, env: dict[str, str]) -> None:
    macos = load_lock()["macos"]
    triplet = env["VNEXT_VCPKG_TRIPLET"]
    bundle = macos["vcpkg_bundles"].get(triplet)
    if not isinstance(bundle, dict):
        raise BaselineError(f"No pinned macOS dependency bundle for triplet: {triplet}")
    DOWNLOAD_DIR.mkdir(parents=True, exist_ok=True)
    manifest = DOWNLOAD_DIR / f"macos-{triplet}-manifest.txt"
    archive = DOWNLOAD_DIR / bundle["archive_name"]
    download(bundle["manifest_url"], manifest, "sha256", bundle["manifest_sha256"])
    manifest_lines = manifest.read_text(encoding="utf-8").splitlines()
    expected_hash_line = f"{bundle['archive_sha512']}  {bundle['archive_name']}"
    if manifest_lines != [bundle["archive_url"], expected_hash_line]:
        raise BaselineError("Pinned macOS dependency manifest content does not match the dependency lock.")
    download(bundle["archive_url"], archive, "sha512", bundle["archive_sha512"])
    extract_macos_bundle(cmake, archive, pathlib.Path(env["VNEXT_VCPKG_ROOT"]), env)
    qmake = pathlib.Path(env["VNEXT_QT_ROOT"]) / "bin" / "qmake"
    if not qmake.is_file():
        raise BaselineError(f"Homebrew Qt installation did not provide {qmake}")


def make_environment(host: str) -> dict[str, str]:
    env = os.environ.copy()
    deps_root = ROOT / "deps"
    if host == "Windows":
        env.setdefault("VNEXT_VCPKG_ROOT", str(deps_root))
        env.setdefault("VNEXT_QT_ROOT", str(deps_root / "Qt" / "6.6.3" / "msvc2019_64"))
    elif host == "Darwin":
        env.setdefault("VNEXT_VCPKG_ROOT", "/tmp/openmw-deps")
        is_arm64 = platform.machine().lower() == "arm64"
        env.setdefault("VNEXT_VCPKG_TRIPLET", "arm64-osx-dynamic" if is_arm64 else "x64-osx-dynamic")
        env.setdefault("VNEXT_QT_ROOT", "/opt/homebrew/opt/qt" if is_arm64 else "/usr/local/opt/qt")
    return env


def validate_environment(host: str, env: dict[str, str], *, provision: bool = False) -> None:
    if platform.machine().lower() not in {"amd64", "x86_64", "arm64"}:
        raise BaselineError(f"Unsupported host architecture: {platform.machine()}")
    if host == "Windows":
        if not provision and not env.get("VSCMD_VER", "").startswith("17."):
            raise BaselineError("Run from an x64 Visual Studio 2022 developer prompt; VSCMD_VER 17.x is required.")
        required = (
            pathlib.Path(env["VNEXT_VCPKG_ROOT"]) / "scripts" / "buildsystems" / "vcpkg.cmake",
            pathlib.Path(env["VNEXT_QT_ROOT"]) / "bin" / "qmake.exe",
        )
        if not provision:
            for path in required:
                if not path.is_file():
                    raise BaselineError(f"Required dependency is missing: {path}. Run the provision command first.")
    elif host == "Darwin":
        for name in ("VNEXT_VCPKG_ROOT", "VNEXT_VCPKG_TRIPLET", "VNEXT_QT_ROOT"):
            if not env.get(name):
                raise BaselineError(f"Required environment variable is unset: {name}")
        expected_triplet = "arm64-osx-dynamic" if platform.machine().lower() == "arm64" else "x64-osx-dynamic"
        if env["VNEXT_VCPKG_TRIPLET"] != expected_triplet:
            raise BaselineError(
                f"macOS host architecture requires {expected_triplet}, got {env['VNEXT_VCPKG_TRIPLET']}."
            )
        if not provision:
            required = (
                pathlib.Path(env["VNEXT_VCPKG_ROOT"]) / "scripts" / "buildsystems" / "vcpkg.cmake",
                pathlib.Path(env["VNEXT_QT_ROOT"]) / "bin" / "qmake",
            )
            for path in required:
                if not path.is_file():
                    raise BaselineError(f"Required dependency is missing: {path}. Run the provision command first.")


def test_environment(host: str, env: dict[str, str]) -> dict[str, str]:
    result = env.copy()
    if host == "Windows":
        search = [
            pathlib.Path(env["VNEXT_VCPKG_ROOT"]) / "installed" / "x64-windows" / "bin" / "Release",
            pathlib.Path(env["VNEXT_VCPKG_ROOT"]) / "installed" / "x64-windows" / "bin",
            pathlib.Path(env["VNEXT_QT_ROOT"]) / "bin",
        ]
        result["PATH"] = os.pathsep.join(str(path) for path in search) + os.pathsep + result.get("PATH", "")
    elif host == "Darwin":
        triplet = env["VNEXT_VCPKG_TRIPLET"]
        library = pathlib.Path(env["VNEXT_VCPKG_ROOT"]) / "installed" / triplet / "lib"
        result["DYLD_LIBRARY_PATH"] = str(library) + os.pathsep + result.get("DYLD_LIBRARY_PATH", "")
    return result


def executable_path(name: str, host: str) -> pathlib.Path:
    if host == "Darwin":
        return BUILD_DIR / "OpenMW.app" / "Contents" / "MacOS" / name
    suffix = ".exe" if host == "Windows" else ""
    return BUILD_DIR / f"{name}{suffix}"


def read_cmake_compiler_info(build_dir: pathlib.Path) -> dict[str, str]:
    candidates = sorted((build_dir / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    for path in candidates:
        compiler_id = re.search(
            r'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)',
            path.read_text(encoding="utf-8", errors="replace"),
        )
        compiler_version = re.search(
            r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)',
            path.read_text(encoding="utf-8", errors="replace"),
        )
        if compiler_id and compiler_version:
            return {
                "CMAKE_CXX_COMPILER_ID": compiler_id.group(1),
                "CMAKE_CXX_COMPILER_VERSION": compiler_version.group(1),
            }
    raise BaselineError("CMake did not record a C++ compiler identity and version.")


def collect_evidence(cmake: str, ninja: str, host: str, preset: str, env: dict[str, str]) -> dict:
    cache = BUILD_DIR / "CMakeCache.txt"
    if not cache.is_file():
        raise BaselineError("CMake cache is missing; configure before collecting evidence.")
    cache_values = {}
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(("CMAKE_CXX_COMPILER:", "CMAKE_CXX_COMPILER_VERSION:", "CMAKE_GENERATOR:")):
            key, value = line.split("=", 1)
            cache_values[key.split(":", 1)[0]] = value
    compiler_info = read_cmake_compiler_info(BUILD_DIR)
    evidence = {
        "schema_version": 1,
        "recorded_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_commit": run(["git", "rev-parse", "HEAD"], env=env, capture=True),
        "host": {"system": host, "release": platform.release(), "architecture": platform.machine()},
        "preset": preset,
        "tools": {
            "cmake": run([cmake, "--version"], env=env, capture=True).splitlines()[0],
            "ninja": run([ninja, "--version"], env=env, capture=True),
            "python": platform.python_version(),
            **cache_values,
            **compiler_info,
        },
        "dependency_lock_sha256": hash_file(LOCK_PATH, "sha256"),
        "tests": [str(executable_path(name, host).relative_to(ROOT)) for name in TEST_NAMES],
    }
    if preset in CI_PRESETS.values():
        if not INSTALL_DIR.is_dir():
            raise BaselineError(f"CI installation is missing: {INSTALL_DIR}")
        evidence["installation"] = {
            "path": str(INSTALL_DIR.relative_to(ROOT)),
            "files": sum(1 for path in INSTALL_DIR.rglob("*") if path.is_file()),
        }
    if host == "Windows":
        lock = load_lock()["windows_x86_64"]
        deps_root = pathlib.Path(env["VNEXT_VCPKG_ROOT"])
        info_files = sorted((deps_root / "installed" / "vcpkg" / "info").glob("*.list"))
        copyrights = sorted((deps_root / "installed" / "x64-windows" / "share").glob("*/copyright"))
        qmake = pathlib.Path(env["VNEXT_QT_ROOT"]) / "bin" / "qmake.exe"
        archive = deps_root / lock["vcpkg_bundle"]["archive_name"]
        evidence["windows_dependencies"] = {
            "vcpkg_tag": lock["vcpkg_bundle"]["tag"],
            "vcpkg_archive_sha512": hash_file(archive, "sha512") if archive.is_file() else None,
            "vcpkg_package_list_files": [path.name for path in info_files],
            "vcpkg_copyright_files": len(copyrights),
            "qt_version": run([str(qmake), "-query", "QT_VERSION"], env=env, capture=True),
            "qt_target": lock["qt"]["target"],
        }
    elif host == "Darwin":
        macos = load_lock()["macos"]
        triplet = env["VNEXT_VCPKG_TRIPLET"]
        bundle = macos["vcpkg_bundles"][triplet]
        deps_root = pathlib.Path(env["VNEXT_VCPKG_ROOT"])
        package_lists = sorted((deps_root / "installed" / "vcpkg" / "info").glob("*.list"))
        copyrights = sorted((deps_root / "installed" / triplet / "share").glob("*/copyright"))
        archive = DOWNLOAD_DIR / bundle["archive_name"]
        manifest = DOWNLOAD_DIR / f"macos-{triplet}-manifest.txt"
        qmake = pathlib.Path(env["VNEXT_QT_ROOT"]) / "bin" / "qmake"
        evidence["macos_dependencies"] = {
            "vcpkg_tag": macos["tag"],
            "vcpkg_triplet": triplet,
            "vcpkg_manifest_sha256": hash_file(manifest, "sha256"),
            "vcpkg_archive_sha512": hash_file(archive, "sha512"),
            "vcpkg_package_list_files": [path.name for path in package_lists],
            "vcpkg_copyright_files": len(copyrights),
            "qt_formula": macos["homebrew"]["qt_formula"],
            "qt_version": run([str(qmake), "-query", "QT_VERSION"], env=env, capture=True),
        }
    return evidence


def write_evidence(data: dict) -> None:
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Wrote evidence: {EVIDENCE_PATH}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=("doctor", "provision", "configure", "build", "test", "install", "evidence", "all")
    )
    parser.add_argument("--index", action="store_true", help="verify the staged tree instead of HEAD")
    parser.add_argument(
        "--ci",
        action="store_true",
        help="use the supported platform's full-build CI preset and install the result",
    )
    args = parser.parse_args(argv)
    host = platform.system()
    if host not in PRESETS:
        raise BaselineError(f"Unsupported host system: {host}")
    if args.ci and host not in CI_PRESETS:
        raise BaselineError(f"A full-build CI preset is not defined for {host} yet.")
    preset = CI_PRESETS[host] if args.ci else PRESETS[host]
    env = make_environment(host)
    if args.command == "provision" and host == "Darwin":
        provision_macos_prerequisites(env)
    cmake = find_tool("cmake", "VNEXT_CMAKE")
    ninja = find_tool("ninja", "VNEXT_NINJA")
    validate_environment(host, env, provision=args.command == "provision")

    if args.command == "provision":
        if host == "Windows":
            provision_windows(cmake, env)
        elif host == "Darwin":
            provision_macos(cmake, env)
        else:
            raise BaselineError("Automated dependency provisioning is defined only for Windows and macOS.")
        print(f"{host} baseline dependencies are provisioned and verified.")
        return 0
    if args.command == "doctor":
        print(f"Host: {host} {platform.machine()}")
        print(f"Preset: {preset}")
        print(f"CMake: {cmake}")
        print(f"Ninja: {ninja}")
        print("Baseline environment checks passed.")
        return 0

    verify = [sys.executable, str(ROOT / "scripts" / "verify_vnext_baseline.py")]
    if args.index:
        verify.append("--index")
    if args.command in {"configure", "all"}:
        run(verify, env=env)
        run([cmake, "--preset", preset, "--fresh"], env=env)
        exclusion = [
            sys.executable,
            str(ROOT / "scripts" / "verify_vnext_legacy_exclusion.py"),
            "--build-dir",
            str(BUILD_DIR),
            "--evidence",
            str(LEGACY_EXCLUSION_EVIDENCE_PATH),
        ]
        if args.index:
            exclusion.append("--index")
        run(exclusion, env=env)
    if args.command in {"build", "all"}:
        run([cmake, "--build", "--preset", preset], env=env)
    if args.command in {"test", "all"}:
        runtime_env = test_environment(host, env)
        for name in TEST_NAMES:
            executable = executable_path(name, host)
            if not executable.is_file():
                raise BaselineError(f"Test executable is missing: {executable}")
            result_file = BUILD_DIR / f"{name}.json"
            run([str(executable), f"--gtest_output=json:{result_file}"], env=runtime_env)
    if args.command == "install" or (args.command == "all" and args.ci):
        run([cmake, "--install", str(BUILD_DIR), "--prefix", str(INSTALL_DIR)], env=env)
    if args.command in {"evidence", "all"}:
        write_evidence(collect_evidence(cmake, ninja, host, preset, env))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BaselineError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
