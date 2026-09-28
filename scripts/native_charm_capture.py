"""Capture stock dialogue disposition before, during and after committed Charm."""

from dataclasses import asdict
import json
from pathlib import Path
import subprocess
import time


def verify_charm_capture(output, evidence, processes, relay, manifest):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def wait_for(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(description, flush=True)
                return value
            if any(process.poll() is not None for name, process in processes.items() if name not in finished):
                raise RuntimeError(f"process exited: {description}")
            time.sleep(.05)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        for attempt in range(40):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                if attempt == 39:
                    raise
                time.sleep(.01)
        event = "traversal_" + action.split()[0]
        return wait_for(lambda: next((r for r in records(evidence[role])
            if r.get("sequence") == sequence[role] and r.get("event") == event), None),
            f"{role}: {action}")

    def dialogue(role):
        command(role, 'dialogue "npc_door_actor"')
        return next(r for r in reversed(records(evidence[role]))
                    if r.get("event") == "traversal_dialogue_effect"
                    and r.get("sequence") == sequence[role])

    def screenshot(role, label):
        time.sleep(.6)  # Let the GUI draw the newly opened dialogue frame.
        destination = output / f"{role}-{label}.png"
        subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                        str(Path(__file__).with_name("capture_desktop_window.ps1")),
                        "-ProcessId", str(processes[role].pid), "-Output", str(destination)],
                       check=True, capture_output=True, text=True)
        return destination.name

    def charm(role):
        samples = [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]
        if not samples:
            return 0
        npc = next((v for v in samples[-1].get("visibility", []) if v["kind"] == 2), None)
        return npc["effects"][7] if npc else 0

    wait_for(lambda: all(any(r.get("event") == "native_combat_sample" for r in records(path))
                         for path in evidence.values()), "two desktop baselines", 75)
    command("Alice", "pose 60 -100 1 .1 0")
    time.sleep(.35)
    command("Alice", "observe")
    command("Alice", 'dialoguestart "npc_door_actor"')
    baseline = dialogue("Alice")
    frames = [screenshot("Alice", "before-charm")]
    command("Alice", "dialogueclose")
    command("Alice", 'castactor "ai_charm_dialogue"')
    wait_for(lambda: all(charm(role) >= 35 for role in evidence), "Charm projected to both desktops")
    command("Alice", 'dialoguestart "npc_door_actor"')
    active = dialogue("Alice")
    frames.append(screenshot("Alice", "during-charm"))
    if not (baseline["charm"] == 0 and active["charm"] >= 35
            and active["disposition"] > baseline["disposition"]):
        raise RuntimeError("Stock dialogue disposition did not rise with committed Charm")
    command("Alice", "dialogueclose")
    wait_for(lambda: all(charm(role) == 0 for role in evidence), "Charm expired on both desktops", 35)
    command("Alice", 'dialoguestart "npc_door_actor"')
    expired = dialogue("Alice")
    frames.append(screenshot("Alice", "after-charm"))
    if expired["charm"] != 0 or expired["disposition"] != baseline["disposition"]:
        raise RuntimeError("Stock dialogue disposition did not return after Charm expiry")
    command("Alice", "dialogueclose")
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="V57 Charm in stock dialogue on two OpenMW desktops",
                  manifest=manifest, baseline=baseline, active=active, expired=expired,
                  screenshots=frames, relay=asdict(relay.stop()))
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
