#!/usr/bin/env python3
"""Run bounded content-backed Phase 12 desktop or PC-VR movement captures."""

import argparse
import heapq
import json
import os
import re
import selectors
import socket
import subprocess
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path

from run_phase7_join_demo import TEST_CONTENT_MANIFEST
from run_phase8_desktop_demo import read_completion


PROFILES = ("direct", "jitter", "loss", "stall")
METRIC_RE = re.compile(
    r"TES3MP movement evidence: metric=([a-z_]+) samples=(\d+) min=(\d+) max=(\d+) total=(\d+)")
REQUIRED_METRICS = {
    "command_ack_nanoseconds", "stop_ack_nanoseconds", "local_correction_distance_quanta",
    "remote_snapshot_age_nanoseconds", "remote_buffer_depth", "remote_hard_snaps",
}


@dataclass
class RelayStats:
    received: int = 0
    delivered: int = 0
    dropped: int = 0
    delayed: int = 0
    socket_resets: int = 0


class ImpairmentSchedule:
    def __init__(self, profile: str):
        if profile not in PROFILES:
            raise ValueError(f"unknown movement capture profile: {profile}")
        self.profile = profile
        self.sequence = {"client_to_server": 0, "server_to_client": 0}
        self.stall_until = {"client_to_server": 0.0, "server_to_client": 0.0}

    def delivery(self, direction: str, now: float) -> float | None:
        self.sequence[direction] += 1
        sequence = self.sequence[direction]
        if self.profile == "loss" and sequence % 20 == 0:
            return None
        if self.profile == "jitter":
            return now + (0.045 if sequence % 2 else 0.005)
        if self.profile == "stall":
            if sequence == 40:
                self.stall_until[direction] = now + 0.250
            return max(now + 0.005, self.stall_until[direction])
        return now


class UdpImpairmentRelay:
    """Small bounded one-server UDP relay with one backend socket per client."""

    MAXIMUM_CLIENTS = 8
    MAXIMUM_PENDING = 4096

    def __init__(self, listen_port: int, server_port: int, profile: str):
        self.listen_port = listen_port
        self.server = ("127.0.0.1", server_port)
        self.schedule = ImpairmentSchedule(profile)
        self.stats = RelayStats()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._error: BaseException | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="phase12-udp-relay", daemon=True)
        self._thread.start()

    def stop(self) -> RelayStats:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=5)
            if self._thread.is_alive():
                raise RuntimeError("UDP impairment relay did not stop")
        if self._error:
            raise RuntimeError(f"UDP impairment relay failed: {self._error}")
        return self.stats

    def _run(self) -> None:
        selector = selectors.DefaultSelector()
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.bind(("127.0.0.1", self.listen_port))
        listener.setblocking(False)
        selector.register(listener, selectors.EVENT_READ, None)
        clients: dict[tuple[str, int], socket.socket] = {}
        pending: list[tuple[float, int, socket.socket, bytes, tuple[str, int] | None]] = []
        serial = 0
        try:
            while not self._stop.is_set():
                now = time.monotonic()
                while pending and pending[0][0] <= now:
                    _, _, target, payload, address = heapq.heappop(pending)
                    target.send(payload) if address is None else target.sendto(payload, address)
                    self.stats.delivered += 1
                timeout = min(0.02, max(0.0, pending[0][0] - now)) if pending else 0.02
                for key, _ in selector.select(timeout):
                    try:
                        payload, address = key.fileobj.recvfrom(65535)
                    except ConnectionResetError:
                        self.stats.socket_resets += 1
                        continue
                    self.stats.received += 1
                    if key.data is None:
                        backend = clients.get(address)
                        if backend is None:
                            if len(clients) >= self.MAXIMUM_CLIENTS:
                                self.stats.dropped += 1
                                continue
                            backend = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            backend.connect(self.server)
                            backend.setblocking(False)
                            clients[address] = backend
                            selector.register(backend, selectors.EVENT_READ, address)
                        target, destination, direction = backend, None, "client_to_server"
                    else:
                        target, destination, direction = listener, key.data, "server_to_client"
                    delivery = self.schedule.delivery(direction, time.monotonic())
                    if delivery is None or len(pending) >= self.MAXIMUM_PENDING:
                        self.stats.dropped += 1
                    else:
                        serial += 1
                        heapq.heappush(pending, (delivery, serial, target, payload, destination))
                        if delivery > time.monotonic() + 0.001:
                            self.stats.delayed += 1
        except BaseException as error:
            self._error = error
        finally:
            selector.close()
            listener.close()
            for backend in clients.values():
                backend.close()


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def client_command(args: argparse.Namespace, executable: Path, port: int, password: Path,
                   label: str, automation_role: str | None) -> list[str]:
    user = args.artifacts / args.profile / f"{label}-user"
    command = [
        str(executable), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1", f"--tes3mp-port={port}",
        "--tes3mp-timeout-ms=5000", f"--tes3mp-password-file={password}",
        f"--tes3mp-player-credential-file={user / 'player-credential'}",
        f"--tes3mp-content-manifest-id={TEST_CONTENT_MANIFEST}",
        "--tes3mp-content-cell-spaces=interior:7;exterior:8",
        "--tes3mp-content-allowed-cells=interior:7;exterior:8:0:0", "--tes3mp-content-appearance-id=1",
        "--tes3mp-content-movement-profile=sneak:1024;walk:4097;run:8192;jump:4096",
        f"--tes3mp-content-cell-space-map=7={args.interior}",
        f"--tes3mp-content-cell-space-map=8={args.worldspace}",
        f"--tes3mp-content-appearance-record={args.avatar}", f"--resources={args.resources}",
        f"--user-data={user}", f"--start={args.interior}", "--skip-menu=1", "--new-game=1",
        "--no-sound=1", "--no-grab=1",
    ]
    if automation_role:
        command += [f"--tes3mp-automation-role={automation_role}",
                    f"--tes3mp-automation-output={args.artifacts / args.profile / (label + '.ndjson')}"]
    command += [f"--data={value}" for value in args.data]
    command += [f"--fallback-archive={value}" for value in args.fallback_archive]
    command += [f"--content={value}" for value in args.content]
    return command


