"""Fresh V76 desktop Target/source/destination and committed ground acceptance.

Reuse the focused native fixtures and the existing impaired two-client runner.
This capture covers live consumers; rollback/restart remains focused fixture evidence.
"""

from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

from native_combat_families_capture import Evidence, record_id, sub, subrecords


def records(blob):
    offset = 0
    if len(blob) > 4 * 1024 * 1024:
        raise ValueError("Acceptance fixture exceeds input budget")
    while offset < len(blob):
        tag, size, unknown, flags = struct.unpack_from("<4sIII", blob, offset)
        offset += 16
        payload = blob[offset:offset + size]
        offset += size
        if len(payload) != size:
            raise ValueError("Truncated acceptance fixture")
        yield tag, unknown, flags, list(subrecords(payload))


def prepare_fixture(config, output, bootstrap, collision):
    from native_spell_presentation_capture import prepare_fixture as spell_fixture
    user, content = spell_fixture(config, output)
    collision = collision.resolve()
    obstruction = next(parts for tag, _, _, parts in records((collision / "NpcDoors.esp").read_bytes())
        if tag == b"MISC" and (b"NAME", b"committed_obstruction\0") in parts)
    # Preserve the focused box as explicit triangles for both stock desktop and
    # detached server Bullet loaders; use the established OSG serialization.
    procedural = (collision / "meshes/placement-obstruction.osgt").read_text()
    if "Center 0 0 70" not in procedural or "HalfLengths 80 50 70" not in procedural:
        raise ValueError("Focused obstruction geometry changed")
    corners = [(-80, -50, 0), (80, -50, 0), (80, 50, 0), (-80, 50, 0),
               (-80, -50, 140), (80, -50, 140), (80, 50, 140), (-80, 50, 140)]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    vertices = [corners[i] for a, b, c, d in faces for i in (a, b, c, a, c, d)]
    template = (output / "meshes/placement-floor.osgt").read_text()
    prefix, suffix = template.split("vector 4 {", 1)
    _, suffix = suffix.split("}", 1)
    mesh = prefix.replace("Mode QUADS", "Mode TRIANGLES").replace("Count 4", "Count 36")
    mesh += 'vector 36 {\n' + '\n'.join('        ' + ' '.join(map(str, vertex)) for vertex in vertices)
    mesh += '\n      }' + suffix
    (output / "meshes/placement-obstruction.osgt").write_text(mesh)
    result = bytearray()
    known = set()
    for tag, unknown, flags, parts in records((output / "NpcDoors.esp").read_bytes()):
        name = next((v.rstrip(b"\0").decode("ascii") for k, v in parts if k == b"NAME"), "")
        if tag == b"SPEL" and name.startswith("target_"):
            content["spells"].append(name)
            if name == "target_wide":
                effects = [v for k, v in parts if k == b"ENAM"]
                parts = [(k, v) for k, v in parts if k != b"ENAM"]
                parts += [(b"ENAM", effects[0])] * 31 + [(b"ENAM", effects[-1])]
        if tag == b"NPC_" and name in ("npc_hit_female", "npc_hit_beast"):
            parts.append((b"NPCO", struct.pack("<i32s", 7, b"committed_obstruction")))
            known.update(v.rstrip(b"\0").decode("ascii") for k, v in parts if k == b"NPCS")
        payload = b"".join(sub(k, v) for k, v in parts)
        result += struct.pack("<4sIII", tag, len(payload), unknown, flags) + payload
    payload = b"".join(sub(k, v) for k, v in obstruction)
    result += struct.pack("<4sIII", b"MISC", len(payload), 0, 0) + payload
    (output / "NpcDoors.esp").write_bytes(result)
    content["spells"] = sorted(set(content["spells"]))
    content["known_sources"] = len(known | set(content["spells"]))
    content["fixture_sha256"] = hashlib.sha256((output / "NpcDoors.esp").read_bytes()).hexdigest()
    content["obstruction_sha256"] = hashlib.sha256((output / "meshes/placement-obstruction.osgt").read_bytes()).hexdigest()
    bootstrap = bootstrap.resolve()
    env = os.environ.copy()
    deps = Path(__file__).resolve().parent.parent / "deps/installed/x64-windows/bin"
    env["PATH"] = os.pathsep.join((str(bootstrap.parent), str(deps), env.get("PATH", "")))
    with (output / "discovery.log").open("w") as log:
        discovered = subprocess.run([str(bootstrap), str(user), str(output / "discovered"),
            "NPC Door Contact Test", "npc_hit_female", "npc_hit_beast", "0", "0:-40:1"],
            env=env, stdout=log, stderr=subprocess.STDOUT)
    if discovered.returncode:
        raise RuntimeError("Native destination discovery failed; inspect content/discovery.log")
    descriptor = (output / "discovered/native.txt").read_text()
    expected = ['interior "npc door contact test"', 'interior "npc door recall test"', 'interior "npc door path test"']
    selectors = [line.lower() for line in descriptor.splitlines() if line.startswith("interior ")]
    if selectors != [expected[0], expected[2], expected[1]]:
        raise RuntimeError(f"Discovered cells differ from acceptance binding: {selectors}")
    # The capture's stable mapping follows discovery order, rather than authored room numbering.
    content["discovery"] = dict(cells=selectors, bootstrap_sha256=hashlib.sha256(bootstrap.read_bytes()).hexdigest())
    content["area_lines"] = 'areas 3\n' + '\n'.join(f'{selector}\ncell interior:{i}'
        for i, selector in enumerate(selectors[1:], 2)) + '\n'
    content["cell_maps"] = {i: selector.split('"')[1] for i, selector in enumerate(selectors, 1)}
    return user, content


