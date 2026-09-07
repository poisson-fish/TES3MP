#!/usr/bin/env python3
"""Run the bounded content-backed Phase 13 two-client actor lifecycle proof."""

import argparse
import json
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from run_phase7_join_demo import TEST_CONTENT_MANIFEST
from run_phase8_desktop_demo import DISCONNECT_GRACE_SECONDS, PHASE_SETTLE_SECONDS


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def client_command(args: argparse.Namespace, port: int, password: Path, role: str,
                   label: str, identity: str) -> list[str]:
    user = args.artifacts / f"{label}-user"
    credential = args.artifacts / f"{identity}-identity" / "player-credential"
    command = [
        str(args.openmw), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1", f"--tes3mp-port={port}",
        "--tes3mp-timeout-ms=5000", f"--tes3mp-password-file={password}",
        f"--tes3mp-player-credential-file={credential}",
        f"--tes3mp-content-manifest-id={TEST_CONTENT_MANIFEST}",
        "--tes3mp-content-cell-spaces=interior:7;exterior:8",
        "--tes3mp-content-allowed-cells=interior:7;exterior:8:0:0",
        "--tes3mp-content-appearance-id=1",
        "--tes3mp-content-movement-profile=sneak:1024;walk:4097;run:8192;jump:4096",
        f"--tes3mp-content-cell-space-map=7={args.interior}",
        f"--tes3mp-content-cell-space-map=8={args.worldspace}",
        f"--tes3mp-content-appearance-record={args.avatar}",
        f"--tes3mp-content-actor-prototype-map=1={args.actor_prototype}",
        f"--tes3mp-automation-role={role}",
        f"--tes3mp-automation-output={args.artifacts / (label + '.ndjson')}",
        f"--resources={args.resources}", f"--user-data={user}", f"--start={args.interior}",
        "--skip-menu=1", "--new-game=1", "--no-sound=1", "--no-grab=1",
    ]
    command += [f"--data={value}" for value in args.data]
    command += [f"--fallback-archive={value}" for value in args.fallback_archive]
    command += [f"--content={value}" for value in args.content]
    return command


def write_server_config(root: Path, port: int, password: Path) -> Path:
    collision = root / "collision-content"
    collision.write_text(
        f"TES3MP_COLLISION_V1\nmanifest {TEST_CONTENT_MANIFEST}\n"
        "cell interior 7\ncell exterior 8 0 0\n", encoding="utf-8")
    actors = root / "actor-content"
    actors.write_text(
        f"TES3MP_ACTORS_V1\nmanifest {TEST_CONTENT_MANIFEST}\n"
        "actor 1 9001 1 interior 7 0 0 0 0 0 0 wander 0 0 0 1000000 0 0\n",
        encoding="utf-8")
    config = root / "server.cfg"
    config.write_text(
        f"bind_address=127.0.0.1\nport={port}\ntick_interval_ms=16\n"
        f"disconnect_grace_ms={int(DISCONNECT_GRACE_SECONDS * 1000)}\n"
        f"join_password_file={password.as_posix()}\ncontent_manifest_id={TEST_CONTENT_MANIFEST}\n"
        "cell_spaces=interior:7;exterior:8\nallowed_cells=interior:7;exterior:8:0:0\n"
        "spawn_cell=interior:7\ndefault_appearance_id=1\n"
        "movement_profile=sneak:1024;walk:4097;run:8192;jump:4096\n"
        f"collision_content_file={collision.as_posix()}\nactor_content_file={actors.as_posix()}\n"
        f"player_identity_file={(root / 'player-identities').as_posix()}\n", encoding="utf-8")
    return config


def read_evidence(path: Path, role: str) -> tuple[dict, list[dict]]:
    records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    completed = [record for record in records if record.get("event") == "phase8_desktop_complete"]
    if len(records) > 128 or len(completed) != 1:
        raise RuntimeError(f"bounded actor evidence missing: role={role}")
    result = completed[0]
    if result.get("role") != role or result.get("success") is not True:
        raise RuntimeError(f"actor role failed: role={role} evidence={result!r}")
    samples = [record for record in records if record.get("event") == "phase13_actor_sample"]
    return result, samples


def run_clients(args: argparse.Namespace, port: int, password: Path,
                specifications: tuple[tuple[str, str, str], ...], timeout: int,
                environment: dict[str, str]) -> list[tuple[dict, list[dict]]]:
    processes = []
    streams = []
    try:
        for label, role, identity in specifications:
            (args.artifacts / f"{label}-user").mkdir(exist_ok=True)
            (args.artifacts / f"{identity}-identity").mkdir(exist_ok=True)
            stdout = (args.artifacts / f"{label}.stdout.log").open("wb")
            stderr = (args.artifacts / f"{label}.stderr.log").open("wb")
            streams.extend((stdout, stderr))
            process = subprocess.Popen(
                client_command(args, port, password, role, label, identity),
                stdout=stdout, stderr=stderr, env=environment)
            processes.append((label, role, process))
        deadline = time.monotonic() + timeout
        while any(process.poll() is None for _, _, process in processes):
            if time.monotonic() >= deadline:
                raise RuntimeError(f"actor lifecycle clients timed out: {specifications!r}")
            time.sleep(0.1)
        codes = {label: process.returncode for label, _, process in processes}
        if any(code != 0 for code in codes.values()):
            raise RuntimeError(f"actor lifecycle client failed: {codes!r}")
        return [read_evidence(args.artifacts / f"{label}.ndjson", role)
                for label, role, _ in processes]
    finally:
        for _, _, process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        for stream in streams:
            stream.close()


