"""One real-record Target cast, two rendered desktops, impaired UDP and reconnect."""

from dataclasses import asdict
import json
import shutil
import time


def verify_magic_visual_capture(output, evidence, processes, relay, manifest, encounter):
    from run_native_navigation_capture import records
    spell = encounter["spell"]

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def rows(role, event):
        return [row for row in records(evidence[role]) if row.get("event") == event]

    def wait_for(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(description, flush=True)
                return value
            exited = [(name, process.returncode) for name, process in processes.items()
                      if name not in finished and process.poll() is not None]
            if exited:
                raise RuntimeError(f"process exited before {description}: {exited}")
            time.sleep(.025)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="utf-8")
        for retry in range(40):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                if retry == 39:
                    raise
                time.sleep(.01)
        wait_for(lambda: any(row.get("sequence") == sequence[role]
                             and row.get("event") == "traversal_" + action.split()[0]
                             for row in records(evidence[role])), f"{role}: {action}")

    def snapshots(role):
        return rows(role, "native_combat_sample")

    wait_for(lambda: all(len(snapshots(role)) >= 3 and snapshots(role)[-1]["actors"]
                         for role in evidence), "two desktop combat baselines", 75)
    for role, x in (("Alice", -80), ("Bob", -160)):
        command(role, f"pose {x} -500 1 0 0")
        command(role, "thirdperson")
    before = {role: len(records(path)) for role, path in evidence.items()}
    command("Alice", f'castactor "{spell}"')

    def flights(role):
        return [p for row in snapshots(role) for p in row["magic_flights"]
                if p["kind"] == 1 and p["caster"] == 1]

    first = wait_for(lambda: next(iter(flights("Alice")), None), "committed Target flight", 20)
    key = (first["kind"], first["caster"], first["life"], first["command"])

    def matching(role, event, field):
        return [p for row in rows(role, event) for p in row[field]
                if (p["kind"], p["caster"], p["life"], p["command"]) == key]

    wait_for(lambda: all(len({tuple(p["position"]) for p in matching(role,
        "magic_projectile_presentation_frame", "projectiles")}) >= 2 for role in evidence),
        "both desktops rendered moving bolt meshes", 15)
    wait_for(lambda: all(any(p["looping_sounds"] > 0 for p in matching(role,
        "magic_projectile_presentation_frame", "projectiles")) for role in evidence),
        "both desktops started stock looping bolt sound", 10)
    if "target_effects" in encounter:
        count = int(encounter["target_effects"])
        expected = [float(value) for value in encounter["bolt_light"].split()]
        if count < 2 or len(expected) != 4:
            raise RuntimeError("invalid multi-effect fixture visual expectation")
        for role in evidence:
            for frame in matching(role, "magic_projectile_presentation_frame", "projectiles"):
                if frame["models"] != count + 1 or any(abs(a - b) > .0001
                        for a, b in zip(frame["light_color"], expected)):
                    raise RuntimeError(f"{role} composite bolt/light disagrees with authored effects")
        print("both desktops assembled composite bolt and blended light", flush=True)
    for role in evidence:
        automatic = evidence[role].with_suffix(".ndjson.control.magic-bolt.png")
        wait_for(lambda: automatic.exists() and automatic.read_bytes().endswith(
            b"\x00\x00\x00\x00IEND\xaeB`\x82"), f"{role} in-flight screenshot", 10)
        shutil.copyfile(automatic, output / f"{role}-bolt-auto.png")
        command(role, "screenshot")
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(
            output / f"{role}-bolt.png")
    wait_for(lambda: all(matching(role, "native_combat_sample", "magic_impacts")
                         for role in evidence), "both desktops presented committed impact", 15)
    for role in evidence:
        command(role, "screenshot")
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(
            output / f"{role}-impact.png")

    first_impact = matching("Alice", "native_combat_sample", "magic_impacts")[0]
    if first_impact != matching("Bob", "native_combat_sample", "magic_impacts")[0]:
        raise RuntimeError("observers disagree on the impact cue")
    if encounter.get("profile") == "vanilla-multi-loop":
        if any(len(matching(role, "native_combat_sample", "magic_casts")) != 1
               or len(matching(role, "native_combat_sample", "magic_impacts")) != 1 for role in evidence):
            raise RuntimeError("cast or hit visual cue was missing or duplicated")
        def loops(role):
            return [effect for row in snapshots(role)[-3:] for actor in row["visual_loops"]
                    if actor["kind"] == 2 for effect in actor["effects"]]
        wait_for(lambda: all(loops(role) for role in evidence),
                 "both desktops received active stock effect loops", 8)
        active = set(loops("Bob"))
        for role in evidence:
            command(role, "screenshot")
            evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(
                output / f"{role}-active-loop.png")
    before_generation = snapshots("Bob")[-1]["generation"]
    marker = len(records(evidence["Bob"]))
    command("Bob", "reconnect")
    wait_for(lambda: snapshots("Bob")[-1]["generation"] > before_generation,
             "Bob reconnected after impact")
    if any(row.get("event") == "native_combat_sample" and (row["magic_impacts"] or row["magic_casts"])
           for row in records(evidence["Bob"])[marker:]):
        raise RuntimeError("reconnect replayed a cast or hit cue")
    if encounter.get("profile") == "vanilla-multi-loop":
        wait_for(lambda: active.intersection(loops("Bob")),
                 "Bob restored active effect loops from the reconnect snapshot", 8)
        command("Bob", "screenshot")
        evidence["Bob"].with_suffix(f".ndjson.control.{sequence['Bob']}.png").rename(
            output / "Bob-reconnected-loop.png")
        wait_for(lambda: not loops("Alice") and not loops("Bob"),
                 "both desktops ended effect loops on expiry", 20)
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, spell=spell, flight=key, impact=first_impact,
                  visual_frames={role: len(matching(role, "magic_projectile_presentation_frame", "projectiles"))
                                 for role in evidence},
                  distinct_positions={role: len({tuple(p["position"]) for p in matching(role,
                      "magic_projectile_presentation_frame", "projectiles")}) for role in evidence},
                  looping_sounds={role: max(p["looping_sounds"] for p in matching(role,
                      "magic_projectile_presentation_frame", "projectiles")) for role in evidence},
                  composite={"target_effects": count, "models": count + 1, "light_color": expected}
                      if "target_effects" in encounter else None,
                  restored_visual_loops=sorted(active) if encounter.get("profile") == "vanilla-multi-loop" else None,
                  reconnect_generation=snapshots("Bob")[-1]["generation"],
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay",
                  relay=asdict(relay.stop()), manifest=manifest,
                  screenshots=[p.name for p in output.glob("*.png")])
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
