"""Two-desktop V73 equipment, summon and actor-life acceptance via real input capture."""

from dataclasses import asdict
import json
import shutil
import struct
import time


SUMMONS = (*range(102, 117), 134, *range(137, 143))


def record_id(name):
    value = 14695981039346656037
    for byte in name.lower().encode("ascii"):
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def subrecords(payload):
    offset = 0
    while offset < len(payload):
        tag, length = struct.unpack_from("<4sI", payload, offset)
        offset += 8
        value = payload[offset:offset + length]
        if len(value) != length:
            raise ValueError("Truncated acceptance fixture subrecord")
        offset += length
        yield tag, value


def sub(tag, value):
    return tag + struct.pack("<I", len(value)) + value


def prepare_fixture(config, output):
    source = config.parent
    output.mkdir()
    shutil.copytree(source / "meshes", output / "meshes")
    blob = (source / "NpcDoors.esp").read_bytes()
    if len(blob) > 4 * 1024 * 1024:
        raise ValueError("Acceptance fixture exceeds bounded input")
    spells = [f"accept_summon_{effect}" for effect in SUMMONS]
    result, offset = bytearray(), 0
    while offset < len(blob):
        tag, length, unknown, flags = struct.unpack_from("<4sIII", blob, offset)
        offset += 16
        payload = blob[offset:offset + length]
        offset += length
        if len(payload) != length:
            raise ValueError("Truncated acceptance fixture record")
        parts = list(subrecords(payload))
        name = next((v.rstrip(b"\0").decode("ascii") for k, v in parts if k == b"NAME"), "")
        if tag == b"NPC_" and name in ("npc_hit_female", "npc_hit_beast"):
            # Keep the source fixture's ordinary player stats, inventory and known spells.
            payload += b"".join(sub(b"NPCS", f"accept_summon_{effect}".encode().ljust(32, b"\0")) for effect in SUMMONS)
        if tag == b"SPEL":
            spells_in_fixture = name
            # Discover existing sources rather than inventing a parallel gameplay catalog.
            if name == "summon_lifecycle" or name.startswith(("travel_bound_", "expanded_", "life_")):
                if spells_in_fixture not in spells:
                    spells.append(spells_in_fixture)
        result += struct.pack("<4sIII", tag, len(payload), unknown, flags) + payload
    for effect in SUMMONS:
        name = f"accept_summon_{effect}"
        payload = sub(b"NAME", name.encode() + b"\0") + sub(b"FNAM", name.encode() + b"\0")
        payload += sub(b"SPDT", struct.pack("<iii", 0, 1, 4))  # ordinary spell, Always succeeds
        payload += sub(b"ENAM", struct.pack("<hbbiiiii", effect, -1, -1, 0, 0, 12 if effect == 139 else 4, 1, 1))
        result += struct.pack("<4sIII", b"SPEL", len(payload), 0, 0) + payload
    (output / "NpcDoors.esp").write_bytes(result)
    user = output / "openmw"
    user.mkdir()
    (user / "openmw.cfg").write_text((config / "openmw.cfg").read_text().replace(
        source.as_posix(), output.as_posix()), encoding="utf-8")
    return user, dict(spells=sorted(set(spells)), selectors=list(SUMMONS))


class Evidence:
    """Read each flushed event once; long grouped captures must not rescan render traces."""
    def __init__(self, paths):
        self.paths, self.offsets, self.events = paths, dict.fromkeys(paths, 0), {r: {} for r in paths}

    def rows(self, role, event):
        path = self.paths[role]
        if path.exists():
            with path.open("rb") as stream:
                stream.seek(self.offsets[role])
                data = stream.read()
            complete = data.rfind(b"\n") + 1
            for line in data[:complete].splitlines():
                row = json.loads(line)
                self.events[role].setdefault(row.get("event"), []).append(row)
            self.offsets[role] += complete
        return self.events[role].get(event, [])


