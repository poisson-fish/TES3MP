#!/usr/bin/env python3
"""Bake and run bounded authoritative lockpick desktop evidence."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import desktop_evidence_harness as harness


ROLES = {"security-pick", "security-probe"}
LOCKPICK_PROTOTYPE = 12936841098047256804
PROBE_PROTOTYPE = 16269827911551786073
PRISON_SHIP_DOOR_REF = 397569
TRAP_PROTOTYPE = 14002894290968619553


def client_command(args: argparse.Namespace, pack: Path, port: int, password: Path,
                   role: str, evidence: Path) -> list[str]:
    return harness.build_client_command(
        args, pack, port, password, role, evidence, ROLES, "authoritative security")


def _bake(args: argparse.Namespace, root: Path) -> Path:
    source = root / "authoring"
    source.mkdir()
    for name in harness.AUTHORING_FILES:
        shutil.copy2(args.content_source / name, source / name)

    manifest = next(line.split()[1] for line in source.joinpath("vanilla-inventory.txt")
                    .read_text(encoding="utf-8").splitlines() if line.startswith("manifest "))
    objects = source / "vanilla-interactive-objects.txt"
    objects.write_text(
        "TES3MP_INTERACTIVE_OBJECTS_V2\n"
        f"manifest {manifest}\n"
        f"object 1 standard interior 1 62464 -138240 24576 0 0 4056358002 1 none {TRAP_PROTOTYPE} 1\n",
        encoding="utf-8")
    with source.joinpath("server.cfg").open("a", encoding="utf-8") as stream:
        stream.write("interactive_object_content_file = vanilla-interactive-objects.txt\n")
    mappings = root / "client-mappings.cfg"
    mappings.write_text(args.client_mappings.read_text(encoding="utf-8")
                        + f"tes3mp-content-interactive-object-map=1={PRISON_SHIP_DOOR_REF}\n",
                        encoding="utf-8")
    output = root / "pack-output"
    subprocess.run([sys.executable, str(args.baker), "bake", "--openmw-config", str(args.openmw_config),
                    "--server-config", str(source / "server.cfg"), "--client-mappings", str(mappings),
                    "--derived-pack-recipe", str(args.derived_pack_recipe), "--output", str(output)], check=True)
    baked_manifest = output.joinpath("CURRENT").read_text(encoding="ascii").strip()
    pack = output / "packs" / baked_manifest
    subprocess.run([sys.executable, str(args.baker), "verify", str(pack)], check=True)
    evidence_pack = args.artifacts / "baked-pack"
    shutil.copytree(pack, evidence_pack)
    return evidence_pack


def _validate(phase: dict, role: str, action: str, outcome: str) -> dict:
    completion = phase["roles"][0]
    records = phase["records"][role]
    submitted = [record for record in records if record.get("event") == f"security_{action}_submitted"]
    if len(submitted) != 1 or completion.get("security_submitted") is not True \
            or completion.get(outcome) is not True \
            or completion.get("resumes") != 1 \
            or completion.get("security_resumed_converged") is not True \
            or completion.get("security_initial_tool_condition", 0) \
            != completion.get("security_tool_condition", 0) + 1 \
            or completion.get("security_progress", 0) <= completion.get("security_initial_progress", 0):
        raise RuntimeError(f"authoritative security desktop convergence failed: {phase!r}")
    return {"submission": submitted[0], "completion": completion,
            "queue_drain": phase["queue_drain"]}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    harness.add_common_arguments(parser)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    harness.prepare_artifacts(args.artifacts)
    secret = "security-capture-secret"
    with tempfile.TemporaryDirectory(prefix="tes3mp-security-") as temporary:
        pack = _bake(args, Path(temporary))
        password = Path(temporary) / "join-password.txt"
        password.write_text(secret + "\n", encoding="utf-8")
        pick = harness.run_phase(args, pack, password, args.artifacts, ("security-pick",), 30,
                                 allowed_roles=ROLES, scenario="authoritative lockpicking",
                                 credential_namespace="security-pick-credential", username_prefix="lockpick",
                                 starting_inventory=((LOCKPICK_PROTOTYPE, 1, -1),),
                                 skill_overrides={18: 90})
        probe = harness.run_phase(args, pack, password, args.artifacts, ("security-probe",), 30,
                                  allowed_roles=ROLES, scenario="authoritative probe disarming",
                                  credential_namespace="security-probe-credential", username_prefix="probe",
                                  starting_inventory=((PROBE_PROTOTYPE, 1, -1),),
                                  skill_overrides={18: 90})
        manifest = json.loads(pack.joinpath("pack.json").read_text(encoding="utf-8"))["manifest_id"]
        summary = {"event": "authoritative_security_capture_passed", "manifest": manifest,
                   "lockpick": _validate(pick, "security-pick", "pick", "security_unlocked"),
                   "probe": _validate(probe, "security-probe", "probe", "security_disarmed")}
        harness.write_summary(args.artifacts, summary, secret)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
