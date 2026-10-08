"""Two rendered V85 clients: private choices, independent stories, silent recovery.

Uses the normal dialogue window's topic/choice handlers. Requires the bounded
north/south fixture and a fresh campaign for the first run; never edits saves.
This is synthetic semantic input, not physical desktop-click acceptance.
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


def read_json(path, limit):
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError("Dialogue capture input exceeds its byte budget")
    return json.loads(data)


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
    save = args.runtime / (settings["player_identity_file"] + ".world-v2")
    previous = read_json(args.recover_from, 65536) if args.recover_from else None
    if previous and (not previous.get("success") or not save.exists()
                     or hashlib.sha256(save.read_bytes()).hexdigest() != previous["durable_sha256"]):
        raise ValueError("Restart requires the exact successful prior campaign")
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

        def command(role, action, event=None, asynchronous=False):
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
            if asynchronous:
                return sequence
            return wait(lambda: next((row for row in evidence.rows(role, event or "traversal_" + action.split()[0])
                if row.get("sequence") == sequence), None), role + ": " + action)

        def views(role, story):
            return [v for v in evidence.rows(role, "personal_dialogue_presented") if v["story"] == story]

        def view(role, story):
            rows = views(role, story)
            return rows[-1] if rows else None

        def progression(role, story, revision, index, choices):
            v = view(role, story)
            return v and v["revision"] == revision and len(v["choices"]) == choices and (
                v["quests"] == [] if index == 0 else len(v["quests"]) == 1 and v["quests"][0]["index"] == index
                and v["quests"][0]["finished"] == (index in (20, 30)))

        def screenshot(role, label):
            if args.no_screenshots:
                return
            destination = args.output / f"{role}-{label}.png"
            subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                str(Path(__file__).with_name("capture_desktop_window.ps1")), "-ProcessId", str(processes[role].pid),
                "-Output", str(destination)], check=True, capture_output=True)
            result.setdefault("screenshots", []).append(destination.name)

        def reconnect(role):
            before = {story: view(role, story) for story in (91, 92)}
            starts = {story: len(views(role, story)) for story in before}
            count = len(evidence.rows(role, "phase8_desktop_status"))
            command(role, "dialogueclose")
            command(role, "disconnectbrief")
            wait(lambda: any(r.get("status") == "resumed" for r in evidence.rows(role, "phase8_desktop_status")[count:]),
                 role + " resumed", 60)
            wait(lambda: all((v := view(role, s)) and v["generation"] > before[s]["generation"] for s in before),
                 role + " recovered private views")
            for story, old in before.items():
                recovered = views(role, story)[starts[story]:]
                for v in recovered:
                    if v["response"] or v["messages"] or any(v[k] != old[k] for k in ("revision", "quests", "choices", "command")):
                        raise RuntimeError("Reconnect replayed output or changed progression")
            command(role, 'dialoguestart "npc_door_actor"')
            if view(role, 92)["choices"]:
                command(role, 'dialoguetopic "south_passage_92 (story 92)"')
            screenshot(role, "reconnect")
            result["checks"].append(dict(role=role, reconnect=True, silent=True))

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
            profiles = read_json(args.runtime / "profiles.json", 16384)
            if set(profiles) != set(paths):
                raise ValueError("Exactly Alice and Bob profiles are required")
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
            wait(lambda: all(view(r, s) for r in paths for s in (91, 92)), "four isolated story baselines")
            for role in paths:
                command(role, "close")
                command(role, 'dialoguestart "npc_door_actor"')
            relay.schedule = NavigationImpairment()
            if previous:
                for role in paths:
                    for story in (91, 92):
                        old = previous["final_views"][role][str(story)]
                        for v in views(role, story):
                            if v["response"] or v["messages"] or any(v[k] != old[k] for k in ("revision", "quests", "choices", "command")):
                                raise RuntimeError("Restart replayed output or lost private state")
                result["checks"].append(dict(restart=True, silent=True, pending_choices=True))
                # The current window restores story 91 first. Select the second story's topic.
                for role in paths:
                    command(role, 'dialoguetopic "south_passage_92 (story 92)"')
                # Opening the second story must restore its offered choices without executing a topic.
                for role in paths:
                    command(role, "dialoguechoice " + ("7" if role == "Alice" else "9"), asynchronous=True)
                wait(lambda: progression("Alice", 92, 2, 20, 0) and progression("Bob", 92, 2, 30, 0),
                     "restarted choices progress independently")
            else:
                command("Alice", 'dialoguetopic "north_passage_91 (story 91)"')
                wait(lambda: progression("Alice", 91, 1, 10, 2), "Alice private response and offered choices")
                if not progression("Bob", 91, 0, 0, 0) or not progression("Alice", 92, 0, 0, 0):
                    raise RuntimeError("First dialogue crossed character/story ownership")
                screenshot("Alice", "private-choice")
                screenshot("Bob", "before-own-topic")
                command("Bob", 'dialoguetopic "north_passage_91 (story 91)"')
                wait(lambda: progression("Bob", 91, 1, 10, 2), "Bob owns separate choices")
                for role in paths:
                    command(role, "dialoguechoice " + ("7" if role == "Alice" else "9"), asynchronous=True)
                wait(lambda: progression("Alice", 91, 2, 20, 0) and progression("Bob", 91, 2, 30, 0),
                     "concurrent divergent progression")
                for role in paths:
                    command(role, 'dialoguetopic "south_passage_92 (story 92)"', asynchronous=True)
                wait(lambda: all(progression(r, 92, 1, 10, 2) for r in paths), "independent second story pending choices")
                result["checks"].append(dict(private_responses=True, private_choices=True, concurrent_divergence=True,
                                             separate_stories=True))
            for role in paths:
                reconnect(role)
            for role in paths:
                for story, direction in ((91, "north"), (92, "south")):
                    transients = [v for v in views(role, story) if v["response"] or v["messages"]]
                    expected = 1 if previous and story == 92 else 0 if previous else 2 if story == 91 else 1
                    if len(transients) != expected or len({v["revision"] for v in transients}) != expected:
                        raise RuntimeError("Duplicate/missing response occurrence")
                    for v in transients:
                        stat, threshold = ("Strength", 40) if story == 91 else ("Destruction", 20)
                        response = (f'Your {stat} is {"below" if role == "Alice" else "at least"} {threshold}. '
                                    f'The {direction} passage awaits.' if v["revision"] == 1 else
                                    f'Your {direction} passage is {"taken" if role == "Alice" else "declined"}.')
                        messages = [f'Private {direction} notice'] if v["revision"] == 1 else []
                        if v["response"] != response or v["messages"] != messages:
                            raise RuntimeError("Private response/choice branch differs")
            if not previous:
                result["checks"].append(dict(stat_filters=True, strength_threshold=40, destruction_threshold=20,
                                             Alice="below both", Bob="at least both"))
            result["final_views"] = {r: {s: view(r, s) for s in (91, 92)} for r in paths}
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
            result["relay"] = vars(relay.stop())
            for role, process in processes.items():
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            result["exit_codes"] = {r: p.returncode for r, p in processes.items()}
            if save.exists():
                result["durable_sha256"] = hashlib.sha256(save.read_bytes()).hexdigest()
            (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    if not result["success"]:
        raise RuntimeError(result["error"])
    print("PASS rendered private dialogue: " + str(args.output))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "output", "binary", "server-binary"):
        parser.add_argument("--" + name, type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument("--start", required=True)
    parser.add_argument("--recover-from", type=lambda p: Path(p).resolve())
    parser.add_argument("--no-screenshots", action="store_true",
                        help="Validate semantic evidence without native-window capture; this remains synthetic")
    capture(parser.parse_args())
