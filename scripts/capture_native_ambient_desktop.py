"""Two rendered V80 clients: committed ambient output, reentry and durable restart.

Requires a fresh bootstrap campaign and a context-free fixture that periodically
emits 'The shared bell rings.'. Uses ordinary character choices,
disconnect/resume and stock message UI; never injects script or gameplay state.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from native_combat_families_capture import Evidence
from run_native_navigation_capture import NavigationImpairment
from run_phase12_movement_capture import UdpImpairmentRelay, free_port


def validate_output(rows):
    previous = 0
    for row in rows:
        if row["invocation"] <= previous or row["messages"] != ['The shared bell rings.']:
            raise RuntimeError("Duplicate, reordered or divergent script output")
        previous = row["invocation"]


def capture(args):
    args.output.mkdir(parents=True, exist_ok=False)
    settings = dict(line.split("=", 1) for line in (args.runtime / "server.cfg").read_text().splitlines()
                    if "=" in line and not line.lstrip().startswith("#"))
    settings = {k.strip(): v.strip() for k, v in settings.items()}
    environment = os.environ.copy()
    deps = Path(__file__).resolve().parents[1] / "deps/installed/x64-windows/bin"
    environment["PATH"] = os.pathsep.join((str(args.binary), str(deps), environment.get("PATH", "")))
    environment["OSG_LIBRARY_PATH"] = str(deps / "osgPlugins-3.6.5")
    startup = args.output / "startup.txt"
    startup.write_text("StopScript CharGen\nset CharGenState to -1\nEnableInventoryMenu\nEnableMagicMenu\n"
                       "EnableMapMenu\nEnableStatsMenu\nEnablePlayerControls\n")
    paths = {r: args.output / (r + ".ndjson") for r in ("Alice", "Bob")}
    evidence = Evidence(paths)
    relay = UdpImpairmentRelay(free_port(), int(settings["port"]), "direct")
    sequences = dict.fromkeys(paths, 0)
    processes, finished = {}, set()
    result = dict(success=False, checks=[], synthetic=True, impairment="100ms +/-25ms, 10% loss/reorder")
    with ExitStack() as stack:
        def launch(role, command):
            processes[role] = subprocess.Popen(command, cwd=args.binary, env=environment,
                stdout=stack.enter_context((args.output / (role + ".stdout.log")).open("wb")),
                stderr=stack.enter_context((args.output / (role + ".stderr.log")).open("wb")),
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

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
            pending = control.with_suffix(".pending")
            pending.write_text(f"{sequence} {action}\n")
            for attempt in range(100):
                try:
                    pending.replace(control)
                    break
                except PermissionError:
                    if attempt == 99:
                        raise
                    time.sleep(.01)
            return wait(lambda: next((row for row in evidence.rows(role, event or "traversal_" + action.split()[0])
                if row.get("sequence") == sequence), None), role + ": " + action)

        def paired(after):
            left = {r["invocation"]: r for r in evidence.rows("Alice", "ambient_presented") if r["invocation"] > after}
            right = {r["invocation"]: r for r in evidence.rows("Bob", "ambient_presented") if r["invocation"] > after}
            shared = sorted(left.keys() & right.keys())
            if len(shared) < 2:
                return None
            for invocation in shared:
                if any(left[invocation][k] != right[invocation][k] for k in ("script", "tick", "revision", "messages")):
                    raise RuntimeError("Observers differ for one shared invocation")
            return shared

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
            wait(lambda: all(latest(r, "character_profile_confirmed") for r in paths), "both character profiles", 90)
            if args.profiles:
                if args.profiles.stat().st_size > 16384:
                    raise ValueError("Character input exceeds 16 KiB")
                profiles = json.loads(args.profiles.read_text())
                if set(profiles) != set(paths):
                    raise ValueError("Exactly two profiles are required")
                for role, p in profiles.items():
                    confirmed = latest(role, "character_profile_confirmed")
                    if confirmed["lifecycle"] == 3:
                        continue
                    choices = (f'name {json.dumps(role)}',
                        f'race {json.dumps(p["race"])} {int(p["male"])} {json.dumps(p["head"])} {json.dumps(p["hair"])}',
                        f'class {json.dumps(p["class"])}', f'sign {json.dumps(p["sign"])}')
                    for choice in choices[confirmed["phase"] - 1:]:
                        revision = confirmed["revision"]
                        command(role, choice, "character_choice_submitted")
                        confirmed = wait(lambda: (s if (s := latest(role, "character_profile_confirmed"))
                            and (s["revision"] > revision or s["result"] != 0) else None), role + " committed choice")
                        if confirmed["result"] != 0:
                            raise RuntimeError("Character choice rejected")
            wait(lambda: all((s := latest(r, "native_player_sample")) and s.get("physics") for r in paths), "both rendered players", 90)
            for role in paths:
                command(role, "close")
            relay.schedule = NavigationImpairment()
            floor = 0
            if args.recover_from:
                previous = json.loads(args.recover_from.read_text())
                if not previous.get("success"):
                    raise ValueError("Restart requires a successful prior capture")
                floor = previous["last_invocation"]
                for role in paths:
                    first = wait(lambda: latest(role, "ambient_presented"), role + " recovered ambient")
                    if evidence.rows(role, "ambient_presented")[0]["invocation"] <= floor:
                        raise RuntimeError("Restart reset or replayed script progress")
                    result["checks"].append(dict(role=role, restart=True, first_invocation=first["invocation"]))
            shared = wait(lambda: paired(floor), "two clients present identical committed occurrences")
            result["checks"].append(dict(paired_invocations=shared))
            for role in paths:
                peer = "Bob" if role == "Alice" else "Alice"
                before = latest(role, "ambient_presented")
                count = len(evidence.rows(role, "phase8_desktop_status"))
                command(role, "disconnectbrief")
                offline = wait(lambda: (r if (r := latest(peer, "ambient_presented"))
                    and r["invocation"] >= before["invocation"] + 120 else None), peer + " receives while peer is absent")
                wait(lambda: any(r.get("status") == "resumed" for r in evidence.rows(role, "phase8_desktop_status")[count:]), role + " reentered")
                resumed = wait(lambda: next((r for r in evidence.rows(role, "ambient_presented")
                    if r["generation"] > before["generation"]), None), role + " new ambient after reentry")
                if resumed["invocation"] <= offline["invocation"]:
                    raise RuntimeError("Reentry replayed output emitted while absent")
                result["checks"].append(dict(role=role, reentry=True, before=before["invocation"],
                    offline=offline["invocation"], after=resumed["invocation"]))
                shared = wait(lambda: paired(resumed["invocation"]), "shared occurrences after reentry")
                result["checks"].append(dict(paired_invocations=shared))
                command(role, "screenshot")
            if args.review_seconds:
                print("ambient UI review ready", flush=True)
                time.sleep(args.review_seconds)
            for role in paths:
                validate_output(evidence.rows(role, "ambient_presented"))
            result["last_invocation"] = max(latest(r, "ambient_presented")["invocation"] for r in paths)
            for role in paths:
                finished.add(role)
                command(role, "quit")
                processes[role].wait(timeout=15)
                if processes[role].returncode:
                    raise RuntimeError("Client shutdown failed: " + role)
            result["success"] = True
        except Exception as error:
            result["error"] = str(error)
        finally:
            relay.stop()
            for role, process in processes.items():
                if process.poll() is None:
                    if role == "server":
                        result["server_terminated_for_restart"] = True
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            result["exit_codes"] = {r: p.returncode for r, p in processes.items()}
            save = args.runtime / (settings["player_identity_file"] + ".world-v2")
            if save.exists():
                result["durable_sha256"] = hashlib.sha256(save.read_bytes()).hexdigest()
            (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    if not result["success"]:
        raise RuntimeError(result["error"])
    print("PASS rendered ambient delivery: " + str(args.output))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "output", "binary", "server-binary"):
        parser.add_argument("--" + name, type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument("--start", required=True)
    parser.add_argument("--profiles", type=lambda p: Path(p).resolve())
    parser.add_argument("--recover-from", type=lambda p: Path(p).resolve())
    parser.add_argument("--review-seconds", type=int, choices=range(0, 61), default=0)
    capture(parser.parse_args())