def verify_combat_families(output, evidence, processes, relay, manifest, content, runtime):
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

    def cast(name, role="Alice", target=None):
        player = 1 if role == "Alice" else 2
        life = next(p["life"] for p in latest(role, "actor_presentation_frame")["actors"]
                    if p["kind"] == 1 and p["id"] == player)
        def released(before):
            return any(c["success"] and c["caster"] == player
                and c["caster_life"] == life
                and c["source"] == record_id(name)
                for row in log.rows(role, "native_combat_sample")[before:]
                for c in row["magic_events"])
        for attempt in range(3):
            wait(lambda: latest(role) and all(p["cast_phase"] == 0 for p in latest(role)["casts"]
                 if p["id"] == player), f"{role}: previous cast recovered")
            before = len(log.rows(role, "native_combat_sample"))
            command(role, f'castat "{name}" {target[0]} {target[1]}' if target else f'cast "{name}"')
            deadline = time.monotonic() + 3
            admitted = False
            while time.monotonic() < deadline:
                if released(before):
                    print(f"{name}: committed cast", flush=True)
                    return
                admitted |= any(p["id"] == player and p["cast_phase"] != 0
                    for row in log.rows(role, "native_combat_sample")[before:] for p in row["casts"])
                time.sleep(.03)
            if admitted:
                wait(lambda: released(before), f"{name}: committed cast")
                return
            print(f"{name}: no admitted cast; retry {attempt + 1}", flush=True)
        raise RuntimeError(f"{name}: no cast admitted after bounded retries")

    def bodies(role):
        return latest(role)["actors"]

    def kill(kind, identity, role="Alice"):
        def dead():
            return all(any(p["kind"] == kind and p["id"] == identity and p["dead"]
                for p in latest(r, "actor_presentation_frame")["actors"]) for r in evidence)
        for attempt in range(3):
            cast("life_kill", role=role, target=(kind, identity))
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                if dead():
                    print(f"death committed: {kind}:{identity}", flush=True)
                    return
                time.sleep(.03)
            # Always succeeds governs casting, not stock target resistance.
            print(f"{kind}:{identity}: survived stock resistance; hit {attempt + 1}", flush=True)
        raise RuntimeError(f"{kind}:{identity}: survived bounded death fixture")

    wait(lambda: all(latest(r) and latest(r, "traversal_inventory")
        and len(latest(r)["players"]) == 1 for r in evidence), "two established desktops")
    baseline = {r: {a["id"] for a in bodies(r)} for r in evidence}
    for role, x in (("Alice", 60), ("Bob", -160)):
        command(role, f"pose {x} -400 1 0 0")

    # Death recipes continue through the same render cursor for native NPC, creature and self/peer.
    npc = bodies("Alice")[0]["id"]
    position = next(p for p in reversed(log.rows("Alice", "native_actor_pose")) if p["placement"] == npc)
    command("Alice", f"pose {position['x']} {position['y'] - 65} {position['z']} 0 0")
    kill(2, npc)
    command("Alice", "pose 60 -400 1 0 0")
    # Stock summons disappear when killed. Use the fixture's persistent creature
    # to observe its complete committed death animation on both desktops.
    creature = bodies("Alice")[-1]["id"]
    position = next(p for p in reversed(log.rows("Alice", "native_actor_pose")) if p["placement"] == creature)
    command("Alice", f"pose {position['x']} {position['y'] - 65} {position['z']} 0 0")
    kill(2, creature)
    command("Alice", "pose 60 -400 1 0 0")
    command("Bob", "pose 60 -465 1 0 0")
    wait(lambda: all(any(p["id"] == 1 and abs(p["position"][1] / 1024 + 400) < 3
                        for p in latest(r, "native_player_sample")["players"])
                     for r in evidence), "player death subject position committed on both desktops")
    kill(1, 1, role="Bob")
    command("Alice", "reconnect")
    wait(lambda: all(any(p["kind"] == 1 and p["id"] == 1 and p["life"] == 2 and not p["dead"]
                       for p in latest(r, "actor_presentation_frame")["actors"]) for r in evidence), "life-2 respawn on both desktops")
    for role, x in (("Alice", 60), ("Bob", -160)):
        command(role, f"pose {x} -400 1 0 0")

    # Check ownership/expiry through the ordinary authoritative inventory projection.
    initial = {i["stack"] for i in latest("Alice", "traversal_inventory")["player"]}
    for name in ([f"travel_bound_{i}" for i in range(11)] if content.get("equipment", True) else []):
        cast(name)
        added = wait(lambda: {i["stack"] for i in latest("Alice", "traversal_inventory")["player"]}
                     - initial, f"{name}: temporary inventory identity")
        wait(lambda: not added.intersection(i["stack"] for i in latest("Alice", "traversal_inventory")["player"]),
             f"{name}: stock expiry removed temporary items")
        checks.append(dict(source=name, temporary=sorted(added), expired=True))
    if content.get("equipment", True):
        before_extra = latest("Alice", "traversal_inventory")["player"]
        cast("travel_bound_extra")
        time.sleep(4)
        if latest("Alice", "traversal_inventory")["player"] != before_extra:
            raise RuntimeError("Stock ExtraSpell changed player inventory")
        checks.append(dict(source="travel_bound_extra", stock_player_no_change=True))

    # One source at a time proves all 22 selectors, including stock empty placeholders.
    for effect in content["selectors"]:
        if effect not in (141, 142):
            cast(f"accept_summon_{effect}")
            ids = wait(lambda: {a["id"] for a in bodies("Alice")} - baseline["Alice"],
                       f"summon {effect}: authoritative body")
            wait(lambda: ids <= {a["id"] for a in bodies("Bob")}, f"summon {effect}: peer membership")
            wait(lambda: all(any(p["kind"] == 2 and p["id"] in ids
                         for frame in log.rows(r, "actor_presentation_frame") for p in frame["actors"])
                         for r in evidence), f"summon {effect}: both rendered bodies")
            if effect == 139:
                identity = next(iter(ids))
                position = next(p for p in reversed(log.rows("Alice", "native_actor_pose")) if p["placement"] == identity)
                command("Alice", f"pose {position['x']} {position['y'] - 65} {position['z']} 0 0")
                before = len(log.rows("Alice", "native_combat_sample"))
                maximum = next(a["health"] for a in bodies("Alice") if a["id"] == identity)
                cast("expanded_damage", target=(2, identity))
                damaged = wait(lambda: next((a["health"] for row in log.rows("Alice", "native_combat_sample")[before:]
                    for a in row["actors"] if a["id"] == identity and a["health"] < maximum - 2), None), "stock Bear took damage")
                wait(lambda: any(a["id"] == identity and a["health"] > damaged + 1 for a in bodies("Alice")),
                     "stock Bear passive regenerated health")
                command("Alice", "pose 60 -400 1 0 0")
            wait(lambda: all(not ids.intersection(a["id"] for a in bodies(r)) for r in evidence),
                 f"summon {effect}: shared expiry")
            checks.append(dict(effect=effect, actors=sorted(ids), rendered=True, expired=True))
        else:
            # Stock OpenMW drops these two invalid MGEFs when loading spells.
            if any({a["id"] for a in bodies(r)} != baseline[r] for r in evidence):
                raise RuntimeError("Stock empty summon placeholder created a body")
            checks.append(dict(effect=effect, stock_placeholder=True))

    for role in evidence:
        frames = log.rows(role, "actor_presentation_frame")
        for kind, identity in ((2, npc), (2, creature), (1, 1)):
            death = [p for frame in frames for p in frame["actors"]
                     if p["kind"] == kind and p["id"] == identity and p["dead"] and p["group"].startswith(("death", "swimdeath"))]
            if len(death) < 5 or not any(abs(p["frame"] - round(p["frame"])) > .001 for p in death):
                raise RuntimeError(f"{role}: missing fractional committed death for {kind}:{identity}")
        command(role, "screenshot")
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role}: unclean exit")
        finished.add(role)
    report = dict(success=True, scenario="V73 grouped equipment/summons/actor lives", manifest=manifest,
                  runtime=runtime, content=content, checks=checks, death_subjects=[npc, creature, 1], relay=asdict(relay.stop()))
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS grouped equipment, summons and committed actor lives", flush=True)
