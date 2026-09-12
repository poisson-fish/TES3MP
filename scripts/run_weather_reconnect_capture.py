#!/usr/bin/env python3
"""Bake and run bounded two-region desktop weather/reconnect evidence."""

import argparse
import json
import tempfile
import time
from pathlib import Path

import desktop_evidence_harness as harness


ROLES = {"weather-one", "weather-two", "weather-reconnect", "weather-slow"}


def client_command(args: argparse.Namespace, pack: Path, port: int, password: Path,
                   role: str, evidence: Path) -> list[str]:
    return harness.build_client_command(
        args, pack, port, password, role, evidence, ROLES, "weather")


def _run_phase(args: argparse.Namespace, pack: Path, password: Path, artifacts: Path,
               roles: tuple[str, ...], timeout: int, sample_rss: bool = False) -> dict:
    return harness.run_phase(
        args, pack, password, artifacts, roles, timeout,
        allowed_roles=ROLES,
        scenario="weather",
        credential_namespace="weather-credential",
        username_prefix="weather",
        omitted_config_keys={"inventory_content_file", "combat_content_file", "script_package_file"},
        sample_rss=sample_rss,
    )


def _state_key(sample: dict) -> tuple:
    return tuple((region["region"], region["current"], region["target"], region["revision"],
                  region["transition_start"], region["transition_end"], region["next_selection"])
                 for region in sample["regions"])


def _validate_pair(phase: dict) -> dict:
    samples = {}
    for role in ("weather-one", "weather-two"):
        values = [record for record in phase["records"][role] if record.get("event") == "weather_sample"]
        if not values or any(len(value.get("regions", [])) != 2 for value in values):
            raise RuntimeError(f"two-region evidence missing for {role}")
        samples[role] = values
    common = set(map(_state_key, samples["weather-one"])) & set(map(_state_key, samples["weather-two"]))
    transitions = [value for value in common if any(region[1] != region[2] for region in value)]
    if not transitions:
        raise RuntimeError("desktop clients did not present an identical transition state")
    return {"identical_transition": [list(region) for region in transitions[0]]}


def _validate_reconnect(phase: dict) -> dict:
    complete = phase["roles"][0]
    if complete.get("resumes") != 1 or complete.get("weather_resumed_converged") is not True \
            or complete.get("weather_duplicate_presentations") != 0:
        raise RuntimeError(f"weather resume convergence failed: {complete!r}")
    samples = [record for record in phase["records"]["weather-reconnect"]
               if record.get("event") == "weather_sample"]
    return {"before": samples[0], "after": samples[-1], "completion": complete}


def _validate_slow(phase: dict) -> dict:
    complete = phase["roles"][0]
    events = {record.get("event") for record in phase["records"]["weather-slow"]}
    if complete.get("slow_peer_recovered") is not True or complete.get("weather_duplicate_presentations") != 0 \
            or not {"weather_slow_peer_stall_started", "weather_slow_peer_recovered"}.issubset(events):
        raise RuntimeError(f"slow-peer recovery failed: {complete!r}")
    return {"completion": complete, "memory": phase["memory"], "queue_drain": phase["queue_drain"]}


def _weather_fixture(text: str) -> str:
    text = text.replace("weather_seed 1592594996", "weather_seed 3")
    text = text.replace("weather_region 1 1 9000 300 1 2", "weather_region 1 1 600 60 1 2")
    return text.replace("weather_region 2 1 9000 300 1 2", "weather_region 2 1 600 60 1 2")


def _bake(args: argparse.Namespace, root: Path) -> Path:
    return harness.bake_pack(args, root, _weather_fixture)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    harness.add_common_arguments(parser)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    harness.prepare_artifacts(args.artifacts)
    secret = "weather-capture-secret"
    with tempfile.TemporaryDirectory(prefix="tes3mp-weather-") as temporary:
        root = Path(temporary)
        pack = _bake(args, root)
        password = root / "join-password.txt"
        password.write_text(secret + "\n", encoding="utf-8")
        pair = _run_phase(args, pack, password, args.artifacts, ("weather-one", "weather-two"), 35)
        time.sleep(3.25)
        reconnect = _run_phase(args, pack, password, args.artifacts, ("weather-reconnect",), 35)
        time.sleep(3.25)
        slow = _run_phase(args, pack, password, args.artifacts, ("weather-slow",), 35, sample_rss=True)
        manifest = json.loads(pack.joinpath("pack.json").read_text(encoding="utf-8"))["manifest_id"]
        summary = {"event": "weather_reconnect_capture_passed", "manifest": manifest,
                   "two_client": _validate_pair(pair), "reconnect": _validate_reconnect(reconnect),
                   "slow_peer": _validate_slow(slow)}
        harness.write_summary(args.artifacts, summary, secret)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
