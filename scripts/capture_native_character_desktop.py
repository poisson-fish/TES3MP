#!/usr/bin/env python3
"""Two rendered clients: native character completion, inventory and body recovery.

Requires an operator-prepared V78 runtime and matching client-content.cfg. Never
resets its canonical save or grants items. Choices use the normal coordinator
submission seam; this does not claim coverage of chargen dialog clicks.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from native_combat_families_capture import Evidence, record_id


def capture(args):
    args.output.mkdir(parents=True, exist_ok=False)
    if args.profiles.stat().st_size > 65536:
        raise ValueError("Character profiles exceed 64 KiB")
    profiles = json.loads(args.profiles.read_text())
    if set(profiles) != {"Alice", "Bob"}:
        raise ValueError("Exactly Alice and Bob profiles are required")
    settings = dict(line.split("=", 1) for line in (args.runtime / "server.cfg").read_text().splitlines()
                    if "=" in line and not line.lstrip().startswith("#"))
    settings = {k.strip(): v.strip() for k, v in settings.items()}
    save = Path(settings["player_identity_file"] + ".world-v2")
    if not save.is_absolute():
        save = args.runtime / save
    before = hashlib.sha256(save.read_bytes()).hexdigest() if save.exists() else None
    if args.phase == "restart" and before is None:
        raise RuntimeError("Restart requires the existing committed save")
    environment = os.environ.copy()
    deps = Path(__file__).resolve().parents[1] / "deps/installed/x64-windows/bin"
    environment["PATH"] = os.pathsep.join((str(args.binary), str(deps), environment.get("PATH", "")))
    environment["OSG_LIBRARY_PATH"] = str(deps / "osgPlugins-3.6.5")
    startup = args.output / "startup.txt"
    startup.write_text("StopScript CharGen\nset CharGenState to -1\nEnableInventoryMenu\nEnableMagicMenu\n"
                       "EnableMapMenu\nEnableStatsMenu\nEnablePlayerControls\n")
    paths = {r: args.output / (r + ".ndjson") for r in profiles}
    evidence = Evidence(paths)
    sequences = dict.fromkeys(profiles, 0)
    processes = {}
    finished = set()
    checks = []
    with ExitStack() as stack:
        def launch(role, command):
            stdout = stack.enter_context((args.output / (role + ".stdout.log")).open("wb"))
            stderr = stack.enter_context((args.output / (role + ".stderr.log")).open("wb"))
            processes[role] = subprocess.Popen(command, cwd=args.binary, env=environment,
                stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

        def rows(role, event):
            return evidence.rows(role, event)

        def server_ready():
            with (args.output / "server.stdout.log").open("rb") as stream:
                stream.seek(max(0, stream.seek(0, 2) - 8192))
                return b"server started" in stream.read()

        def latest(role, event):
            found = rows(role, event)
            return found[-1] if found else None

        def wait(predicate, description, timeout=45):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                found = predicate()
                if found:
                    return found
                if any(p.poll() is not None for role, p in processes.items() if role not in finished) or any(
                    any(r.get("success") is False for r in rows(role, "phase8_desktop_complete")) for role in profiles):
                    raise RuntimeError("Process failed while waiting for " + description)
                time.sleep(.1)
            raise RuntimeError("Timed out: " + description)

        def command(role, action, event=None):
            sequences[role] += 1
            sequence = sequences[role]
            control = Path(str(paths[role]) + ".control")
            pending = control.with_suffix(".pending")
            pending.write_text(f"{sequence} {action}\n")
            # Windows disallows replacement during the client's short read.
            for attempt in range(100):
                try:
                    pending.replace(control)
                    break
                except PermissionError:
                    if attempt == 99:
                        raise
                    time.sleep(.01)
            event = event or "traversal_" + action.split()[0]
            return wait(lambda: next((r for r in rows(role, event) if r.get("sequence") == sequence), None), action)

        def observe(role):
            result = command(role, "observe")
            p = profiles[role]
            if not result["inventory_matches"] or result["local_race"].lower() != p["race"].lower() \
                or result["local_head"].lower() != p["head"].lower() or result["local_hair"].lower() != p["hair"].lower() \
                or result["local_male"] != p["male"]:
                raise RuntimeError(role + ": installed profile/inventory mismatch")
            peer = "Bob" if role == "Alice" else "Alice"
            expected = profiles[peer]
            bodies = latest(role, "player_appearance_sample")
            selected = next((b for b in bodies["bodies"] if b["player"] == (2 if peer == "Bob" else 1)), None) if bodies else None
            if selected != dict(player=2 if peer == "Bob" else 1, race=record_id(expected["race"]),
                                head=record_id(expected["head"]), hair=record_id(expected["hair"]), sex=int(expected["male"])):
                raise RuntimeError(role + ": rendered peer body differs from selected profile")
            return result

        try:
            launch("server", [str(args.server_binary or args.binary / "tes3mp_server.exe"), str(args.runtime / "server.cfg")])
            wait(server_ready,
                 "server readiness after native scene/navigation construction", timeout=180)
            for role in profiles:
                user = args.runtime / (role + "-user")
                launch(role, [str(args.binary / "openmw.exe"), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1",
                    f"--tes3mp-port={settings['port']}", "--tes3mp-timeout-ms=15000",
                    f"--tes3mp-password-file={args.runtime / 'join-password.txt'}",
                    f"--tes3mp-player-credential-file={args.runtime / (role + '.credential')}",
                    f"--config={user}", "--replace=config", f"--user-data={user}",
                    f"--resources={args.binary / 'resources'}", f"--start={args.cell}", "--skip-menu=1", "--new-game=0",
                    "--no-grab=1", "--no-sound=1", "--script-console=1", f"--script-run={startup}",
                    "--tes3mp-automation-role=native-traversal", f"--tes3mp-automation-output={paths[role]}"])
            wait(lambda: all(latest(r, "character_profile_confirmed") for r in profiles), "both character profiles")
            if args.phase == "complete":
                for role, p in profiles.items():
                    confirmed = latest(role, "character_profile_confirmed")
                    if confirmed["lifecycle"] == 3:
                        continue # Resume a partially completed acceptance run.
                    if confirmed["lifecycle"] not in (1, 2):
                        raise RuntimeError("Completion capture requires unfinished profiles")
                    choices = (f'name {json.dumps(role)}',
                                   f'race {json.dumps(p["race"])} {int(p["male"])} {json.dumps(p["head"])} {json.dumps(p["hair"])}',
                                   f'class {json.dumps(p["class"])}', f'sign {json.dumps(p["sign"])}')
                    for choice in choices[confirmed["phase"] - 1:]:
                        old = latest(role, "character_profile_confirmed")["revision"]
                        command(role, choice, "character_choice_submitted")
                        confirmed = wait(lambda: (v if (v := latest(role, "character_profile_confirmed"))
                            and (v["revision"] > old or v["result"] != 0) else None), "committed character choice")
                        if confirmed["result"] != 0:
                            raise RuntimeError(f"{role}: character choice rejected: {confirmed['result']}")
            wait(lambda: all((v := latest(r, "character_profile_confirmed")) and v["lifecycle"] == 3
                            for r in profiles), "established profiles")
            wait(lambda: all(latest(r, "native_player_sample") and latest(r, "player_appearance_sample")
                            and (args.steps or len(latest(r, "player_appearance_sample")["bodies"]) == 1)
                            for r in profiles), "rendered scenes")
            time.sleep(1)
            for role in profiles:
                command(role, "close")
                if args.phase == "complete" and latest(role, "traversal_inventory")["player"]:
                    raise RuntimeError("Fresh completion granted starter items")
            if args.steps:
                if args.steps.stat().st_size > 65536:
                    raise ValueError("Acceptance steps exceed 64 KiB")
                steps = json.loads(args.steps.read_text())
                if not isinstance(steps, list) or len(steps) > 128:
                    raise ValueError("Acceptance requires at most 128 steps")
                for step in steps:
                    role = step["role"]
                    observer = step.get("motion", {}).get("observer", role)
                    motion_start = len(rows(observer, "locomotion_frame"))
                    command(role, step["action"])
                    time.sleep(min(2, max(0, step.get("settle", .25))))
                    if "motion" in step:
                        motion = step["motion"]
                        def progressed():
                            values = [v for v in rows(observer, "locomotion_frame")[motion_start:]
                                      if v["kind"] == 1 and v["id"] == motion["player"]
                                      and v["group"] in ("walkforward", "runforward")]
                            return values if len({v["clip_time"] for v in values}) >= 3 else None
                        wait(progressed, "forward locomotion clip progression")
                        checks.append(dict(role=role, observer=observer, forward_clip_progressed=True))
                    if "expect" in step:
                        def matches_step():
                            observed = command(role, "observe")
                            return observed if all(observed.get(k) == v for k, v in step["expect"].items()) else None
                        value = wait(matches_step, "acceptance step: " + step["action"])
                        checks.append(dict(role=role, action=step["action"], expected=step["expect"]))
                    if step.get("profile"):
                        observe(role)
            if args.interactive_seconds:
                deadline = time.monotonic() + args.interactive_seconds
                print("Rendered clients ready for bounded keyboard inspection", flush=True)
                while time.monotonic() < deadline:
                    if any(p.poll() is not None for p in processes.values()):
                        raise RuntimeError("Process exited during bounded inspection")
                    time.sleep(.1)
            for role in profiles:
                command(role, "close")
                observed = observe(role)
                command(role, "facepeer")
                command(role, "screenshot")
                initial = latest(role, "traversal_inventory")["player"]
                previous_resumes = observed["resumes"]
                status_count = len(rows(role, "phase8_desktop_status"))
                command(role, "reconnect")
                wait(lambda: any(v.get("status") == "resumed" for v in rows(role, "phase8_desktop_status")[status_count:]), "resumed client")
                wait(lambda: len((latest(role, "player_appearance_sample") or {}).get("bodies", [])) == 1, "restored peer body")
                time.sleep(1)
                restored = observe(role)
                if latest(role, "traversal_inventory")["player"] != initial or restored["resumes"] <= previous_resumes:
                    raise RuntimeError("Reconnect changed committed inventory or failed to resume")
                checks.append(dict(role=role, empty_inventory=not initial, installed_body=True, rendered_peer=True,
                                   resumes_before=previous_resumes, resumes_after=restored["resumes"]))
            for role in profiles:
                finished.add(role)
                command(role, "quit")
                processes[role].wait(timeout=10)
                if processes[role].returncode or not latest(role, "phase8_desktop_complete")["success"]:
                    raise RuntimeError("Client completion failed")
        finally:
            for process in reversed(list(processes.values())):
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
    summary = dict(success=True, phase=args.phase, checks=checks, save_before=before,
                   save_after=hashlib.sha256(save.read_bytes()).hexdigest(),
                   server_stop="terminated after client completion; no shutdown save", dialog_clicks_verified=False)
    (args.output / "result.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("PASS native character desktops " + args.phase + ": " + str(args.output))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("complete", "restart"))
    for name in ("runtime", "output", "binary", "profiles"):
        parser.add_argument("--" + name, type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument("--cell", required=True)
    parser.add_argument("--server-binary", type=lambda p: Path(p).resolve(), help="Optional separately built native server executable")
    parser.add_argument("--steps", type=lambda p: Path(p).resolve(), help="Bounded JSON commands/assertions through normal desktop delegates")
    parser.add_argument("--interactive-seconds", type=int, choices=range(121), default=0, metavar="0..120")
    capture(parser.parse_args())
