"""Two-desktop visibility capture through ordinary cast intents and OpenMW consumers."""

from dataclasses import asdict
import json
import time


def verify_visibility_capture(output, evidence, processes, relay, manifest):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def visible(role, player):
        data = samples(role)
        return next((entry["effects"] for entry in data[-1].get("visibility", [])
                     if entry["kind"] == 1 and entry["id"] == player), None) if data else None

    def wait_for(predicate, description, timeout=30):
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
        return wait_for(lambda: next((r for r in records(evidence[role])
            if r.get("sequence") == sequence[role]
            and r.get("event") == "traversal_" + action.split()[0]), None), f"{role}: {action}")

    def cast(role, spell):
        player = 1 if role == "Alice" else 2
        wait_for(lambda: samples(role) and all(c["cast_id"] == 0 for c in samples(role)[-1]["casts"]
                     if c["id"] == player), f"{role} cast recovery")
        command(role, f'cast "{spell}"')

    frames = []

    def screenshot(role, label):
        command(role, "screenshot")
        destination = output / f"{role}-{label}.png"
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(destination)
        frames.append(destination.name)

    wait_for(lambda: all(len(samples(role)) >= 3 and visible(role, 1) and visible(role, 2)
                         for role in evidence), "two visibility baselines", 75)
    command("Alice", "thirdperson")
    command("Bob", "thirdperson")
    command("Bob", "facepeer")
    for role in evidence:
        screenshot(role, "baseline")

    cast("Alice", "visibility_41")
    wait_for(lambda: all(visible(role, 1)[2] >= 19 for role in evidence), "Light reaches both clients")
    light = {role: visible(role, 1) for role in evidence}
    for role in evidence:
        screenshot(role, "light")

    wait_for(lambda: all(visible(role, 1)[2] == 0 for role in evidence),
             "Light expires before NightEye comparison", 15)
    for role in evidence:
        screenshot(role, "light-expired")

    cast("Alice", "visibility_43")
    wait_for(lambda: all(visible(role, 1)[3] >= 19 for role in evidence), "NightEye reaches both clients")
    night_eye = {role: visible(role, 1) for role in evidence}
    for role in evidence:
        screenshot(role, "night-eye")

    command("Bob", "pose 100 -200 1 0 0")
    detections = {}
    for name, effect_id, field, slot in (("animal", 64, "creatures", 4),
                                         ("enchantment", 65, "enchantments", 5),
                                         ("key", 66, "keys", 6)):
        cast("Bob", f"visibility_{effect_id}")
        wait_for(lambda: all(visible(role, 2)[slot] >= 19 for role in evidence),
                 f"Detect {name} reaches both clients")
        time.sleep(.35)  # HUD marker widgets refresh every quarter second.
        command("Bob", "detect")
        marker = next((r for r in reversed(records(evidence["Bob"]))
                       if r.get("event") == "traversal_detect_markers"
                       and r.get("sequence") == sequence["Bob"]), None)
        if not marker or marker[field] < 1 or marker["widget_" + field] < 1:
            raise RuntimeError(f"Detect {name} did not create an OpenMW HUD map marker")
        detections[name] = marker
        screenshot("Bob", f"detect-{name}")

    wait_for(lambda: all(visible(role, 1)[2] == 0 and visible(role, 1)[3] == 0
                         and visible(role, 2)[4:] == [0, 0, 0] for role in evidence),
             "Light, NightEye and Detect expire on both clients", 35)
    for role in evidence:
        screenshot(role, "expired")

    cast("Alice", "visibility_invisibility")
    wait_for(lambda: all(visible(role, 1)[0] > 0 for role in evidence), "Invisibility reaches both clients")
    command("Bob", "facepeer")
    for role in evidence:
        screenshot(role, "invisible")
    cast("Alice", "visibility_41")
    wait_for(lambda: all(visible(role, 1)[0] == 0 and visible(role, 1)[2] >= 19
                         for role in evidence), "a committed action breaks Invisibility")
    for role in evidence:
        screenshot(role, "action-break")

    cast("Alice", "visibility_invisibility")
    wait_for(lambda: all(visible(role, 1)[0] > 0 for role in evidence), "Invisibility recast")
    generation = samples("Bob")[-1]["generation"]
    marker = len(records(evidence["Bob"]))
    command("Bob", "reconnect")
    wait_for(lambda: samples("Bob")[-1]["generation"] > generation
             and visible("Bob", 1)[0] > 0
             and any(r.get("event") == "phase8_desktop_status" and r.get("status") == "resumed"
                     for r in records(evidence["Bob"])[marker:]),
             "Bob reconnects into the active Invisibility", 25)
    screenshot("Bob", "reconnected-invisible")
    wait_for(lambda: all(visible(role, 1)[0] == 0 for role in evidence),
             "Invisibility expires after reconnect", 25)
    for role in evidence:
        screenshot(role, "invisibility-expired")

    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="V55 visibility on two OpenMW desktops",
                  synthetic_room=True, real_loadout=True, manifest=manifest,
                  light=light, night_eye=night_eye, detections=detections,
                  reconnect_generation=samples("Bob")[-1]["generation"],
                  screenshots=frames, relay=asdict(relay.stop()))
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
