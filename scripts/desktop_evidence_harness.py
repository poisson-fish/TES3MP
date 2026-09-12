#!/usr/bin/env python3
"""Reusable process, content, and artifact harness for TES3MP desktop evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from collections.abc import Callable
from pathlib import Path

from run_phase7_join_demo import bounded_rss, resident_bytes


AUTHORING_FILES = (
    "server.cfg", "vanilla-actors.txt", "vanilla-characters.txt", "vanilla-collision.txt",
    "vanilla-combat.txt", "vanilla-inventory.txt", "vanilla-script-module.t3sm",
    "vanilla-scripts.txt", "vanilla-world.txt",
)


def build_client_command(args: argparse.Namespace, pack: Path, port: int, password: Path,
                         role: str, evidence: Path, allowed_roles: set[str],
                         scenario: str) -> list[str]:
    if role not in allowed_roles:
        raise ValueError(f"unknown {scenario} role: {role}")
    command = [
        str(args.openmw), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1",
        f"--tes3mp-port={port}", "--tes3mp-timeout-ms=8000",
        f"--tes3mp-password-file={password}",
        f"--tes3mp-player-credential-file={evidence.parent / (role + '-player-credential')}",
        f"--tes3mp-automation-role={role}", f"--tes3mp-automation-output={evidence}",
        f"--resources={args.resources}", f"--user-data={evidence.parent / (role + '-user')}",
        "--start=Imperial Prison Ship", "--skip-menu=1", "--new-game=1", "--no-sound=1", "--no-grab=1",
    ]
    for raw in pack.joinpath("openmw.cfg").read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            command.append("--" + line)
    command.extend(f"--data={value}" for value in args.data)
    command.extend(f"--fallback-archive={value}" for value in args.fallback_archive)
    command.extend(f"--content={value}" for value in args.content)
    return command


def _client_environment(args: argparse.Namespace) -> dict[str, str]:
    environment = os.environ.copy()
    dependency_bin = Path(__file__).resolve().parent.parent / "deps" / "installed" / "x64-windows" / "bin"
    environment["PATH"] = os.pathsep.join((str(args.openmw.resolve().parent), str(dependency_bin),
                                            environment.get("PATH", "")))
    environment["OSG_LIBRARY_PATH"] = str(dependency_bin)
    return environment


def _free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def _runtime_config(pack: Path, output: Path, port: int, password: Path,
                    omitted_config_keys: set[str]) -> Path:
    replacements = {
        "bind_address": "127.0.0.1", "port": str(port), "disconnect_grace_ms": "30000",
        "join_password_file": password.resolve().as_posix(),
        "player_identity_file": output.joinpath("players.txt").resolve().as_posix(),
    }
    lines = []
    for raw in pack.joinpath("server.cfg").read_text(encoding="utf-8").splitlines():
        key = raw.partition("=")[0].strip()
        if key in omitted_config_keys:
            continue
        if key in replacements:
            lines.append(f"{key} = {replacements[key]}")
        elif key.endswith("_content_file") or key == "script_package_file":
            content_path = pack.joinpath(raw.partition("=")[2].strip()).resolve()
            lines.append(f"{key} = {content_path.as_posix()}")
        else:
            lines.append(raw)
    path = output / "server.cfg"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def _seed_established_players(args: argparse.Namespace, pack: Path, phase: Path,
                              roles: tuple[str, ...], credential_namespace: str,
                              username_prefix: str) -> None:
    template_path = Path(__file__).resolve().parent / "fixtures" / "desktop_evidence_established_player_v5.txt"
    template_lines = [line for line in template_path.read_text(encoding="utf-8").splitlines()
                      if line and not line.startswith("TES3MP_")]
    if not template_lines:
        raise RuntimeError(f"established vanilla player template missing: {template_path}")
    template = template_lines[0].split()
    if len(template) < 10 or template[5] != "1":
        raise RuntimeError("established vanilla player template is invalid")
    manifest = json.loads(pack.joinpath("pack.json").read_text(encoding="utf-8"))["manifest_id"]
    records = []
    for index, role in enumerate(roles):
        credential = hashlib.sha256(f"{credential_namespace}:{role}".encode()).digest()
        phase.joinpath(role + "-player-credential").write_bytes(credential)
        tokens = template.copy()
        tokens[0] = str(index + 1)
        tokens[1] = str(1 if index == 0 else 3)
        tokens[3] = manifest
        tokens[4] = hashlib.sha256(credential).hexdigest()
        tokens[6:16] = ["0", "1", "0", "0", "62464", "-138240", "24576", "0", "0", "4056358002"]
        old_name = tokens[-1]
        new_name = (username_prefix + str(index + 1)).encode().hex()
        records.append(" ".join(new_name if token == old_name else token for token in tokens))
    phase.joinpath("players.txt").write_text(
        "TES3MP_PLAYER_IDENTITIES_V5\n" + "\n".join(records) + "\n", encoding="utf-8", newline="\n")


def _completion(path: Path, role: str, scenario: str) -> tuple[dict, list[dict]]:
    records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    completed = [record for record in records if record.get("event") == "phase8_desktop_complete"]
    if len(records) > 128 or len(completed) != 1 or completed[0].get("role") != role:
        raise RuntimeError(f"bounded desktop completion missing: role={role}")
    if completed[0].get("success") is not True:
        raise RuntimeError(f"desktop {scenario} role failed: {completed[0]!r}")
    return completed[0], records


def run_phase(args: argparse.Namespace, pack: Path, password: Path, artifacts: Path,
              roles: tuple[str, ...], timeout: int, *, allowed_roles: set[str],
              scenario: str, credential_namespace: str, username_prefix: str,
              omitted_config_keys: set[str] | None = None,
              sample_rss: bool = False) -> dict:
    phase = artifacts / "-".join(roles)
    phase.mkdir(parents=True, exist_ok=True)
    _seed_established_players(args, pack, phase, roles, credential_namespace, username_prefix)
    server = None
    for _ in range(5):
        port = _free_port()
        config = _runtime_config(pack, phase, port, password, omitted_config_keys or set())
        candidate = subprocess.Popen([str(args.server), str(config)], stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True)
        time.sleep(0.75)
        if candidate.poll() is None:
            server = candidate
            break
        stdout, stderr = candidate.communicate()
        if "listener start rejected" not in stderr:
            raise RuntimeError(f"server failed to start: {stdout!r} {stderr!r}")
    if server is None:
        raise RuntimeError("server failed to reserve a listener after five bounded attempts")
    processes = []
    streams = []
    samples: dict[str, list[int]] = {role: [] for role in roles}
    server_samples: list[int] = []
    try:
        for role in roles:
            evidence = phase / f"{role}.ndjson"
            (phase / f"{role}-user").mkdir(exist_ok=True)
            stdout = (phase / f"{role}.stdout.log").open("wb")
            stderr = (phase / f"{role}.stderr.log").open("wb")
            streams.extend((stdout, stderr))
            command = build_client_command(
                args, pack, port, password, role, evidence, allowed_roles, scenario)
            processes.append(subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                              env=_client_environment(args)))
            if len(roles) > 1:
                time.sleep(0.5)
        deadline = time.monotonic() + timeout
        while any(process.poll() is None for process in processes):
            if time.monotonic() >= deadline:
                raise RuntimeError(f"{scenario} roles timed out: {roles!r}")
            if sample_rss:
                if server.poll() is None:
                    server_samples.append(resident_bytes(server.pid))
                for role, process in zip(roles, processes):
                    if process.poll() is None:
                        samples[role].append(resident_bytes(process.pid))
            time.sleep(0.25 if sample_rss else 0.1)
        if any(process.returncode != 0 for process in processes):
            codes = [process.returncode for process in processes]
            raise RuntimeError(f"{scenario} client failed: roles={roles!r} codes={codes!r}")
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
        for stream in streams:
            stream.close()
        server.terminate()
        try:
            server_stdout, server_stderr = server.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server_stdout, server_stderr = server.communicate(timeout=5)
        phase.joinpath("server.stdout.log").write_text(server_stdout, encoding="utf-8")
        phase.joinpath("server.stderr.log").write_text(server_stderr, encoding="utf-8")

    completions = []
    records = {}
    for role in roles:
        complete, role_records = _completion(phase / f"{role}.ndjson", role, scenario)
        completions.append(complete)
        records[role] = role_records
    queue_records = [json.loads(line) for line in server_stdout.splitlines() if line.startswith("{")]
    drained = [record for record in queue_records if record.get("event") == "phase7_queue_drain"]
    if not drained:
        raise RuntimeError(f"server queue evidence missing: roles={roles!r}")
    queue = drained[-1]
    for key in ("final_reliable_messages", "final_reliable_bytes", "final_latest_messages", "final_latest_bytes"):
        if queue.get(key) != 0:
            raise RuntimeError(f"server queue did not drain: {queue!r}")
    memory = {role: bounded_rss(values) for role, values in samples.items()} if sample_rss else {}
    if sample_rss:
        memory["server"] = bounded_rss(server_samples)
    return {"roles": completions, "records": records, "queue_drain": queue, "memory": memory}


def bake_pack(args: argparse.Namespace, root: Path, transform_world: Callable[[str], str]) -> Path:
    source = root / "authoring"
    source.mkdir()
    for name in AUTHORING_FILES:
        shutil.copy2(args.content_source / name, source / name)
    world = source / "vanilla-world.txt"
    world.write_text(transform_world(world.read_text(encoding="utf-8")), encoding="utf-8")
    output = root / "pack"
    subprocess.run([sys.executable, str(args.baker), "bake", "--openmw-config", str(args.openmw_config),
                    "--server-config", str(source / "server.cfg"), "--client-mappings", str(args.client_mappings),
                    "--derived-pack-recipe", str(args.derived_pack_recipe), "--output", str(output)], check=True)
    manifest = output.joinpath("CURRENT").read_text(encoding="ascii").strip()
    pack = output / "packs" / manifest
    subprocess.run([sys.executable, str(args.baker), "verify", str(pack)], check=True)
    evidence_pack = args.artifacts / "baked-pack"
    shutil.copytree(pack, evidence_pack, dirs_exist_ok=True)
    return evidence_pack


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--openmw", type=Path, required=True)
    parser.add_argument("--baker", type=Path, default=Path("scripts/bake_tes3mp_content.py"))
    parser.add_argument("--openmw-config", type=Path, required=True)
    parser.add_argument("--content-source", type=Path, default=Path("files/data/tes3mp"))
    parser.add_argument("--client-mappings", type=Path, default=Path("files/data/tes3mp/vanilla-client-mappings.cfg"))
    parser.add_argument("--derived-pack-recipe", type=Path, default=Path("files/data/tes3mp/vanilla-derived-pack.json"))
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--data", type=Path, action="append", required=True)
    parser.add_argument("--fallback-archive", action="append", default=[])
    parser.add_argument("--content", action="append", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)


def prepare_artifacts(artifacts: Path) -> None:
    if artifacts.exists() and any(artifacts.iterdir()):
        raise RuntimeError(f"artifact directory is not empty: {artifacts}")
    artifacts.mkdir(parents=True, exist_ok=True)


def write_summary(artifacts: Path, summary: dict, secret: str) -> None:
    summary_path = artifacts / "summary.json"
    summary_path.write_text(
        json.dumps(summary, separators=(",", ":")) + "\n", encoding="utf-8")
    for artifact in artifacts.rglob("*"):
        if artifact.is_file() and secret.encode() in artifact.read_bytes():
            raise RuntimeError(f"credential leaked to artifact: {artifact}")
    print(json.dumps({"event": summary.get("event"), "manifest": summary.get("manifest"),
                      "summary": str(summary_path)}, separators=(",", ":")))
