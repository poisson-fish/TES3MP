"""Rendered exploration acceptance on a prepared bounded V77 exterior fixture.

Same-cell pose commands place clients near an edge; AutoMove performs every
cell crossing through inherited desktop movement. Uses real inventory/attack
delegates and impaired UDP. Never edits the canonical save or grants items.
"""
import argparse
from contextlib import ExitStack
from dataclasses import asdict
import hashlib
import json
import math
import os
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
    save = args.runtime / (settings["player_identity_file"] + ".world-v2")
    expected_path = args.runtime / "exploration-expected.json"
    expected = json.loads(expected_path.read_text()) if args.phase != "travel" else None
    before = hashlib.sha256(save.read_bytes()).hexdigest() if save.exists() else None
    if args.phase != "travel" and before is None:
        raise RuntimeError("Restart requires the existing canonical save")
    paths = {r: args.output / (r + ".ndjson") for r in ("Alice", "Bob")}
    evidence = Evidence(paths)
    sequences = dict.fromkeys(paths, 0)
    processes, finished, checks, screenshots = {}, set(), [], []
    relay = UdpImpairmentRelay(free_port(), int(settings["port"]), "direct")
    relay.schedule = NavigationImpairment()
    environment = os.environ.copy()
    dependencies = Path(__file__).resolve().parents[1] / "deps/installed/x64-windows/bin"
    environment["PATH"] = os.pathsep.join((str(args.binary), str(dependencies), environment.get("PATH", "")))
    environment["OSG_LIBRARY_PATH"] = str(dependencies / "osgPlugins-3.6.5")
    startup = args.output / "startup.txt"
    startup.write_text("StopScript CharGen\nset CharGenState to -1\nEnableInventoryMenu\nEnableMagicMenu\n"
                       "EnableMapMenu\nEnableStatsMenu\nEnablePlayerControls\n")
    result = dict(success=False, phase=args.phase, synthetic_content=True,
                  crossing_input="same-cell setup followed by desktop AutoMove", checks=checks, screenshots=screenshots,
                  save_before=before, impairment="100ms delay, +/-25ms jitter, 10% loss, periodic reorder")
    with ExitStack() as stack:
        def launch(role, command):
            stdout = stack.enter_context((args.output / (role + ".stdout.log")).open("wb"))
            stderr = stack.enter_context((args.output / (role + ".stderr.log")).open("wb"))
            processes[role] = subprocess.Popen(command, cwd=args.binary, env=environment, stdout=stdout, stderr=stderr,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)

        def latest(role, event):
            rows = evidence.rows(role, event)
            return rows[-1] if rows else None

        def wait(predicate, description, timeout=45):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                value = predicate()
                if value:
                    print(description, flush=True)
                    return value
                exited = [(r, p.poll()) for r, p in processes.items() if r not in finished and p.poll() is not None]
                if exited:
                    raise RuntimeError(f"{description}: process exited {exited}")
                time.sleep(.1)
            raise RuntimeError("Timed out: " + description)

        def command(role, action):
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
            return wait(lambda: next((r for r in evidence.rows(role, "traversal_" + action.split()[0])
                if r.get("sequence") == sequence), None), role + ": " + action)

        def sample(role):
            return latest(role, "native_combat_sample")

        def inventory(role):
            command(role, "observe")
            return latest(role, "traversal_inventory")["player"]

        def screenshot(role, name):
            command(role, "screenshot")
            path = args.output / (role + "-" + name + ".png")
            Path(str(paths[role]) + f".control.{sequences[role]}.png").rename(path)
            screenshots.append(path.name)

        def cross(role, origin, destination):
            if abs(destination - origin) != 1:
                raise ValueError("Only adjacent crossings are supported")
            direction = 1 if destination > origin else -1
            edge = (origin + (direction > 0)) * 8192
            x = edge - direction * 24
            command(role, f"pose {x} 820200 257 0 {math.pi/2 if direction > 0 else -math.pi/2}")
            wait(lambda: (s := latest(role, "native_player_sample")) and abs(s["position"][0]/1024-x) < 12,
                 role + " same-cell setup committed")
            command(role, "autowalk")
            wait(lambda: (s := latest(role, "native_player_sample")) and
                 math.floor(s["position"][0]/1024/8192) == destination and
                 (s["position"][0]/1024-edge)*direction >= 30, role + f" crossed {origin}->{destination}")
            command(role, "autowalk")
            observed = command(role, "observe")
            if math.floor(observed["position"][0]/8192) != destination or not observed["inventory_matches"]:
                raise RuntimeError("Rendered crossing failed to converge")
            checks.append(dict(role=role, crossing=[origin, destination], cell=observed["cell"], rendered=True))
            screenshot(role, f"cross-{origin}-{destination}-{sequences[role]}")

        try:
            launch("server", [str(args.server_binary), str(args.runtime / "server.cfg")])
            def ready():
                with (args.output / "server.stdout.log").open("rb") as stream:
                    stream.seek(max(0, stream.seek(0, 2)-8192))
                    return b"server started" in stream.read()
            wait(ready, "server ready", 180)
            relay.start()
            for role in paths:
                user = args.runtime / (role + "-user")
                launch(role, [str(args.binary / "openmw.exe"), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1",
                    f"--tes3mp-port={relay.listen_port}", "--tes3mp-timeout-ms=15000",
                    f"--tes3mp-password-file={args.runtime / 'join-password.txt'}",
                    f"--tes3mp-player-credential-file={args.runtime / (role + '.credential')}",
                    f"--config={user}", "--replace=config", f"--user-data={user}",
                    f"--resources={args.binary / 'resources'}", "--start=3,100", "--skip-menu=1", "--new-game=0",
                    "--no-grab=1", "--no-sound=1", "--script-console=1", f"--script-run={startup}",
                    "--tes3mp-automation-role=native-traversal", f"--tes3mp-automation-output={paths[role]}"])
            wait(lambda: all(sample(r) and latest(r, "native_player_sample") for r in paths), "two rendered clients", 90)
            for role in paths:
                command(role, "close")
                command(role, "thirdperson")
            initial_inventory = {r: inventory(r) for r in paths}
            actors = {a["id"]: a["health"] for a in sample("Alice")["actors"]}
            if expected:
                if initial_inventory != expected["inventories"] or actors != {int(k): v for k, v in expected["actors"].items()}:
                    raise RuntimeError("Restart changed item identities/counts or committed actor health")
                checks.append(dict(restart_recovered=True, actor_ids=list(actors)))
            else:
                target = min(actors)
                command("Alice", "pose 24577 820200 257 0 1.5707963")
                time.sleep(2)  # Let the setup pose and combat revisions converge through the impaired lanes.
                for attempt in range(8):
                    command("Alice", f"swingat 0 1 {target}")
                    time.sleep(2)
                    if any(a["id"] == target and a["health"] < actors[target] for a in sample("Alice")["actors"]):
                        break
                else:
                    raise RuntimeError("Adjacent-cell attacks produced no committed damage")
                checks.append(dict(attack_attempts=attempt+1))
                wait(lambda: all(any(a["id"] == target and a["health"] < actors[target] for a in sample(r)["actors"])
                                 for r in paths), "both clients see cross-cell combat")
                checks.append(dict(adjacent_combat=True, target=target))
                time.sleep(2)
            if args.phase == "presentation":
                for index, role in enumerate(paths):
                    x = 24560 + index*60
                    command(role, f"pose {x} 820000 257 0 0")
                    wait(lambda: abs(latest(role, "native_player_sample")["position"][0]/1024-x) < 12,
                         role + " group-view setup committed")
                time.sleep(2)
                for role in paths:
                    observed = command(role, "observe")
                    if not observed["inventory_matches"] or observed["visible_originals"] or len(sample(role)["actors"]) != 2:
                        raise RuntimeError("Regrouped presentation duplicated originals or lost actors/inventory")
                    screenshot(role, "shared-actors")
                for role in paths:
                    finished.add(role)
                    command(role, "quit")
                    processes[role].wait(timeout=10)
                    if processes[role].returncode:
                        raise RuntimeError("Client completion failed")
                result.update(success=True, actor_ids=list(actors), inventories_preserved=True,
                              crossing_input="same-cell presentation framing; travel evidence is separate")
                print("PASS rendered exploration presentation: " + str(args.output), flush=True)
                return
            for role in paths:
                screenshot(role, "baseline")
            for origin in (3, 4, 5):
                cross("Alice", origin, origin+1)
            wait(lambda: not sample("Alice")["actors"] and len(sample("Bob")["actors"]) == 2,
                 "separated area visibility")
            checks.append(dict(separated=True))
            command("Bob", "disconnectbrief")
            wait(lambda: latest("Bob", "phase8_desktop_status") and latest("Bob", "phase8_desktop_status").get("status") == "resumed",
                 "Bob reconnects after area unload", 40)
            for origin in (3, 4, 5):
                cross("Bob", origin, origin+1)
            wait(lambda: all(len(command(r, "observe")["peer_equipment"]) == 1
                             and len(latest(r, "native_player_sample")["players"]) == 2 for r in paths),
                 "regrouped players")
            for role in paths:
                for origin in (6, 5, 4):
                    cross(role, origin, origin-1)
            wait(lambda: all(len(sample(r)["actors"]) == 2 for r in paths), "actors restored after reentry")
            final_inventory = {r: inventory(r) for r in paths}
            # Equipment wear is committed gameplay; identity/counts must survive.
            key = lambda stacks: [(s["stack"], s["item"], s["count"]) for s in stacks]
            if any(key(initial_inventory[r]) != key(final_inventory[r]) for r in paths):
                raise RuntimeError("Exploration duplicated or replaced inventory")
            final_actors = {a["id"]: a["health"] for a in sample("Alice")["actors"]}
            for role in paths:
                screenshot(role, "regrouped")
                finished.add(role)
                command(role, "quit")
                processes[role].wait(timeout=10)
                if processes[role].returncode:
                    raise RuntimeError("Client completion failed")
            expected_path.write_text(json.dumps(dict(inventories=final_inventory, actors=final_actors), indent=2)+"\n")
            result.update(success=True, actor_ids=list(final_actors), final_actor_health=final_actors,
                          inventories_preserved=True, save_after=hashlib.sha256(save.read_bytes()).hexdigest())
        except BaseException as error:
            result["error"] = str(error)
            raise
        finally:
            for process in reversed(list(processes.values())):
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
            result["relay"] = asdict(relay.stop())
            (args.output / "result.json").write_text(json.dumps(result, indent=2)+"\n")
    print("PASS rendered exploration " + args.phase + ": " + str(args.output))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("travel", "restart", "presentation"))
    for name in ("runtime", "output", "binary", "server-binary"):
        parser.add_argument("--"+name, type=lambda p: Path(p).resolve(), required=True)
    capture(parser.parse_args())
