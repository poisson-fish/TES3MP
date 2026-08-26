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
import urllib.request


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "vnext-baseline"
LOCK_PATH = ROOT / "scripts" / "vnext_baseline_dependencies.json"
EVIDENCE_PATH = BUILD_DIR / "vnext-baseline-evidence.json"
TEST_NAMES = ("components-tests", "openmw-tests", "openmw-cs-tests")
PRESETS = {
    "Windows": "vnext-baseline-windows",
    "Linux": "vnext-baseline-linux",
    "Darwin": "vnext-baseline-macos",
}


class BaselineError(RuntimeError):
    pass


def load_lock(path: pathlib.Path = LOCK_PATH) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or "windows_x86_64" not in data:
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


def make_environment(host: str) -> dict[str, str]:
    env = os.environ.copy()
    deps_root = ROOT / "deps"
    if host == "Windows":
        env.setdefault("VNEXT_VCPKG_ROOT", str(deps_root))
        env.setdefault("VNEXT_QT_ROOT", str(deps_root / "Qt" / "6.6.3" / "msvc2019_64"))
    elif host == "Darwin":
        env.setdefault("VNEXT_VCPKG_ROOT", "/tmp/openmw-deps")
        env.setdefault("VNEXT_VCPKG_TRIPLET", "arm64-osx-dynamic" if platform.machine() == "arm64" else "x64-osx-dynamic")
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
    suffix = ".exe" if host == "Windows" else ""
    return BUILD_DIR / f"{name}{suffix}"


def read_cmake_compiler_version(build_dir: pathlib.Path) -> str:
    candidates = sorted((build_dir / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    for path in candidates:
        match = re.search(
            r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)',
            path.read_text(encoding="utf-8", errors="replace"),
        )
        if match:
            return match.group(1)
    raise BaselineError("CMake did not record a C++ compiler version.")


def collect_evidence(cmake: str, ninja: str, host: str, preset: str, env: dict[str, str]) -> dict:
    cache = BUILD_DIR / "CMakeCache.txt"
    if not cache.is_file():
        raise BaselineError("CMake cache is missing; configure before collecting evidence.")
    cache_values = {}
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(("CMAKE_CXX_COMPILER:", "CMAKE_CXX_COMPILER_VERSION:", "CMAKE_GENERATOR:")):
            key, value = line.split("=", 1)
            cache_values[key.split(":", 1)[0]] = value
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
            "CMAKE_CXX_COMPILER_VERSION": read_cmake_compiler_version(BUILD_DIR),
        },
        "dependency_lock_sha256": hash_file(LOCK_PATH, "sha256"),
        "tests": [str(executable_path(name, host).relative_to(ROOT)) for name in TEST_NAMES],
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
    return evidence


def write_evidence(data: dict) -> None:
    EVIDENCE_PATH.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Wrote evidence: {EVIDENCE_PATH}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("doctor", "provision", "configure", "build", "test", "evidence", "all"))
    parser.add_argument("--index", action="store_true", help="verify the staged tree instead of HEAD")
    args = parser.parse_args(argv)
    host = platform.system()
    if host not in PRESETS:
        raise BaselineError(f"Unsupported host system: {host}")
    preset = PRESETS[host]
    env = make_environment(host)
    cmake = find_tool("cmake", "VNEXT_CMAKE")
    ninja = find_tool("ninja", "VNEXT_NINJA")
    validate_environment(host, env, provision=args.command == "provision")

    if args.command == "provision":
        if host != "Windows":
            raise BaselineError("Automated dependency provisioning is currently defined only for Windows.")
        provision_windows(cmake, env)
        print("Windows baseline dependencies are provisioned and verified.")
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
    if args.command in {"evidence", "all"}:
        write_evidence(collect_evidence(cmake, ninja, host, preset, env))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BaselineError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