def require_actor(result: dict, *, leave: bool = False, resume: bool = False,
                  resync: bool = False) -> None:
    required = ("actor_stable", "actor_moved", "player_identity_stable")
    if any(result.get(field) is not True for field in required):
        raise RuntimeError(f"actor continuity failed: {result!r}")
    if min(result.get("actor_id", 0), result.get("actor_entity_id", 0),
           result.get("actor_prototype_id", 0), result.get("actor_first_revision", 0)) <= 0:
        raise RuntimeError(f"actor identity evidence missing: {result!r}")
    if leave and (result.get("actor_saw_leave") is not True or result.get("actor_saw_return") is not True):
        raise RuntimeError(f"actor cell lifecycle failed: {result!r}")
    if resume and result.get("actor_resume_snapshots") != 4:
        raise RuntimeError(f"actor resume lifecycle failed: {result!r}")
    if resync and (result.get("resync_completed") is not True
                   or result.get("actor_after_resync") is not True):
        raise RuntimeError(f"actor resync lifecycle failed: {result!r}")


def common_samples(left: list[dict], right: list[dict]) -> int:
    fields = ("actor_id", "entity_id", "prototype_id", "tick", "x", "y", "z")
    left_by_revision = {sample["revision"]: tuple(sample[field] for field in fields) for sample in left}
    right_by_revision = {sample["revision"]: tuple(sample[field] for field in fields) for sample in right}
    shared = left_by_revision.keys() & right_by_revision.keys()
    if len(shared) < 4 or any(left_by_revision[revision] != right_by_revision[revision] for revision in shared):
        raise RuntimeError(f"two-client canonical actor samples diverged: shared={len(shared)}")
    return len(shared)


def run_capture(args: argparse.Namespace) -> dict:
    args.artifacts.mkdir(parents=True, exist_ok=True)
    port = free_port()
    environment = os.environ.copy()
    if args.runtime_dir:
        environment["PATH"] = os.pathsep.join(map(str, args.runtime_dir)) + os.pathsep + environment.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="tes3mp-phase13-actor-") as temporary:
        root = Path(temporary)
        password = root / "join-password"
        password.write_text("phase13-actor-capture-secret\n", encoding="utf-8")
        config = write_server_config(root, port, password)
        server_stdout = (args.artifacts / "server.stdout.log").open("wb")
        server_stderr = (args.artifacts / "server.stderr.log").open("wb")
        server = subprocess.Popen([str(args.server), str(config)], stdout=server_stdout,
                                  stderr=server_stderr, env=environment)
        try:
            time.sleep(0.5)
            flow = run_clients(args, port, password,
                (("flow-one", "flow-one", "flow-one"), ("flow-two", "flow-two", "flow-two")),
                args.timeout, environment)
            require_actor(flow[0][0], leave=True)
            require_actor(flow[1][0])
            shared_samples = common_samples(flow[0][1], flow[1][1])
            time.sleep(PHASE_SETTLE_SECONDS)
            reconnect = run_clients(args, port, password,
                (("reconnect", "actor-reconnect", "continuity"),), args.reconnect_timeout, environment)[0]
            require_actor(reconnect[0], resume=True, resync=True)
            time.sleep(PHASE_SETTLE_SECONDS)
            authenticated = run_clients(args, port, password,
                (("authenticated-resync", "actor-auth", "continuity"),), args.timeout, environment)[0]
            require_actor(authenticated[0], resync=True)
        finally:
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
            server_stdout.close()
            server_stderr.close()
        if server.returncode not in (0, 1):
            raise RuntimeError(f"actor lifecycle server failed: {server.returncode}")

    results = (flow[0][0], flow[1][0], reconnect[0], authenticated[0])
    identity = (reconnect[0]["player_id"], reconnect[0]["player_entity_id"])
    if identity != (authenticated[0]["player_id"], authenticated[0]["player_entity_id"]):
        raise RuntimeError("durable player identity changed across authenticated rejoin")
    actor_identity = (results[0]["actor_id"], results[0]["actor_entity_id"], results[0]["actor_prototype_id"])
    if any((result["actor_id"], result["actor_entity_id"], result["actor_prototype_id"])
           != actor_identity for result in results[1:]):
        raise RuntimeError("actor identity changed across lifecycle phases")
    revisions = [results[0]["actor_last_revision"], reconnect[0]["actor_first_revision"],
                 reconnect[0]["actor_last_revision"], authenticated[0]["actor_first_revision"]]
    if revisions != sorted(revisions):
        raise RuntimeError(f"actor revision reset across lifecycle phases: {revisions!r}")
    summary = {
        "event": "phase13_actor_lifecycle_passed",
        "actor": {"actor_id": actor_identity[0], "entity_id": actor_identity[1],
                  "prototype_id": actor_identity[2], "revision_checkpoints": revisions},
        "player_identity": {"player_id": identity[0], "entity_id": identity[1]},
        "two_client_common_samples": shared_samples,
        "flow": [flow[0][0], flow[1][0]],
        "reconnect": reconnect[0],
        "authenticated_resync": authenticated[0],
    }
    (args.artifacts / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    secret = b"phase13-actor-capture-secret"
    for artifact in args.artifacts.iterdir():
        if artifact.is_file() and secret in artifact.read_bytes():
            raise RuntimeError(f"credential leaked to artifact: {artifact.name}")
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--openmw", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--data", type=Path, action="append", required=True)
    parser.add_argument("--fallback-archive", action="append", default=[])
    parser.add_argument("--content", action="append", required=True)
    parser.add_argument("--interior", required=True)
    parser.add_argument("--worldspace", required=True)
    parser.add_argument("--avatar", required=True)
    parser.add_argument("--actor-prototype", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--reconnect-timeout", type=int, default=90)
    return parser.parse_args()


def main() -> int:
    result = run_capture(parse_args())
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
