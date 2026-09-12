#!/usr/bin/env python3
"""Bake and run bounded authoritative enchanted-item desktop evidence."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import desktop_evidence_harness as harness


ROLES = {"magic-item"}
ITEM_RECORD = "ring of fleabite"
ITEM_PROTOTYPE = 13568541167308910850


def _bake(args: argparse.Namespace, root: Path) -> Path:
    source = root / "authoring"
    source.mkdir()
    for name in harness.AUTHORING_FILES:
        shutil.copy2(args.content_source / name, source / name)

    recipe = json.loads(args.derived_pack_recipe.read_text(encoding="utf-8"))
    if ITEM_RECORD not in recipe["items"]:
        recipe["items"].append(ITEM_RECORD)
    recipe_path = root / "magic-derived-pack.json"
    recipe_path.write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")

    mappings = root / "client-mappings.cfg"
    mappings.write_text(args.client_mappings.read_text(encoding="utf-8")
                        + f"tes3mp-content-item-prototype-map={ITEM_PROTOTYPE}={ITEM_RECORD}\n",
                        encoding="utf-8")
    output = root / "pack-output"
    subprocess.run([sys.executable, str(args.baker), "bake",
                    "--openmw-config", str(args.openmw_config),
                    "--server-config", str(source / "server.cfg"),
                    "--client-mappings", str(mappings),
                    "--derived-pack-recipe", str(recipe_path), "--output", str(output)], check=True)
    manifest = output.joinpath("CURRENT").read_text(encoding="ascii").strip()
    pack = output / "packs" / manifest
    subprocess.run([sys.executable, str(args.baker), "verify", str(pack)], check=True)
    evidence_pack = args.artifacts / "baked-pack"
    shutil.copytree(pack, evidence_pack)
    return evidence_pack


def _validate(phase: dict) -> dict:
    completion = phase["roles"][0]
    records = phase["records"]["magic-item"]
    submitted = [record for record in records if record.get("event") == "magic_item_submitted"]
    if len(submitted) != 1 or completion.get("magic_submitted") is not True \
            or completion.get("magic_event_presented") is not True \
            or completion.get("resumes") != 1 \
            or completion.get("magic_resumed_converged") is not True \
            or completion.get("magic_initial_charge", 0) <= completion.get("magic_charge", 0) \
            or completion.get("magic_initial_target_fatigue", 0) \
            <= completion.get("magic_minimum_target_fatigue", 0) \
            or completion.get("magic_enchant_progress", 0) \
            <= completion.get("magic_initial_enchant_progress", 0):
        raise RuntimeError(f"authoritative magic desktop convergence failed: {phase!r}")
    return {"submission": submitted[0], "completion": completion,
            "queue_drain": phase["queue_drain"]}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    harness.add_common_arguments(parser)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    harness.prepare_artifacts(args.artifacts)
    secret = "magic-use-capture-secret"
    with tempfile.TemporaryDirectory(prefix="tes3mp-magic-use-") as temporary:
        pack = _bake(args, Path(temporary))
        password = Path(temporary) / "join-password.txt"
        password.write_text(secret + "\n", encoding="utf-8")
        phase = harness.run_phase(args, pack, password, args.artifacts, ("magic-item",), 30,
                                  allowed_roles=ROLES, scenario="authoritative enchanted-item use",
                                  credential_namespace="magic-item-credential", username_prefix="magic",
                                  starting_inventory=((ITEM_PROTOTYPE, 1, -1),),
                                  skill_overrides={9: 90})
        manifest = json.loads(pack.joinpath("pack.json").read_text(encoding="utf-8"))["manifest_id"]
        summary = {"event": "authoritative_magic_use_capture_passed", "manifest": manifest,
                   "enchanted_item": _validate(phase)}
        harness.write_summary(args.artifacts, summary, secret)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
