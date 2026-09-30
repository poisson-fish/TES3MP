"""One real-record Target cast, two rendered desktops, impaired UDP and reconnect."""

from dataclasses import asdict
import json
import shutil
import time


def verify_magic_visual_capture(output, evidence, processes, relay, manifest, spell):
    from run_native_navigation_capture import records

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
    before_generation = snapshots("Bob")[-1]["generation"]
    marker = len(records(evidence["Bob"]))
    command("Bob", "reconnect")
    wait_for(lambda: snapshots("Bob")[-1]["generation"] > before_generation,
             "Bob reconnected after impact")
    if any(row.get("event") == "native_combat_sample" and row["magic_impacts"]
           for row in records(evidence["Bob"])[marker:]):
        raise RuntimeError("reconnect replayed the impact")
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
                  reconnect_generation=snapshots("Bob")[-1]["generation"],
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay",
                  relay=asdict(relay.stop()), manifest=manifest,
                  screenshots=[p.name for p in output.glob("*.png")])
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