def parse_metrics(path: Path) -> dict[str, dict[str, int]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    if "TES3MP movement evidence dropped observations" in text:
        raise RuntimeError(f"movement metric sink overflowed: {path}")
    metrics = {
        match.group(1): {"samples": int(match.group(2)), "min": int(match.group(3)),
                         "max": int(match.group(4)), "total": int(match.group(5))}
        for match in METRIC_RE.finditer(text)
    }
    missing = REQUIRED_METRICS - metrics.keys()
    if missing:
        raise RuntimeError(f"movement evidence missing from {path}: {sorted(missing)}")
    return metrics


def write_server_config(root: Path, port: int, password: Path) -> Path:
    collision = root / "collision-content"
    collision.write_text(
        f"TES3MP_COLLISION_V1\nmanifest {TEST_CONTENT_MANIFEST}\ncell interior 7\ncell exterior 8 0 0\n",
        encoding="utf-8")
    actors = root / "actor-content"
    actors.write_text(
        f"TES3MP_ACTORS_V1\nmanifest {TEST_CONTENT_MANIFEST}\n", encoding="utf-8")
    config = root / "server.cfg"
    config.write_text(
        f"bind_address=127.0.0.1\nport={port}\ntick_interval_ms=16\ndisconnect_grace_ms=3000\n"
        f"join_password_file={password.as_posix()}\ncontent_manifest_id={TEST_CONTENT_MANIFEST}\n"
        "cell_spaces=interior:7;exterior:8\nallowed_cells=interior:7;exterior:8:0:0\n"
        "spawn_cell=interior:7\ndefault_appearance_id=1\n"
        "movement_profile=sneak:1024;walk:4097;run:8192;jump:4096\n"
        f"collision_content_file={collision.as_posix()}\n"
        f"actor_content_file={actors.as_posix()}\n"
        f"player_identity_file={(root / 'player-identities').as_posix()}\n", encoding="utf-8")
    return config


def run_profile(args: argparse.Namespace) -> dict:
    profile_dir = args.artifacts / args.profile
    profile_dir.mkdir(parents=True, exist_ok=True)
    server_port, relay_port = free_port(), free_port()
    while relay_port == server_port:
        relay_port = free_port()
    with tempfile.TemporaryDirectory(prefix=f"tes3mp-phase12-{args.profile}-") as temporary:
        root = Path(temporary)
        password = root / "join-password"
        password.write_text("phase12-capture-secret\n", encoding="utf-8")
        config = write_server_config(root, server_port, password)
        server_out = (profile_dir / "server.stdout.log").open("wb")
        server_err = (profile_dir / "server.stderr.log").open("wb")
        environment = os.environ.copy()
        if args.runtime_dir:
            environment["PATH"] = os.pathsep.join(map(str, args.runtime_dir)) + os.pathsep + environment.get("PATH", "")
        server = subprocess.Popen(
            [str(args.server), str(config)], stdout=server_out, stderr=server_err, env=environment)
        relay = UdpImpairmentRelay(relay_port, server_port, args.profile)
        clients: list[tuple[str, subprocess.Popen[bytes], object, object]] = []
        try:
            time.sleep(0.5)
            relay.start()
            specifications = (("capture-one", args.openmw, "capture-one"),
                              ("capture-two", args.openmw, "capture-two"))
            if args.platform == "pc-vr":
                specifications = (("desktop", args.openmw, None), ("pc-vr", args.openmw_vr, None))
            for label, executable, role in specifications:
                user = profile_dir / f"{label}-user"
                user.mkdir(exist_ok=True)
                stdout = (profile_dir / f"{label}.stdout.log").open("wb")
                stderr = (profile_dir / f"{label}.stderr.log").open("wb")
                process = subprocess.Popen(
                    client_command(args, executable, relay_port, password, label, role),
                    stdout=stdout, stderr=stderr, env=environment)
                clients.append((label, process, stdout, stderr))
            deadline = time.monotonic() + args.timeout
            while any(process.poll() is None for _, process, _, _ in clients):
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"movement capture timed out: {args.platform}/{args.profile}")
                time.sleep(0.1)
            codes = {label: process.returncode for label, process, _, _ in clients}
            if any(code != 0 for code in codes.values()):
                raise RuntimeError(f"movement capture client failed: {codes}")
        finally:
            for _, process, _, _ in clients:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            for _, _, stdout, stderr in clients:
                stdout.close()
                stderr.close()
            relay_error = None
            try:
                stats = relay.stop()
            except RuntimeError as error:
                relay_error = error
                stats = relay.stats
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
            server_out.close()
            server_err.close()
            if relay_error:
                raise relay_error
        if server.returncode not in (0, 1):
            raise RuntimeError(f"movement capture server failed: {server.returncode}")
        if args.platform == "desktop":
            for label, _, _, _ in clients:
                read_completion(profile_dir / f"{label}.ndjson", label)
        result = {
            "platform": args.platform, "profile": args.profile,
            "relay": vars(stats),
            "clients": {label: parse_metrics(profile_dir / f"{label}.stdout.log")
                        for label, _, _, _ in clients},
        }
        (profile_dir / "summary.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=("desktop", "pc-vr"), required=True)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--openmw", type=Path, required=True)
    parser.add_argument("--openmw-vr", type=Path)
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--data", type=Path, action="append", required=True)
    parser.add_argument("--fallback-archive", action="append", default=[])
    parser.add_argument("--content", action="append", required=True)
    parser.add_argument("--interior", required=True)
    parser.add_argument("--worldspace", required=True)
    parser.add_argument("--avatar", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()
    if args.platform == "pc-vr" and args.openmw_vr is None:
        parser.error("--openmw-vr is required for PC-VR capture")
    return args


def main() -> int:
    args = parse_args()
    result = run_profile(args)
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