def verify_capture(output, evidence, processes, relay, manifest, content, runtime):
    log = Evidence(evidence)
    sequence = dict.fromkeys(evidence, 0)
    checks, finished = [], set()

    def wait(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(description, flush=True)
                return value
            exited = [(r, p.poll()) for r, p in processes.items() if r not in finished and p.poll() is not None]
            if exited:
                raise RuntimeError(f"{description}: processes exited {exited}")
            time.sleep(.03)
        raise RuntimeError(f"Timed out: {description}")

    def latest(role, event="native_combat_sample"):
        rows = log.rows(role, event)
        return rows[-1] if rows else None

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        for attempt in range(100):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                if attempt == 99:
                    raise
                time.sleep(.01)
        return wait(lambda: next((r for r in log.rows(role, "traversal_" + action.split()[0])
                    if r.get("sequence") == sequence[role]), None), f"{role}: {action}")

    def save(check):
        checks.append(check)
        with (output / "checks.ndjson").open("a") as stream:
            stream.write(json.dumps(check) + "\n")

    def trace(role):
        path = output / (role + ".spell-presentation.ndjson")
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def cast(source, role="Alice", expected_target=None, ordinal=None, miss=False):
        player = 1 if role == "Alice" else 2
        wait(lambda: all(c["cast_phase"] == 0 for c in latest(role)["casts"] if c["id"] == player), "cast recovered")
        before = {r: len(log.rows(r, "native_combat_sample")) for r in evidence}
        command(role, f'cast "{source}"')
        def samples(r):
            return log.rows(r, "native_combat_sample")[before[r]:]
        def applications(r):
            return [c for row in samples(r) for c in row["spell_applications"]
                    if c["source"] == source and c["caster"] == player and c["kind"] == 1]
        wait(lambda: any(c["source"] == record_id(source) and c["caster"] == player and c["success"] and c["target_kind"] == 0
             for row in samples(role) for c in row["magic_events"]), source + ": authoritative release")
        if expected_target or ordinal is not None:
            wait(lambda: all(any(c["outcome"] == 0 and (ordinal is None or c["ordinal"] == ordinal)
                and (expected_target is None or (c["target_kind"], c["target"]) == expected_target)
                for c in applications(r)) for r in evidence), source + ": committed recipient/ordinal on both desktops")
        wait(lambda: not latest(role)["projectiles"], source + ": flight terminated")
        time.sleep(.6)
        outcomes = {r: applications(r) for r in evidence}
        if miss and any(any(c["target_kind"] in (1, 2) and c["outcome"] == 0 for c in values)
                        for values in outcomes.values()):
            raise RuntimeError(f"{source}: miss applied actor effects")
        if expected_target or miss:
            for r in evidence:
                launches = [c for row in samples(r) for c in row["magic_events"]
                            if c["source"] == record_id(source) and c["caster"] == player and c["target_kind"] == 0
                            and c["magicka_delta"] < 0]
                if len(launches) != 1 or launches[0]["target"] != 0:
                    raise RuntimeError(f"{source}/{r}: duplicate or bound release {launches}")
        for r, values in outcomes.items():
            keys = [(c["target_kind"], c["target"], c["ordinal"]) for c in values]
            if len(keys) != len(set(keys)):
                raise RuntimeError(f"{source}/{r}: duplicated application")
        check = dict(source=source, role=role, outcomes=outcomes,
            impacts={r: [c for row in samples(r) for c in row["magic_impacts"] if c["caster"] == player] for r in evidence})
        save(check)
        return check

    def pose(role, x, y, pitch=0, yaw=0):
        command(role, f"pose {x} {y} 1 {pitch} {yaw}")
        time.sleep(.65)  # Impaired spatial ingress must settle before authoritative facing is sampled.

    def probe(label, start, end, blocked):
        results = {}
        for role in evidence:
            command(role, "probe " + " ".join(map(str, (*start, *end))))
            result = latest(role, "traversal_collision_probe")
            if any(result[key] != blocked for key in ("physics_hit", "projectile_hit", "render_hit")):
                raise RuntimeError(f"{label}/{role}: collision consumers disagree {result}")
            if blocked and result["record"] != "committed_obstruction":
                raise RuntimeError(f"{label}/{role}: wrong hull {result}")
            results[role] = result
        save(dict(collision=label, blocked=blocked, probes=results))

    def reconnect(role, ground_count):
        inventory = latest(role, "traversal_inventory")["player"]
        magicka = latest(role)["magicka"]
        generation = latest(role)["generation"]
        marker = len(trace(role))
        command(role, "reconnect")
        wait(lambda: latest(role)["generation"] > generation, role + ": resumed fresh baseline")
        wait(lambda: latest(role, "traversal_inventory") and latest(role, "traversal_reconnect")
             and len(latest(role, "traversal_inventory")["player"]) > 0, role + ": current inventory restored")
        time.sleep(1)
        restored = command(role, "observe")
        if (len(restored["ground"]) != ground_count or latest(role, "traversal_inventory")["player"] != inventory
            or latest(role)["magicka"] != magicka or any(t["event"] != "loop-vfx" for t in trace(role)[marker:])):
            raise RuntimeError(role + ": reconnect changed current ground or replayed transient cues")
        save(dict(reconnect=role, ground_count=ground_count, transient_replay=False, inventory_preserved=True, magicka=magicka))

    def screenshot(role, label):
        command(role, "screenshot")
        shot = evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png")
        wait(lambda: shot.exists() and shot.read_bytes().endswith(b"\x00\x00\x00\x00IEND\xaeB`\x82"), "saved desktop screenshot")
        shot.rename(output / (role + "-" + label + ".png"))

    wait(lambda: all(latest(r) and latest(r, "actor_presentation_frame") and len(latest(r)["actors"]) >= 2
         for r in evidence), "two capability-35 V76 desktop baselines", 75)
    pose("Alice", -160, -400)
    pose("Bob", -160, -200)
    cast("target_recipient", expected_target=(1, 2), ordinal=2)
    pose("Bob", -600, -400)
    cast("target_recipient", miss=True)
    pose("Bob", -160, -200)
    area = cast("target_area", expected_target=(1, 2), ordinal=0)
    if not all(any(c["target_kind"] == 2 and c["outcome"] == 0 for c in values)
               for values in area["outcomes"].values()):
        raise RuntimeError("Impact-centered area omitted nearby actors")
    # Cast from a second release position: actor recipients must still follow the impact, not the caster.
    pose("Alice", -160, -900)
    distant_area = cast("target_area", expected_target=(1, 2), ordinal=0)
    if any(any(c["target_kind"] == 1 and c["target"] == 1 and c["outcome"] == 0 for c in values)
           for values in distant_area["outcomes"].values()):
        raise RuntimeError("Impact area incorrectly included distant release origin")
    pose("Bob", -600, -400)
    actors = latest("Alice")["actors"]
    for actor in (actors[0], actors[-1]):
        position = next(p for p in reversed(log.rows("Alice", "native_actor_pose")) if p["placement"] == actor["id"])
        pose("Alice", position["x"], position["y"] - 120)
        cast("target_recipient", expected_target=(2, actor["id"]), ordinal=2)
    # Ordinary door object recipient uses the same committed lock projection.
    pose("Alice", 0, -250)
    cast("target_object")
    wait(lambda: all(any(d["lock"] == 23 and d["rendered_locked"]
         for d in latest(r, "native_door_presented")["doors"]) for r in evidence),
         "unbound Target locked and rendered the committed door on both desktops")
    pose("Alice", -160, -400)
    # Full 32-effect source, area/duration/magnitude above former bounds, final ordinal 31.
    cast("target_wide", ordinal=31)
    if content["known_sources"] <= 64:
        raise RuntimeError("Fixture did not retain expanded known source set")
    reconnect("Bob", 0)
    wait(lambda: any(a["kind"] == 1 and a["id"] == 1 and content["effects"]["Shield"] in a["effects"]
                    for a in latest("Bob")["visual_loops"]), "extended source loop restored")

    # Place using the stock inventory/HUD drop delegate, then exercise shared world queries.
    pose("Alice", 60, -230, 1.2)
    pose("Bob", -160, -400)
    probe("before-drop", (-150, -230, 70), (250, -230, 70), False)
    command("Alice", 'drop "committed_obstruction"')
    wait(lambda: all(command(r, "observe")["ground_matches"] and len(latest(r, "traversal_observe")["ground"]) == 1
         for r in evidence), "committed drop visible on both desktops")
    ground = latest("Alice", "traversal_observe")["ground"][0]
    x, y, z = ground["position"]
    start, end = (x - 210, y, z + 70), (x + 210, y, z + 70)
    probe("after-drop", start, end, True)
    pose("Alice", x - 210, y, yaw=1.5707963)
    pose("Bob", x + 210, y)
    blocked = cast("target_recipient", miss=True)
    if not all(values and all(abs(c["position"][0] - x) <= 80.1 and abs(c["position"][1] - y) <= 50.1
        and z - .1 <= c["position"][2] <= z + 140.1 for c in values) for values in blocked["impacts"].values()):
        raise RuntimeError("Dropped hull omitted authoritative projectile contact")
    reconnect("Bob", 1)
    probe("reconnected-drop", start, end, True)
    cast("travel_mark")
    cast("accept_almsivi")
    wait(lambda: latest("Alice", "native_player_sample")["cell"] == 3, "Almsivi reached discovered destination")
    cast("travel_recall")
    wait(lambda: latest("Alice", "native_player_sample")["cell"] == 1, "Recall reloaded ground cell")
    probe("reloaded-drop", start, end, True)
    for role in evidence:
        screenshot(role, "dropped")
    pose("Alice", x - 120, y, .4, 1.5707963)
    command("Alice", 'activate "committed_obstruction"')
    wait(lambda: all(len(command(r, "observe")["ground"]) == 0 for r in evidence), "pickup removed rendered ground on both desktops")
    probe("after-pickup", start, end, False)
    for role in evidence:
        screenshot(role, "picked-up")
    pose("Alice", x - 210, y, yaw=1.5707963)
    pose("Bob", x + 210, y)
    cast("target_recipient", expected_target=(1, 2), ordinal=2)
    reconnect("Alice", 0)
    probe("reconnected-pickup", start, end, False)

    # Destinations supplied by owning native bootstrap, not a manually authored server list.
    cast("accept_divine")
    wait(lambda: latest("Alice", "native_player_sample")["cell"] == 1, "Divine reached discovered local marker")
    for role in evidence:
        screenshot(role, "acceptance")
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        if processes[role].returncode:
            raise RuntimeError(role + ": failed clean exit")
    report = dict(success=True, scenario="fresh V76/capability-35 desktop Target and committed collision",
        synthetic_placements=True, manifest=manifest, runtime=runtime, content=content, checks=checks,
        reconnect_transients_replayed=False, relay=asdict(relay.stop()),
        profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay",
        inherited_evidence=["build/spell-v74-acceptance-23/result.json", "build/logs/target-recipients-08.log",
                            "build/logs/world-collision-09.log", "build/logs/source-limits-02.log"])
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
