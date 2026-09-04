#!/usr/bin/env python3
import hashlib
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROVENANCE = ROOT / "docs/vnext/OPENMW_VR_PROVENANCE.json"
REGISTRY = ROOT / "docs/vnext/OPENMW_VR_PATCH_REGISTRY.json"


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(ROOT), *args], check=check, capture_output=True,
        text=True, encoding="utf-8")


def git_text(*args: str) -> str:
    return git(*args).stdout.strip()


def commit_file(commit: str, path: str) -> str:
    return git_text("show", f"{commit}:{path}")


def verify() -> None:
    provenance = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    if provenance.get("schema_version") != 1 or provenance.get("target") != "pc-vr":
        raise ValueError("invalid VR provenance root")
    if registry.get("schema_version") != 1 or registry.get("target") != "pc-vr":
        raise ValueError("invalid VR patch registry root")

    upstream = provenance["upstream"]
    desktop = provenance["desktop_product_parent"]
    integration = provenance["integration"]
    openxr = provenance["openxr"]
    if registry["upstream_commit"] != upstream["commit"]:
        raise ValueError("VR registry upstream does not match provenance")
    if registry["integration_commit"] != integration["commit"]:
        raise ValueError("VR registry integration does not match provenance")

    if git_text("rev-parse", f"{upstream['ref']}^{{commit}}") != upstream["commit"]:
        raise ValueError("VR tag does not resolve to the pinned commit")
    if git_text("show", "-s", "--format=%T", upstream["commit"]) != upstream["tree"]:
        raise ValueError("VR upstream tree mismatch")
    if git_text("show", "-s", "--format=%T", desktop["commit"]) != desktop["tree"]:
        raise ValueError("desktop product tree mismatch")
    if git_text("show", "-s", "--format=%T", integration["commit"]) != integration["tree"]:
        raise ValueError("VR integration tree mismatch")
    parents = git_text("show", "-s", "--format=%P", integration["commit"]).split()
    if parents != integration["parents"]:
        raise ValueError(f"unexpected VR integration parents: {parents}")
    if git_text("merge-base", *parents) != integration["merge_base"]:
        raise ValueError("VR integration merge base mismatch")
    if git("merge-base", "--is-ancestor", integration["commit"], "HEAD", check=False).returncode != 0:
        raise ValueError("HEAD does not descend from the approved VR integration")

    lock_path = ROOT / provenance["dependencies"]["desktop_lock_path"]
    lock_hash = hashlib.sha256(lock_path.read_bytes()).hexdigest()
    if lock_hash != provenance["dependencies"]["desktop_lock_sha256"]:
        raise ValueError("desktop dependency lock hash changed")
    extern = (ROOT / openxr["cmake_path"]).read_text(encoding="utf-8")
    if f"GIT_TAG {openxr['commit']}" not in extern or "GIT_TAG release-1.0.24" in extern:
        raise ValueError("OpenXR source is not commit-pinned")
    if (openxr["license"] != "Apache-2.0"
            or openxr["license_path"] != "LICENSE"
            or len(openxr["archive_sha256"]) != 64
            or len(openxr["license_sha256"]) != 64
            or openxr["acquisition_command"] != "python scripts/prepare_openmw_vr_dependencies.py"
            or not (ROOT / "scripts/prepare_openmw_vr_dependencies.py").is_file()):
        raise ValueError("invalid OpenXR provenance")

    required_patch = {"id", "slice", "owner", "purpose", "paths", "tests", "disposition", "removal_condition"}
    covered: set[str] = set()
    ids: set[str] = set()
    for patch in registry["patches"]:
        if set(patch) != required_patch or not all(patch.values()):
            raise ValueError("invalid VR patch entry")
        if patch["id"] in ids:
            raise ValueError(f"duplicate VR patch id: {patch['id']}")
        ids.add(patch["id"])
        for path in patch["paths"]:
            if path in covered or not (ROOT / path).is_file():
                raise ValueError(f"invalid or duplicate VR patch path: {path}")
            covered.add(path)
    changed = set(git_text("diff", "--name-only", upstream["commit"], "HEAD", "--", "apps/openmw").splitlines())
    for prefix in registry["excluded_prefixes"]:
        changed = {path for path in changed if not path.startswith(prefix)}
    if changed != covered:
        raise ValueError(f"VR registry coverage mismatch: missing={sorted(changed-covered)}, stale={sorted(covered-changed)}")

    desktop_registry = json.loads((ROOT / "docs/vnext/OPENMW_PATCH_REGISTRY.json").read_text(encoding="utf-8"))
    desktop_patch_paths = {path for patch in desktop_registry["patches"] for path in patch["paths"]}
    vr_delta = set(git_text(
        "diff", "--name-only", integration["merge_base"], upstream["commit"]).splitlines())
    observed_overlap = desktop_patch_paths & vr_delta
    if observed_overlap != set(registry["observed_p8_overlap_paths"]):
        raise ValueError("recorded P8 overlap paths differ from the two pinned trees")

    required_resolution = required_patch
    resolution_paths: set[str] = set()
    for resolution in registry["merge_resolutions"]:
        if set(resolution) != required_resolution or not all(resolution.values()):
            raise ValueError("invalid VR merge-resolution entry")
        if resolution["id"] in ids:
            raise ValueError(f"duplicate VR registry id: {resolution['id']}")
        ids.add(resolution["id"])
        resolution_paths.update(resolution["paths"])
    merge_tree = git("merge-tree", "--write-tree", "--name-only", *parents, check=False)
    lines = merge_tree.stdout.splitlines()[1:]
    conflicts = set(lines[:lines.index("")] if "" in lines else lines)
    if conflicts != resolution_paths:
        raise ValueError(f"VR conflict record mismatch: observed={sorted(conflicts)} recorded={sorted(resolution_paths)}")

    main = (ROOT / "apps/openmw/main.cpp").read_text(encoding="utf-8")
    cmake = (ROOT / "apps/openmw/CMakeLists.txt").read_text(encoding="utf-8")
    vismask = (ROOT / "apps/openmw/mwrender/vismask.hpp").read_text(encoding="utf-8")
    rendering = (ROOT / "apps/openmw/mwrender/renderingmanager.cpp").read_text(encoding="utf-8")
    if main.count("#ifndef OPENMW_VR") < 4 or "#include <components/vr/vr.hpp>" not in main:
        raise ValueError("VR executable does not keep desktop providers excluded")
    if ("if(BUILD_OPENMW)\n    target_link_libraries(openmw TES3MP::OpenMWAdapter "
            "TES3MP::OpenMWDesktopProviders)" not in cmake):
        raise ValueError("desktop provider link guard is missing")
    if ("if(BUILD_OPENMW_VR AND TARGET openmw_vr)\n"
            "    target_link_libraries(openmw_vr TES3MP::OpenMWAdapter)" not in cmake):
        raise ValueError("VR shared-adapter link guard is missing")
    for value in ("Mask_3DGUI = (1 << 21)", "Mask_3DGUI_NonIntersectable = (1 << 22)",
                  "Mask_Pointer = (1 << 23)", "Mask_ReplicatedActor = (1 << 24)"):
        if value not in vismask:
            raise ValueError(f"missing unique VR/C-R1 mask: {value}")
    if "Mask_Groundcover | Mask_ReplicatedActor | Mask_Pointer" not in rendering:
        raise ValueError("ray traversal does not exclude pointer and replicated actors")
    if (ROOT / ".github/workflows/push.yml").exists() or not (ROOT / "README.md").read_text(encoding="utf-8").startswith("# TES3MP vNext"):
        raise ValueError("vNext workflow or README merge resolution was lost")
    if (ROOT / ".gitmodules").exists():
        raise ValueError("VR target must not add a submodule")


if __name__ == "__main__":
    try:
        verify()
    except (KeyError, OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"OpenMW VR target verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
    print("OpenMW VR target verified")
