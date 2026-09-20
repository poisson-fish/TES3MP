#!/usr/bin/env python3
"""One fresh real-loadout, two-desktop native NPC navigation/disconnect capture."""

import argparse
from dataclasses import asdict
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import time

from run_phase12_movement_capture import UdpImpairmentRelay, free_port


class NavigationImpairment:
    def __init__(self):
        self.sequence = {}

    def delivery(self, direction, now):
        n = self.sequence.get(direction, 0) + 1
        self.sequence[direction] = n
        if n % 10 == 0:
            return None
        return now + 0.1 + (n % 5 - 2) * 0.0125 + (0.125 if n % 23 == 0 else 0)


def records(path):
    if not path.exists():
        return []
    # The writer flushes complete events; ignore a concurrently written tail.
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    return [json.loads(line) for line in lines if line.endswith("\n")]


def poses(path):
    return [r for r in records(path) if r.get("event") == "native_actor_pose"]


def distance(a, b):
    return math.hypot(a["x"] - b["x"], a["y"] - b["y"])


def run(args):
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Never overwrite a campaign.
    binary = args.build.resolve()
    config = args.content_config.resolve()
    settings = root / "files/settings-default.cfg"
    cell = "Seyda Neen, Arrille's Tradehouse"
    manifest = hashlib.sha256(b"native-navigation-capture-16" + config.joinpath("openmw.cfg").read_bytes()
                              + settings.read_bytes()).hexdigest()
    password = output / "join-password.txt"
    password.write_text(secrets.token_hex(24), encoding="ascii")
    port, relay_port = free_port(), free_port()
    output.joinpath("native.txt").write_text(
        f'native-inventory-16\nmanifest {manifest}\nconfig "{config.as_posix()}"\nplayers 1 2\n'
        f'actors "player" "player"\nloot 1 0\ninterior "{cell}"\ndoors auto\ncell interior:1\nareas 1\n'
        f'npc "raflod the braggart" "{settings.as_posix()}"\ndestination -550 70 385 40\n', encoding="utf-8")
    common = dict(content_manifest_id=manifest, cell_spaces="interior:1", allowed_cells="interior:1",
                  spawn_cell="interior:1", spawn_positions="-768000:-409600:394240", default_appearance_id="2",
                  movement_profile="sneak:1024;walk:4097;run:8192;jump:4096")
    server_config = common | dict(native_inventory_file="native.txt", bind_address="127.0.0.1", port=port,
                                 tick_interval_ms=33, disconnect_grace_ms=30000,
                                 join_password_file="join-password.txt", player_identity_file="players.txt")
    output.joinpath("server.cfg").write_text("".join(f"{k}={v}\n" for k, v in server_config.items()), encoding="utf-8")
    template = next(line.split() for line in root.joinpath(
        "scripts/fixtures/desktop_evidence_established_player_v5.txt").read_text().splitlines()
                    if line and not line.startswith("TES3MP_"))
    identities = []
    for index, role in enumerate(("Alice", "Bob")):
        credential = secrets.token_bytes(32)
        output.joinpath(role + ".credential").write_bytes(credential)
        tokens = template.copy()
        tokens[0:5] = [str(index + 1), str(index * 2 + 1), "2", manifest, hashlib.sha256(credential).hexdigest()]
        tokens[6:16] = ["0", "1", "0", "0", str((-750 + 80 * index) * 1024), "-409600", "394240", "0", "0", "0"]
        name = tokens[-1]
        tokens = [role.encode().hex() if token == name else token for token in tokens]
        identities.append(" ".join(tokens))
        user = output / (role + "-user")
        user.mkdir()
        shutil.copyfile(config / "openmw.cfg", user / "openmw.cfg")
        user.joinpath("settings.cfg").write_text(
            "[Video]\nresolution x = 1000\nresolution y = 700\nwindow mode = 2\nwindow border = true\n"
            "minimize on focus loss = false\nframerate limit = 30\n", encoding="utf-8")
    output.joinpath("players.txt").write_text("TES3MP_PLAYER_IDENTITIES_V5\n" + "\n".join(identities) + "\n",
                                            encoding="utf-8", newline="\n")
    startup = output / "startup.txt"
    startup.write_text("StopScript CharGen\nset CharGenState to -1\nEnableInventoryMenu\nEnableMagicMenu\n"
                       "EnableMapMenu\nEnableStatsMenu\nEnablePlayerControls\n", encoding="utf-8")
    environment = os.environ.copy()
    dependencies = root / "deps/installed/x64-windows/bin"
    environment["PATH"] = os.pathsep.join((str(binary), str(dependencies), environment.get("PATH", "")))
    environment["OSG_LIBRARY_PATH"] = str(dependencies)
    streams, processes = [], {}
    relay = UdpImpairmentRelay(relay_port, port, "direct")
    relay.schedule = NavigationImpairment()
    def start(name, command):
        stdout = output.joinpath(name + ".stdout.log").open("wb")
        stderr = output.joinpath(name + ".stderr.log").open("wb")
        streams.extend((stdout, stderr))
        process = subprocess.Popen(command, cwd=binary, env=environment, stdout=stdout, stderr=stderr,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        processes[name] = process
        return process
    evidence = {role: output / (role + ".ndjson") for role in ("Alice", "Bob")}
    survivor = "Bob" if args.leave == "Alice" else "Alice"
    try:
        server = start("server", [str(binary / "tes3mp_server.exe"), str(output / "server.cfg")])
        time.sleep(2)
        if server.poll() is not None:
            raise RuntimeError("server startup failed; inspect server.stderr.log")
        relay.start()
        for role in evidence:
            user = output / (role + "-user")
            command = [str(binary / "openmw.exe"), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1",
                       f"--tes3mp-port={relay_port}", "--tes3mp-timeout-ms=15000",
                       f"--tes3mp-password-file={password}", f"--tes3mp-player-credential-file={output / (role + '.credential')}",
                       f"--config={user}", "--replace=config", f"--user-data={user}", f"--resources={binary / 'resources'}",
                       f"--start={cell}", "--skip-menu=1", "--new-game=0", "--no-grab=1", "--no-sound=1",
                       "--script-console=1", f"--script-run={startup}", "--tes3mp-automation-role=native-traversal",
                       f"--tes3mp-automation-output={evidence[role]}", f"--tes3mp-content-manifest-id={manifest}",
                       "--tes3mp-content-cell-spaces=interior:1", "--tes3mp-content-allowed-cells=interior:1",
                       f"--tes3mp-content-cell-space-map=1={cell}", "--tes3mp-content-appearance-id=2",
                       "--tes3mp-content-appearance-record=player"]
            start(role, command)
        deadline = time.monotonic() + 75
        ready_at, left_at, disconnect_pose = None, None, None
        while time.monotonic() < deadline:
            if server.poll() is not None or processes[survivor].poll() is not None:
                raise RuntimeError("server or survivor exited during navigation")
            if left_at is None and processes[args.leave].poll() is not None:
                raise RuntimeError("departing client exited before the disconnect request")
            samples = {role: poses(path) for role, path in evidence.items()}
            if ready_at is None and all(len(value) >= 30 for value in samples.values()):
                ready_at = time.monotonic()
                print("Both desktop clients render the moving native actor", flush=True)
            if ready_at and left_at is None and time.monotonic() >= ready_at + 2:
                disconnect_pose = samples[survivor][-1]
                evidence[args.leave].with_suffix(".ndjson.control").write_text("1 disconnect\n", encoding="ascii")
                left_at = time.monotonic()
                print(f"Requested transport disconnect for {args.leave}", flush=True)
            if left_at and time.monotonic() > left_at + 4:
                tail = samples[survivor][-30:]
                if len(tail) == 30 and all(distance(p, tail[-1]) < 0.01 for p in tail) and abs(tail[-1]["y"] - 70) < 16:
                    break
            time.sleep(0.2)
        else:
            raise RuntimeError("bounded desktop navigation/disconnect timed out")
        all_records = {role: records(path) for role, path in evidence.items()}
        if not any(r.get("event") == "phase8_desktop_status" and r.get("status") == "reconnecting"
                   for r in all_records[args.leave]):
            raise RuntimeError("departing client did not close its transport")
        metrics = {}
        for role, data in all_records.items():
            frames = [r for r in data if r.get("event") == "native_actor_pose"]
            motion = {r["tick"]: r for r in data if r.get("event") == "native_actor_sample"}
            steps = [distance(a, b) for a, b in zip(frames, frames[1:])]
            metrics[role] = dict(rendered_frames=len(frames), received_samples=len(motion), max_frame_step=max(steps),
                                 first=frames[0], last=frames[-1])
            if len(frames) < 60 or max(steps) >= 16 or distance(frames[0], frames[-1]) <= 25:
                raise RuntimeError(f"{role}: insufficient or discontinuous rendered movement")
        motions = [{r["tick"]: r for r in all_records[role] if r.get("event") == "native_actor_sample"} for role in evidence]
        common_ticks = motions[0].keys() & motions[1].keys()
        if any(distance(motions[0][tick], motions[1][tick]) != 0
                                       or motions[0][tick]["z"] != motions[1][tick]["z"] for tick in common_ticks):
            raise RuntimeError("clients did not receive matching authoritative actor samples")
        # Latest-wins delivery may select disjoint ticks for the two peers. Compare
        # the overlapping trajectories at the same server ticks as well as exact
        # equality wherever both received the same tick.
        interpolated_errors = []
        other = sorted(motions[1].values(), key=lambda sample: sample["tick"])
        for tick, sample in motions[0].items():
            for a, b in zip(other, other[1:]):
                if a["tick"] <= tick <= b["tick"] and b["tick"] - a["tick"] <= 10:
                    ratio = (tick - a["tick"]) / (b["tick"] - a["tick"])
                    interpolated_errors.append(distance(sample, {axis: a[axis] + ratio * (b[axis] - a[axis])
                                                                 for axis in ("x", "y")}))
                    break
        if len(interpolated_errors) < 15 or max(interpolated_errors) >= 4:
            raise RuntimeError("overlapping authoritative trajectories diverged")
        final = metrics[survivor]["last"]
        post_disconnect = distance(disconnect_pose, final)
        if post_disconnect < 50:
            raise RuntimeError("survivor did not observe enough movement after disconnect")
        latest = next(reversed(motions[0 if survivor == "Alice" else 1].values()))
        if distance(final, latest) >= 0.1:
            raise RuntimeError("survivor did not converge to the committed actor position")
        disconnected = re.findall(r"native actor disconnect committed: tick=(\d+) active_sessions=1",
                                   output.joinpath("server.stderr.log").read_text(encoding="utf-8"))
        if len(disconnected) != 1:
            raise RuntimeError("server did not durably disconnect exactly one participant")
        disconnect_tick = int(disconnected[0])
        remaining = sorted((r for r in motions[0 if survivor == "Alice" else 1].values()
                            if r["tick"] > disconnect_tick), key=lambda r: r["tick"])
        if not remaining or distance(remaining[0], final) < 50:
            raise RuntimeError("native actor did not continue after the durable disconnect")
        evidence[survivor].with_suffix(".ndjson.control").write_text("1 quit\n", encoding="ascii")
        processes[survivor].wait(timeout=15)
        completed = [r for r in records(evidence[survivor]) if r.get("event") == "phase8_desktop_complete"]
        if processes[survivor].returncode != 0 or len(completed) != 1 or not all(
                completed[0][key] for key in ("success", "saw_peer")):
            raise RuntimeError("survivor did not finish cleanly")
        stats = relay.stop()
        report = dict(success=True, leaving=args.leave, common_ticks=len(common_ticks),
                      overlapping_samples=len(interpolated_errors), maximum_trajectory_difference=max(interpolated_errors),
                      post_disconnect_distance=post_disconnect, relay=asdict(stats), clients=metrics,
                      durable_disconnect_tick=disconnect_tick, movement_after_durable_disconnect=distance(remaining[0], final),
                      profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay",
                      manifest=manifest, content_config_sha256=hashlib.sha256(config.joinpath("openmw.cfg").read_bytes()).hexdigest())
        output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2), flush=True)
    finally:
        relay.stop()
        for process in reversed(list(processes.values())):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
        for stream in streams:
            stream.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--content-config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--leave", choices=("Alice", "Bob"), required=True)
    run(parser.parse_args())
