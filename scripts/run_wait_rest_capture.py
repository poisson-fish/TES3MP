#!/usr/bin/env python3
"""Bake and run bounded authoritative wait/rest desktop evidence."""

import argparse
import json
import tempfile
from pathlib import Path

import desktop_evidence_harness as harness


ROLES = {"wait-one", "wait-two", "wait-anchor", "wait-reconnect", "wait-slow-anchor", "wait-slow"}


def client_command(args: argparse.Namespace, pack: Path, port: int, password: Path,
                   role: str, evidence: Path) -> list[str]:
    return harness.build_client_command(
        args, pack, port, password, role, evidence, ROLES, "wait/rest")


def _run_phase(args: argparse.Namespace, pack: Path, password: Path, artifacts: Path,
               roles: tuple[str, ...], timeout: int, sample_rss: bool = False) -> dict:
    return harness.run_phase(
        args, pack, password, artifacts, roles, timeout,
        allowed_roles=ROLES,
        scenario="wait/rest",
        credential_namespace="wait-rest-credential",
        username_prefix="wait",
        sample_rss=sample_rss,
    )


def _time_samples(phase: dict, role: str) -> list[dict]:
    return [record for record in phase["records"][role] if record.get("event") == "wait_rest_time_sample"]


def _state_key(sample: dict) -> tuple[int, ...]:
    return (sample["revision"], sample["day"], sample["month"], sample["year"],
            sample["milliseconds_since_midnight"])


def _validate_pair(phase: dict) -> dict:
    samples = {role: _time_samples(phase, role) for role in ("wait-one", "wait-two")}
    common = set(map(_state_key, samples["wait-one"])) & set(map(_state_key, samples["wait-two"]))
    rollovers = [value for value in common if value[3] == 428 and value[2] == 0 and value[1] == 1]
    completions = phase["roles"]
    if not rollovers or any(item.get("world_time_duplicate_presentations") != 0 for item in completions):
        raise RuntimeError(f"identical duplicate-free rollover missing: {phase!r}")
    return {"identical_rollover": rollovers[0], "completions": completions}


def _validate_reconnect(phase: dict) -> dict:
    by_role = dict(zip(("wait-anchor", "wait-reconnect"), phase["roles"]))
    resumed = by_role["wait-reconnect"]
    samples = _time_samples(phase, "wait-reconnect")
    if resumed.get("resumes") != 1 or resumed.get("world_time_resumed_converged") is not True \
            or resumed.get("world_time_duplicate_presentations") != 0 or len(samples) < 2 \
            or samples[0]["year"] != 427 or samples[-1]["year"] != 428:
        raise RuntimeError(f"wait/rest resume convergence failed: {phase!r}")
    return {"before": samples[0], "after": samples[-1], "completion": resumed}


def _validate_slow(phase: dict) -> dict:
    slow = phase["roles"][1]
    events = {record.get("event") for record in phase["records"]["wait-slow"]}
    if slow.get("slow_peer_recovered") is not True or slow.get("world_time_duplicate_presentations") != 0 \
            or not {"wait_slow_peer_stall_started", "wait_slow_peer_recovered"}.issubset(events):
        raise RuntimeError(f"wait/rest slow-peer recovery failed: {phase!r}")
    return {"completion": slow, "memory": phase["memory"], "queue_drain": phase["queue_drain"]}


def _wait_rest_fixture(text: str) -> str:
    original = "time 16 6 427 32400000 30000"
    if original not in text:
        raise RuntimeError("expected authoring world-time fixture is missing")
    return text.replace(original, "time 30 11 427 82800000 30000")


def _bake(args: argparse.Namespace, root: Path) -> Path:
    return harness.bake_pack(args, root, _wait_rest_fixture)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    harness.add_common_arguments(parser)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    harness.prepare_artifacts(args.artifacts)
    secret = "wait-rest-capture-secret"
    with tempfile.TemporaryDirectory(prefix="tes3mp-wait-rest-") as temporary:
        pack = _bake(args, Path(temporary))
        password = Path(temporary) / "join-password.txt"
        password.write_text(secret + "\n", encoding="utf-8")
        pair = _run_phase(args, pack, password, args.artifacts, ("wait-one", "wait-two"), 30)
        reconnect = _run_phase(args, pack, password, args.artifacts, ("wait-anchor", "wait-reconnect"), 35)
        slow = _run_phase(
            args, pack, password, args.artifacts, ("wait-slow-anchor", "wait-slow"), 30, sample_rss=True)
        manifest = json.loads(pack.joinpath("pack.json").read_text(encoding="utf-8"))["manifest_id"]
        summary = {"event": "authoritative_wait_rest_capture_passed", "manifest": manifest,
                   "two_client": _validate_pair(pair), "reconnect": _validate_reconnect(reconnect),
                   "slow_peer": _validate_slow(slow)}
        harness.write_summary(args.artifacts, summary, secret)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
