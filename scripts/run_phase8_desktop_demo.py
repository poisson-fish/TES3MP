#!/usr/bin/env python3
"""Run the content-backed Phase 8 two-client OpenMW proof."""

import argparse
import json
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from run_phase7_join_demo import bounded_rss, resident_bytes


ROLES = {"flow-one", "flow-two", "reconnect", "soak-one", "soak-two"}


def client_command(args: argparse.Namespace, port: int, password: Path,
                   role: str, evidence: Path) -> list[str]:
    if role not in ROLES:
        raise ValueError(f"unknown desktop role: {role}")
    command = [
        str(args.openmw),
        "--tes3mp-enable=1",
        "--tes3mp-host=127.0.0.1",
        f"--tes3mp-port={port}",
        "--tes3mp-timeout-ms=5000",
        f"--tes3mp-password-file={password}",
        f"--tes3mp-fixture-interior={args.interior}",
        f"--tes3mp-fixture-worldspace={args.worldspace}",
        f"--tes3mp-fixture-avatar={args.avatar}",
        f"--tes3mp-automation-role={role}",
        f"--tes3mp-automation-output={evidence}",
        f"--resources={args.resources}",
        f"--start={args.interior}",
        "--skip-menu=1",
        "--new-game=1",
        "--no-sound=1",
        "--no-grab=1",
    ]
    for data in args.data:
        command.append(f"--data={data}")
    for archive in args.fallback_archive:
        command.append(f"--fallback-archive={archive}")
    for content in args.content:
        command.append(f"--content={content}")
    return command


def read_completion(path: Path, role: str) -> dict:
    records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    completed = [record for record in records if record.get("event") == "phase8_desktop_complete"]
    if len(records) > 128 or len(completed) != 1:
        raise RuntimeError(f"bounded completion evidence missing: role={role}")
    result = completed[0]
    if result.get("role") != role or result.get("success") is not True:
        raise RuntimeError(f"desktop role failed: role={role} evidence={result!r}")
    return result


def run_clients(args: argparse.Namespace, port: int, password: Path,
                artifacts: Path, roles: tuple[str, ...], timeout: int,
                sample_rss: bool = False,
                server: subprocess.Popen[str] | None = None) -> tuple[list[dict], dict[str, dict[str, int]]]:
    processes: list[subprocess.Popen[bytes]] = []
    paths: list[Path] = []
    samples: dict[str, list[int]] = {}
    try:
        for role in roles:
            path = artifacts / f"{role}.ndjson"
            paths.append(path)
            process = subprocess.Popen(
                client_command(args, port, password, role, path),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            processes.append(process)
            samples[role] = []
        deadline = time.monotonic() + timeout
        server_samples: list[int] = []
        while any(process.poll() is None for process in processes):
            if time.monotonic() >= deadline:
                raise RuntimeError(f"desktop roles timed out: roles={roles!r}")
            if sample_rss:
                if server is not None and server.poll() is None:
                    server_samples.append(resident_bytes(server.pid))
                for role, process in zip(roles, processes):
                    if process.poll() is None:
                        samples[role].append(resident_bytes(process.pid))
            time.sleep(1 if sample_rss else 0.1)
        if any(process.returncode != 0 for process in processes):
            raise RuntimeError(
                f"desktop process failed: roles={roles!r} codes={[process.returncode for process in processes]!r}")
        evidence = [read_completion(path, role) for path, role in zip(paths, roles)]
        memory = {role: bounded_rss(values) for role, values in samples.items()} if sample_rss else {}
        if sample_rss and server is not None:
            memory["server"] = bounded_rss(server_samples)
        return evidence, memory
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--openmw", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--data", type=Path, action="append", required=True)
    parser.add_argument("--fallback-archive", action="append", default=[])
    parser.add_argument("--content", action="append", required=True)
    parser.add_argument("--interior", required=True)
    parser.add_argument("--worldspace", required=True)
    parser.add_argument("--avatar", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    secret = "phase8-desktop-secret"
    with tempfile.TemporaryDirectory(prefix="tes3mp-phase8-") as temporary:
        root = Path(temporary)
        password = root / "join-password"
        config = root / "server.cfg"
        password.write_text(secret + "\n", encoding="utf-8")
        config.write_text(
            f"bind_address=127.0.0.1\nport={port}\ntick_interval_ms=16\n"
            f"disconnect_grace_ms=3000\njoin_password_file={password.as_posix()}\n",
            encoding="utf-8",
        )
        server = subprocess.Popen(
            [str(args.server), str(config)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            time.sleep(0.5)
            flow, _ = run_clients(
                args, port, password, args.artifacts, ("flow-one", "flow-two"), 30)
            reconnect, _ = run_clients(
                args, port, password, args.artifacts, ("reconnect",), 90)
            if reconnect[0].get("resumes") != 32:
                raise RuntimeError(f"desktop reconnect threshold failed: {reconnect[0]!r}")
            soak, memory = run_clients(
                args, port, password, args.artifacts, ("soak-one", "soak-two"), 80,
                sample_rss=True, server=server)
        finally:
            server.terminate()
            try:
                server_output = server.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server_output = server.communicate(timeout=5)
        if server.returncode not in (0, 1):
            raise RuntimeError(f"server shutdown failed: code={server.returncode}")
        server_records = [
            json.loads(line) for line in server_output[0].splitlines() if line.startswith("{")
        ]
        queue = next((record for record in server_records if record.get("event") == "phase7_queue_drain"), None)
        if not queue or any(queue.get(field) != 0 for field in (
            "final_reliable_messages", "final_reliable_bytes",
            "final_latest_messages", "final_latest_bytes",
        )):
            raise RuntimeError("server queue drain evidence missing or nonzero")
        for field, cap in {
            "reliable_high_water_messages": 256,
            "reliable_high_water_bytes": 4 * 1024 * 1024,
            "latest_high_water_messages": 1,
            "latest_high_water_bytes": 65536,
        }.items():
            if not 0 < queue.get(field, 0) <= cap:
                raise RuntimeError(f"server queue bound failed: field={field} evidence={queue!r}")
        summary = {
            "event": "phase8_desktop_demo_passed",
            "flow": flow,
            "reconnect": reconnect[0],
            "soak": soak,
            "memory": memory,
            "queue_drain": queue,
        }
        summary_path = args.artifacts / "summary.json"
        summary_path.write_text(json.dumps(summary, separators=(",", ":")) + "\n", encoding="utf-8")
        for artifact in args.artifacts.iterdir():
            if artifact.is_file() and secret.encode() in artifact.read_bytes():
                raise RuntimeError(f"credential leaked to artifact: {artifact.name}")
        print(json.dumps(summary, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
