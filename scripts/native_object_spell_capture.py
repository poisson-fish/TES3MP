"""Verify two desktop Touch Lock/Open and door activation over impaired UDP."""

from dataclasses import asdict
import json
import time


def verify_object_spell_capture(output, evidence, processes, relay, manifest):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def rows(role, event):
        return [row for row in records(evidence[role]) if row.get("event") == event]

    def latest_door(role):
        shown = rows(role, "native_door_presented")
        return shown[-1]["doors"][0] if shown and shown[-1]["doors"] else None

    def wait_for(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(description, flush=True)
                return value
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

    def converged(level, prior=None):
        doors = {role: latest_door(role) for role in evidence}
        if any(door is None or door["lock"] != level or door["rendered_locked"] != (level > 0)
               for door in doors.values()):
            return None
        if len({door["id"] for door in doors.values()}) != 1 or len({door["contact_revision"] for door in doors.values()}) != 1:
            return None
        if prior is not None and doors["Alice"]["contact_revision"] == prior:
            return None
        return doors

    def samples(role):
        return rows(role, "native_combat_sample")

    def spell_events(role, source):
        return [event for sample in samples(role) for event in sample["magic_events"]
                if event["caster"] == 1 and event["source"] == source and event["success"]]

    def cast_cues(role):
        return [cue for sample in samples(role) for cue in sample["magic_casts"]
                if cue["kind"] == 1 and cue["caster"] == 1]

    def spell_id(name):
        value = 14695981039346656037
        for byte in name.encode("ascii"):
            value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
        return value

    lock_id, open_id = spell_id("door_spell_lock"), spell_id("door_spell_open")
    baseline = wait_for(lambda: converged(0) if all(samples(role) for role in evidence) else None,
                        "two desktops received the unlocked door", 75)
    initial_magicka = samples("Alice")[-1]["magicka"]
    command("Alice", "pose 0 -75 1 0 0")
    wait_for(lambda: rows("Alice", "traversal_pose")[-1]["focus"] == "npc_door",
             "Alice focuses the streamed door")
    command("Alice", 'castdoor "door_spell_lock"')
    locked = wait_for(lambda: converged(50, baseline["Alice"]["contact_revision"]),
                      "both desktops received Lock level 50 and contact revision")
    wait_for(lambda: all(len(spell_events(role, lock_id)) == 1 for role in evidence),
             "both desktops received one committed Lock event")
    after_lock_magicka = samples("Alice")[-1]["magicka"]
    if after_lock_magicka != initial_magicka - 5:
        raise RuntimeError(f"Lock spell cost mismatch: {initial_magicka} -> {after_lock_magicka}")
    wait_for(lambda: any(cast["id"] == 1 and cast["cast_phase"] == 0
                         for cast in samples("Alice")[-1]["casts"]),
             "Alice completed Lock recovery")
    command("Alice", "pose 0 -75 1 0 0")
    time.sleep(.2)
    focused = command("Alice", "observe")
    if focused["focus"] != "npc_door":
        raise RuntimeError(f"Alice lost the locked door focus: {focused['focus']!r}")
    command("Alice", 'activate "npc_door"')
    time.sleep(.75)
    blocked = {role: latest_door(role) for role in evidence}
    if any(door["lock"] != 50 or door["angle"] != baseline[role]["angle"]
           or door["contact_revision"] != locked[role]["contact_revision"]
           for role, door in blocked.items()):
        raise RuntimeError("Locked door activated or changed contact identity")
    command("Alice", "pose 0 -75 1 0 0")
    command("Alice", 'castdoor "door_spell_open"')
    unlocked = wait_for(lambda: converged(0, locked["Alice"]["contact_revision"]),
                        "both desktops received Open level 0 and contact revision")
    wait_for(lambda: all(len(spell_events(role, open_id)) == 1 for role in evidence),
             "both desktops received one committed Open event")
    paid_magicka = samples("Alice")[-1]["magicka"]
    if paid_magicka != initial_magicka - 10:
        raise RuntimeError("Open duplicated or missed spell cost")
    wait_for(lambda: any(cast["id"] == 1 and cast["cast_phase"] == 0
                         for cast in samples("Alice")[-1]["casts"]),
             "Alice completed Open recovery")
    command("Alice", "pose 0 -75 1 0 0")
    time.sleep(.2)
    focused = command("Alice", "observe")
    if focused["focus"] != "npc_door":
        raise RuntimeError(f"Alice lost the unlocked door focus: {focused['focus']!r}")
    command("Alice", 'activate "npc_door"')
    opened = wait_for(lambda: {role: latest_door(role) for role in evidence}
                      if all(latest_door(role) and latest_door(role)["angle"] > baseline[role]["angle"]
                             for role in evidence) else None,
                      "both desktops saw door activation resume")
    marker = len(records(evidence["Bob"]))
    generation = samples("Bob")[-1]["generation"]
    command("Bob", "reconnect")
    wait_for(lambda: samples("Bob")[-1]["generation"] > generation
             and latest_door("Bob") and latest_door("Bob")["lock"] == 0,
             "Bob reconnected to the current unlocked door")
    after = {role: latest_door(role) for role in evidence}
    if after["Bob"]["contact_revision"] != after["Alice"]["contact_revision"]:
        raise RuntimeError("Reconnect restored a stale door contact revision")
    cue_commands = {role: [cue["command"] for cue in cast_cues(role)] for role in evidence}
    if any(len(commands) != 2 or len(set(commands)) != 2 for commands in cue_commands.values()) \
            or cue_commands["Alice"] != cue_commands["Bob"]:
        raise RuntimeError(f"Spell presentation cues diverged or duplicated: {cue_commands}")
    if any(len(spell_events(role, lock_id)) != 1 or len(spell_events(role, open_id)) != 1
           for role in evidence) or any(sample["magicka"] != paid_magicka
                                      for sample in samples("Alice")[-3:]):
        raise RuntimeError("Reconnect duplicated spell event or cost")
    if any(event for event in records(evidence["Bob"])[marker:]
           if event.get("event") == "native_combat_sample"
           and (event["magic_events"] or event["magic_casts"] or event["magic_impacts"])):
        raise RuntimeError("Reconnect replayed a spell event or cue")
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="two-desktop Touch Lock/Open on streamed door",
                  manifest=manifest, baseline=baseline, locked=locked, blocked=blocked,
                  unlocked=unlocked, opened=opened, reconnected=after,
                  magicka=[initial_magicka, after_lock_magicka, paid_magicka],
                  cast_cues=cue_commands,
                  relay=asdict(relay.stop()),
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay")
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
