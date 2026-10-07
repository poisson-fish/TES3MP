"""Two rendered V79 players using ordinary AutoMove against authoritative hulls.

Uses an operator-prepared campaign in a clear area, optionally completing fresh
registrations through character choices. Never injects a player position. Reuses the
bounded impaired UDP relay. Shutdown exit codes are recorded separately.
"""
import argparse
from contextlib import ExitStack
from dataclasses import asdict
import json
import math
import os
import re
from pathlib import Path
import subprocess
import time

from native_combat_families_capture import Evidence
from run_native_navigation_capture import NavigationImpairment
from run_phase12_movement_capture import UdpImpairmentRelay, free_port


def capture(args):
    args.output.mkdir(parents=True, exist_ok=False)
    settings = dict(line.split("=", 1) for line in (args.runtime / "server.cfg").read_text().splitlines()
                    if "=" in line and not line.lstrip().startswith("#"))
    settings = {k.strip(): v.strip() for k, v in settings.items()}
    deps = Path(__file__).resolve().parents[1] / "deps/installed/x64-windows/bin"
    environment = os.environ.copy()
    environment["PATH"] = os.pathsep.join((str(args.binary), str(deps), environment.get("PATH", "")))
    environment["OSG_LIBRARY_PATH"] = str(deps / "osgPlugins-3.6.5")
    startup = args.output / "startup.txt"
    startup.write_text("StopScript CharGen\nset CharGenState to -1\nEnableInventoryMenu\nEnableMagicMenu\n"
                       "EnableMapMenu\nEnableStatsMenu\nEnablePlayerControls\n")
    paths = {r: args.output / (r + ".ndjson") for r in ("Alice", "Bob")}
    evidence = Evidence(paths)
    relay = UdpImpairmentRelay(free_port(), int(settings["port"]), "direct")
    sequences = dict.fromkeys(paths, 0)
    correction_start = dict.fromkeys(paths, 0)
    processes = {}
    finished = set()
    result = dict(success=False, checks=[], impairment="direct" if args.direct else "100ms +/-25ms, 10% loss/reorder",
                  bootstrap_direct=True, correction_budget=dict(max_units=35, mean_units=8))
    with ExitStack() as stack:
        def launch(role, command):
            stdout = stack.enter_context((args.output / (role + ".stdout.log")).open("wb"))
            stderr = stack.enter_context((args.output / (role + ".stderr.log")).open("wb"))
            processes[role] = subprocess.Popen(command, cwd=args.binary, env=environment,
                stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

        def latest(role, event):
            rows = evidence.rows(role, event)
            return rows[-1] if rows else None

        def wait(predicate, label, timeout=45):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                value = predicate()
                if value:
                    print(label, flush=True)
                    return value
                if any(p.poll() is not None for r, p in processes.items() if r not in finished):
                    raise RuntimeError("Process exited during " + label)
                time.sleep(.05)
            raise RuntimeError("Timed out: " + label)

        def command(role, action, event=None):
            sequences[role] += 1
            sequence = sequences[role]
            control = Path(str(paths[role]) + ".control")
            temporary = control.with_suffix(".pending")
            temporary.write_text(f"{sequence} {action}\n")
            for attempt in range(100):
                try:
                    temporary.replace(control)
                    break
                except PermissionError:
                    if attempt == 99:
                        raise
                    time.sleep(.01)
            return wait(lambda: next((r for r in evidence.rows(role, event or "traversal_" + action.split()[0])
                if r.get("sequence") == sequence), None), role + ": " + action)

        def position(role):
            return latest(role, "native_player_sample")["position"]

        def stationary(role):
            values = evidence.rows(role, "native_player_sample")[-8:]
            return len(values) == 8 and all(abs(v["position"][a] - values[-1]["position"][a]) <= 2
                for v in values for a in (0, 1))

        try:
            launch("server", [str(args.server_binary), str(args.runtime / "server.cfg")])
            wait(lambda: b"server started" in (args.output / "server.stdout.log").read_bytes()[-8192:], "server ready", 180)
            relay.start()
            for role in paths:
                user = args.runtime / (role + "-user")
                launch(role, [str(args.binary / "openmw.exe"), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1",
                    f"--tes3mp-port={relay.listen_port}", "--tes3mp-timeout-ms=15000",
                    f"--tes3mp-password-file={args.runtime / 'join-password.txt'}",
                    f"--tes3mp-player-credential-file={args.runtime / (role + '.credential')}",
                    f"--config={user}", "--replace=config", f"--user-data={user}",
                    f"--resources={args.binary / 'resources'}", f"--start={args.start}", "--skip-menu=1", "--new-game=0",
                    "--no-grab=1", "--no-sound=1", "--script-console=1", f"--script-run={startup}",
                    "--tes3mp-automation-role=native-traversal", f"--tes3mp-automation-output={paths[role]}"])
            if args.profiles:
                if args.profiles.stat().st_size > 16384:
                    raise ValueError("Character selection input exceeds 16 KiB")
                profiles = json.loads(args.profiles.read_text())
                if set(profiles) != set(paths):
                    raise ValueError("Exactly Alice and Bob profiles are required")
                wait(lambda: all(latest(r, "character_profile_confirmed") for r in paths), "both fresh profiles", 90)
                for role, p in profiles.items():
                    confirmed = latest(role, "character_profile_confirmed")
                    if confirmed["lifecycle"] == 3:
                        continue
                    choices = (f'name {json.dumps(role)}',
                        f'race {json.dumps(p["race"])} {int(p["male"])} {json.dumps(p["head"])} {json.dumps(p["hair"])}',
                        f'class {json.dumps(p["class"])}', f'sign {json.dumps(p["sign"])}')
                    for choice in choices[confirmed["phase"] - 1:]:
                        previous = confirmed["revision"]
                        command(role, choice, "character_choice_submitted")
                        confirmed = wait(lambda: (s if (s := latest(role, "character_profile_confirmed"))
                            and (s["revision"] > previous or s["result"] != 0) else None), role + " committed character choice")
                        if confirmed["result"] != 0:
                            raise RuntimeError("Character choice rejected")
            wait(lambda: all((s := latest(r, "native_player_sample")) and s.get("physics") for r in paths),
                 "both players received solver baselines", 90)
            for role in paths:
                command(role, "close")
                command(role, "thirdperson")
            wait(lambda: all(stationary(r) for r in paths), "initial physics settled")
            correction_start = {r: len(evidence.rows(r, "native_player_sample")) for r in paths}
            if not args.direct:
                relay.schedule = NavigationImpairment()
                wait(lambda: all(latest(r, "native_player_sample")["physics_replayed"] >= 2 for r in paths),
                     "impaired input replay active")
            if args.recover_from:
                previous = json.loads(args.recover_from.read_text())
                if not previous.get("success"):
                    raise ValueError("Recovery requires a successful prior capture")
                for role in paths:
                    saved = next(c["position"] for c in reversed(previous["checks"])
                                 if c.get("role") == role and c.get("reconnect"))
                    if math.dist(saved, position(role)) > 2 * 1024:
                        raise RuntimeError("Restart changed stopped player physics")
                    result["checks"].append(dict(role=role, restart=True, position=position(role)))
                verify_reconnect(paths, result, command, wait, stationary, position, evidence, latest)
                for role in paths:
                    finished.add(role)
                    command(role, "quit")
                    processes[role].wait(timeout=15)
                result["success"] = True
                return
            for role in paths:
                before = position(role)[:]
                command(role, "faceaway")
                if role == "Alice":
                    command(role, "turnright")
                command(role, "autowalk")
                distance = 96 if role == "Alice" else 16
                wait(lambda: math.dist(before[:2], position(role)[:2]) > distance * 1024, role + " server-owned movement")
                command(role, "autowalk")
                wait(lambda: stationary(role), role + " authoritative stop")
                result["checks"].append(dict(role=role, moved_and_stopped=True, position=position(role)))
                height = position(role)[2]
                peer = "Bob" if role == "Alice" else "Alice"
                peer_trace = len(evidence.rows(peer, "locomotion_frame"))
                command(role, "jump")
                wait(lambda: position(role)[2] > height + 4 * 1024, role + " authoritative jump")
                player_id = latest(role, "native_player_sample")["self"]
                wait(lambda: any(s.get("kind") == 1 and s.get("id") == player_id and s.get("group") == "jump"
                    for s in evidence.rows(peer, "locomotion_frame")[peer_trace:]), peer + " rendered peer jump")
                wait(lambda: abs(position(role)[2] - height) <= 2 and stationary(role), role + " authoritative landing")
                result["checks"].append(dict(role=role, jump_and_peer_animation=True))
            if not all(any(s.get("physics_replayed", 0) for s in evidence.rows(r, "native_player_sample")) for r in paths):
                raise RuntimeError("Physics input replay was not exercised")
            approach = {r: latest(r, "native_player_sample")["physics_consumed"] for r in paths}
            for role in paths:
                command(role, "facepeer")
                command(role, "autowalk")
            wait(lambda: all(latest(r, "native_player_sample")["physics_consumed"] > approach[r] + 2 for r in paths),
                 "both players submitted controls toward the shared hulls", 20)
            wait(lambda: math.dist(position("Alice")[:2], position("Bob")[:2]) / 1024 <= 65,
                 "two players reached shared collision hulls", 10)
            separation = math.dist(position("Alice")[:2], position("Bob")[:2]) / 1024
            if separation < 50:
                raise RuntimeError(f"Unexpected player hull separation: {separation}")
            for role in paths:
                command(role, "autowalk")
            wait(lambda: all(stationary(r) for r in paths), "both stopped after hull contact")
            for role in paths:
                command(role, "screenshot")
                before = position(role)[:]
                status_count = len(evidence.rows(role, "phase8_desktop_status"))
                command(role, "reconnect")
                wait(lambda: any(s.get("status") == "resumed" for s in evidence.rows(role, "phase8_desktop_status")[status_count:]),
                     role + " reconnected")
                wait(lambda: (s := latest(role, "native_player_sample")) and s.get("physics") and stationary(role),
                     role + " physics restored")
                if math.dist(before, position(role)) > 2 * 1024:
                    raise RuntimeError("Reconnect changed stopped player physics")
                result["checks"].append(dict(role=role, replay=True, hull_separation=separation,
                    reconnect=True, position=position(role)))
            for role in paths:
                finished.add(role)
                command(role, "quit")
                processes[role].wait(timeout=15)
            result["success"] = True
        except BaseException as error:
            result["error"] = str(error)
            raise
        finally:
            for process in reversed(list(processes.values())):
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
            result["exits"] = {r: p.returncode for r, p in processes.items()}
            result["relay"] = asdict(relay.stop())
            result["movement_metrics"] = {}
            for role in paths:
                logs = "\n".join((args.output / (role + suffix)).read_text(errors="replace")
                                  for suffix in (".stdout.log", ".stderr.log"))
                metrics = {name: dict(samples=int(count), min=int(low), max=int(high), total=int(total))
                    for name, count, low, high, total in re.findall(
                        r"TES3MP movement evidence: metric=(\w+) samples=(\d+) min=(\d+) max=(\d+) total=(\d+)", logs)}
                result["movement_metrics"][role] = metrics
                corrections = [s["physics_correction_quanta"] for s in evidence.rows(role, "native_player_sample")[correction_start[role]:]
                               if s.get("physics_correction_quanta") is not None]
                if corrections:
                    metrics["local_correction_distance_quanta"] = dict(samples=len(corrections),
                        min=min(corrections), max=max(corrections), total=sum(corrections))
                if result["success"] and not args.recover_from:
                    correction = metrics.get("local_correction_distance_quanta")
                    if not correction or correction["max"] > 35 * 1024 or correction["total"] / correction["samples"] > 8 * 1024:
                        result["success"] = False
                        result["error"] = "Player correction budget failed: " + role
            (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            if not result["success"] and "error" in result:
                raise RuntimeError(result["error"])
    print("PASS rendered player physics: " + str(args.output))


def verify_reconnect(paths, result, command, wait, stationary, position, evidence, latest):
    for role in paths:
        command(role, "screenshot")
        before = position(role)[:]
        count = len(evidence.rows(role, "phase8_desktop_status"))
        command(role, "reconnect")
        wait(lambda: any(s.get("status") == "resumed" for s in evidence.rows(role, "phase8_desktop_status")[count:]),
             role + " reconnected")
        wait(lambda: (s := latest(role, "native_player_sample")) and s.get("physics") and stationary(role),
             role + " physics restored")
        if math.dist(before, position(role)) > 2 * 1024:
            raise RuntimeError("Reconnect changed stopped player physics")
        result["checks"].append(dict(role=role, reconnect=True, position=position(role)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "output", "binary", "server-binary"):
        parser.add_argument("--" + name, type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument("--start", required=True)
    parser.add_argument("--direct", action="store_true")
    parser.add_argument("--profiles", type=lambda p: Path(p).resolve(), help="Complete fresh registrations through normal character choices")
    parser.add_argument("--recover-from", type=lambda p: Path(p).resolve(), help="Check restart against prior capture positions")
    capture(parser.parse_args())
