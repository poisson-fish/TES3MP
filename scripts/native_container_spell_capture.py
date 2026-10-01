"""Two desktop Touch Lock/Open and stock container activation under impaired UDP."""

from dataclasses import asdict
import json
import time


def verify_container_spell_capture(output, evidence, processes, relay, manifest):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def rows(role, event):
        return [row for row in records(evidence[role]) if row.get("event") == event]

    def latest(role):
        values = rows(role, "traversal_inventory")
        return values[-1] if values else None

    def wait_for(predicate, description, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                print(description, flush=True)
                return result
            exited = [(name, process.poll()) for name, process in processes.items()
                      if name not in finished and process.poll() is not None]
            if exited:
                raise RuntimeError(f"process exited before {description}: {exited}")
            time.sleep(.05)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        for retry in range(30):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                if retry == 29:
                    raise
                time.sleep(.02)
        return wait_for(lambda: next((row for row in records(evidence[role])
                            if row.get("sequence") == sequence[role]
                            and row.get("event") == "traversal_" + action.split()[0]), None),
                        f"{role}: {action}")

    def converged(level, previous=None):
        values = {role: latest(role) for role in evidence}
        if any(value is None or value["container_lock"] != level
               or value["rendered_container_locked"] != (level > 0)
               or not value["container_id"] for value in values.values()):
            return None
        if len({value["container_id"] for value in values.values()}) != 1 or len({
                value["container_contact_revision"] for value in values.values()}) != 1:
            return None
        if previous is not None and values["Alice"]["container_contact_revision"] == previous:
            return None
        return values

    def samples(role):
        return rows(role, "native_combat_sample")

    def spell_id(name):
        result = 14695981039346656037
        for byte in name.encode("ascii"):
            result = ((result ^ byte) * 1099511628211) & 0xffffffffffffffff
        return result

    def committed(role, name):
        return [event for sample in samples(role) for event in sample["magic_events"]
                if event["caster"] == 1 and event["source"] == spell_id(name) and event["success"]]

    baseline = wait_for(lambda: converged(0) if all(samples(role) for role in evidence) else None,
                        "both desktops received the ordinary unlocked container", 75)
    initial_magicka = samples("Alice")[-1]["magicka"]
    focus_pose = None
    focus_seen = []
    for distance in (-175, -145, -205):
        for pitch in (0.2, 0.5, 0.8, -0.3, -0.6):
            candidate = f"pose 30 {distance} 1 {pitch} 0"
            command("Alice", candidate)
            focused = command("Alice", "observe")
            focus_seen.append((candidate, focused["focus"]))
            if focused["focus"] == "npc_spell_container":
                focus_pose = candidate
                break
        if focus_pose:
            break
    if not focus_pose:
        raise RuntimeError(f"Container focus missing: {focus_seen}")
    command("Alice", 'castcontainer "door_spell_lock"')
    locked = wait_for(lambda: converged(50, baseline["Alice"]["container_contact_revision"]),
                      "both desktops received committed Lock 50")
    wait_for(lambda: all(len(committed(role, "door_spell_lock")) == 1 for role in evidence),
             "both desktops received one container Lock event")
    if samples("Alice")[-1]["magicka"] != initial_magicka - 5:
        raise RuntimeError("Container Lock payment mismatch")
    wait_for(lambda: any(cast["id"] == 1 and cast["cast_phase"] == 0
                         for cast in samples("Alice")[-1]["casts"]),
             "Alice finished Lock recovery")
    command("Alice", focus_pose)
    command("Alice", 'activate "npc_spell_container"')
    denied = command("Alice", "observe")
    if denied["focus"] != "npc_spell_container" or latest("Alice")["container_window_open"]:
        raise RuntimeError("Locked container activation opened the inventory window")
    if not converged(50):
        raise RuntimeError("Locked activation changed the committed container")
    command("Alice", focus_pose)
    command("Alice", 'castcontainer "door_spell_open"')
    opened = wait_for(lambda: converged(0, locked["Alice"]["container_contact_revision"]),
                      "both desktops received committed Open 0")
    wait_for(lambda: all(len(committed(role, "door_spell_open")) == 1 for role in evidence),
             "both desktops received one container Open event")
    if samples("Alice")[-1]["magicka"] != initial_magicka - 10:
        raise RuntimeError("Container Open payment mismatch")
    wait_for(lambda: any(cast["id"] == 1 and cast["cast_phase"] == 0
                         for cast in samples("Alice")[-1]["casts"]),
             "Alice finished Open recovery")
    command("Alice", focus_pose)
    command("Alice", 'activate "npc_spell_container"')
    accepted = command("Alice", "observe")
    if not latest("Alice")["container_window_open"]:
        raise RuntimeError("Open did not restore stock container activation")
    command("Alice", "close")
    generation = samples("Bob")[-1]["generation"]
    marker = len(records(evidence["Bob"]))
    command("Bob", "reconnect")
    wait_for(lambda: samples("Bob")[-1]["generation"] > generation
             and converged(0, locked["Alice"]["container_contact_revision"]),
             "Bob reconnected to the opened container")
    cues = {role: [cue["command"] for sample in samples(role) for cue in sample["magic_casts"]
                   if cue["kind"] == 1 and cue["caster"] == 1] for role in evidence}
    if any(len(values) != 2 or len(set(values)) != 2 for values in cues.values()) or cues["Alice"] != cues["Bob"]:
        raise RuntimeError(f"Container cast cues diverged: {cues}")
    if any(event for event in records(evidence["Bob"])[marker:]
           if event.get("event") == "native_combat_sample"
           and (event["magic_events"] or event["magic_casts"] or event["magic_impacts"])):
        raise RuntimeError("Reconnect replayed a container cast cue")
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not exit cleanly")
        finished.add(role)
    report = dict(success=True, scenario="two-desktop Touch Lock/Open on one ordinary container",
                  manifest=manifest, baseline=baseline, locked=locked, activation_denied=denied,
                  opened=opened, activation_restored=accepted, cues=cues,
                  relay=asdict(relay.stop()),
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay")
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
