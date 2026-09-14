#!/usr/bin/env python3
"""Bake and run bounded authoritative timed-spell desktop evidence."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import desktop_evidence_harness as harness


ROLES = {"magic-spell-caster", "magic-spell-target"}
SPELL_ID = 13128897029866312148


def _bake(args: argparse.Namespace, root: Path) -> Path:
    source = root / "authoring"
    source.mkdir()
    for name in harness.AUTHORING_FILES:
        shutil.copy2(args.content_source / name, source / name)

    characters = source / "vanilla-characters.txt"
    character_lines = []
    for line in characters.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if fields and fields[0] == "race":
            spell_count = int(fields[45])
            spells = sorted({int(value) for value in fields[46:46 + spell_count]} | {SPELL_ID})
            fields = fields[:45] + [str(len(spells)), *(str(value) for value in spells)]
            line = " ".join(fields)
        character_lines.append(line)
    characters.write_text("\n".join(character_lines) + "\n", encoding="utf-8", newline="\n")

    recipe = json.loads(args.derived_pack_recipe.read_text(encoding="utf-8"))
    recipe_path = root / "magic-derived-pack.json"
    recipe_path.write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")

    mappings = root / "client-mappings.cfg"
    mappings.write_text(args.client_mappings.read_text(encoding="utf-8"), encoding="utf-8")
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
    caster, target = phase["roles"]
    records = phase["records"]["magic-spell-caster"]
    submitted = [record for record in records if record.get("event") == "magic_spell_submitted"]
    lifecycle = ("magic_effect_started", "magic_effect_updated", "magic_effect_ended")
    if len(submitted) != 1 or submitted[0].get("spell_id") != SPELL_ID \
            or caster.get("magic_submitted") is not True or caster.get("magic_event_presented") is not True \
            or caster.get("resumes") != 1 or caster.get("magic_effect_active_after_resume") is not True \
            or any(caster.get(field) is not True for field in lifecycle) \
            or target.get("resumes") != 0 or target.get("magic_event_presented") is not True \
            or any(target.get(field) is not True for field in lifecycle) \
            or caster.get("magic_applied_delta", 0) >= 0 or target.get("magic_applied_delta", 0) >= 0:
        raise RuntimeError(f"authoritative magic desktop convergence failed: {phase!r}")
    return {"submission": submitted[0], "caster": caster, "target": target,
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
        phase = harness.run_phase(args, pack, password, args.artifacts,
                                  ("magic-spell-caster", "magic-spell-target"), 35,
                                  allowed_roles=ROLES, scenario="authoritative timed player-target spell",
                                  credential_namespace="magic-spell-credential", username_prefix="magic",
                                  starting_spells=(SPELL_ID,))
        manifest = json.loads(pack.joinpath("pack.json").read_text(encoding="utf-8"))["manifest_id"]
        summary = {"event": "authoritative_magic_use_capture_passed", "manifest": manifest,
                   "timed_player_spell": _validate(phase)}
        harness.write_summary(args.artifacts, summary, secret)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
