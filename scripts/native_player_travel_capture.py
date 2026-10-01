"""Fresh V68 desktop Mark/Recall with unloaded scenes and impaired UDP."""

from dataclasses import asdict
import json
import math
import shutil
import struct
import time


CELLS = {1: "NPC Door Contact Test", 2: "NPC Door Recall Test", 3: "NPC Door Path Test"}


def prepare_fixture(config, output):
    """Copy the synthetic travel fixture and connect its three rooms with stock doors."""
    source = config.parent
    output.mkdir()
    shutil.copytree(source / "meshes", output / "meshes")
    blob = (source / "NpcDoors.esp").read_bytes()
    if len(blob) > 4 * 1024 * 1024:
        raise ValueError("Travel fixture exceeds bounded input")
    result = bytearray()
    offset = 0
    linked = set()
    targets = {CELLS[1]: CELLS[2], CELLS[2]: CELLS[3], CELLS[3]: CELLS[1]}

    def sub(name, value):
        return name + struct.pack("<I", len(value)) + value

    while offset < len(blob):
        tag, size, unknown, flags = struct.unpack_from("<4sIII", blob, offset)
        offset += 16
        payload = blob[offset:offset + size]
        if len(payload) != size:
            raise ValueError("Truncated travel fixture record")
        offset += size
        if tag == b"CELL":
            subs, index = [], 0
            while index < len(payload):
                name, length = struct.unpack_from("<4sI", payload, index)
                index += 8
                value = payload[index:index + length]
                if len(value) != length:
                    raise ValueError("Truncated travel fixture subrecord")
                index += length
                subs.append((name, value))
            cell = subs[0][1].rstrip(b"\0").decode("ascii")
            if cell in targets:
                # Keep the synthetic capture's scene geometry visible.
                subs = [(name, struct.pack("<I", 0x00b0b0b0) + value[4:]
                         if name == b"AMBI" and len(value) == 16 else value) for name, value in subs]
                payload = b"".join(sub(name, value) for name, value in subs)
                start = next(i - 1 for i, (name, value) in enumerate(subs)
                             if name == b"NAME" and value.rstrip(b"\0") == b"npc_door")
                end = next((i for i in range(start + 1, len(subs)) if subs[i][0] == b"FRMR"), len(subs))
                cloned = bytearray()
                for name, value in subs[start:end]:
                    if name == b"FRMR":
                        value = struct.pack("<I", 1000000 + list(targets).index(cell))
                    if name == b"DATA":
                        cloned += sub(b"DODT", struct.pack("<6f", 60, -250, 1, 0, 0, 0))
                        cloned += sub(b"DNAM", targets[cell].encode("ascii") + b"\0")
                        value = struct.pack("<6f", -300, 0, 0, 0, 0, 0)
                    cloned += sub(name, value)
                payload += cloned
                linked.add(cell)
        result += struct.pack("<4sIII", tag, len(payload), unknown, flags) + payload
    if linked != set(targets):
        raise ValueError(f"Travel fixture lacks required doors: {sorted(set(targets) - linked)}")
    (output / "NpcDoors.esp").write_bytes(result)
    user = output / "openmw"
    user.mkdir()
    text = (config / "openmw.cfg").read_text().replace(source.as_posix(), output.as_posix())
    (user / "openmw.cfg").write_text(text, encoding="utf-8")
    return user


def verify_player_travel_capture(output, evidence, processes, relay, manifest, runtime):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()
    phases = {}
    checks = []

    def rows(role, event):
        return [r for r in records(evidence[role]) if r.get("event") == event]

    def latest(role, event):
        values = rows(role, event)
        return values[-1] if values else None

    def wait_for(predicate, description, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(description, flush=True)
                return value
            exited = [(name, p.poll()) for name, p in processes.items()
                      if name not in finished and p.poll() is not None]
            if exited:
                raise RuntimeError(f"Process exited before {description}: {exited}")
            time.sleep(.05)
        raise RuntimeError(f"Timed out: {description}")

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
        return wait_for(lambda: next((r for r in records(evidence[role])
                        if r.get("sequence") == sequence[role]
                        and r.get("event") == "traversal_" + action.split()[0]), None),
                        f"{role}: {action}")

    def observe(role, cell):
        wait_for(lambda: latest(role, "native_player_sample")
                 and latest(role, "native_player_sample")["cell"] == cell,
                 f"{role} authority entered cell {cell}")
        def converged():
            value = command(role, "observe")
            if value["authority_cell"] == cell and value["baseline_cell"] == cell \
                    and CELLS[cell].lower() in value["cell"].lower():
                return value
            time.sleep(.2)
            return None
        return wait_for(converged, f"{role} scene/baseline/authority converged in cell {cell}")

    def screenshot(role, name):
        command(role, "screenshot")
        source = evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png")
        wait_for(lambda: source.exists() and source.read_bytes().endswith(
                 b"\x00\x00\x00\x00IEND\xaeB`\x82"), f"{role} saved {name}", 10)
        source.rename(output / f"{role}-{name}.png")

    def visit(role, cell):
        for _ in range(8):
            command(role, "pose -240 -75 1 0 0")
            time.sleep(.75)
            x, y, z = (v / 1024 for v in latest(role, "native_player_sample")["position"])
            # Aim from the settled canonical pose so a correction cannot turn
            # the setup ray away from the authored doorway between frames.
            yaw = math.atan2(-240 - x, -y)
            command(role, f"pose {x} {y} {z} 0 {yaw}")
            focus = command(role, "observe")
            if focus["focus"] == "npc_door":
                break
        else:
            raise RuntimeError(f"{role} teleport door not focused: {focus['focus']}")
        command(role, 'activate "npc_door"')
        return observe(role, cell)

    def combat(role):
        return latest(role, "native_combat_sample")

    def recovery(role):
        player = combat(role)["self"]
        wait_for(lambda: any(c["id"] == player and c["cast_phase"] == 0
                             for c in combat(role)["casts"]), f"{role} cast recovery completed")

    def cast(role, spell):
        recovery(role)
        before = combat(role)["magicka"]
        start = len(records(evidence[role]))
        command(role, f'cast "{spell}"')
        source = 14695981039346656037
        for byte in spell.encode("ascii"):
            source = ((source ^ byte) * 1099511628211) & 0xffffffffffffffff
        def paid():
            values = [r for r in records(evidence[role])[start:]
                      if r.get("event") == "native_combat_sample"]
            return next((r for r in values if any(e["caster"] == combat(role)["self"]
                         and e["caster_kind"] == 1 and e["source"] == source
                         and e["success"] for e in r["magic_events"])), None)
        committed = wait_for(paid, f"{role} committed {spell}")
        # Reliable results can accompany an older latest-wins snapshot.
        wait_for(lambda: combat(role)["magicka"] == before - 5,
                 f"{role} authoritative payment converged")
        recovery(role)
        checks.append(dict(role=role, spell=spell, before=before,
                           after=combat(role)["magicka"], tick=committed["tick"]))
        if combat(role)["magicka"] != before - 5:
            raise RuntimeError(f"{role} {spell} repeated payment during recovery")

    wait_for(lambda: all(combat(role) and latest(role, "native_player_sample") for role in evidence),
             "two fresh V68 desktops joined", 75)
    phases["initial"] = {role: observe(role, 1) for role in evidence}
    initial_magicka = {role: combat(role)["magicka"] for role in evidence}
    command("Alice", "pose -120 -200 1 0 0")
    time.sleep(.75)
    cast("Alice", "travel_mark")
    alice_mark = observe("Alice", 1)
    visit("Bob", 2)
    command("Bob", "pose 180 -300 1 0 0")
    time.sleep(.75)
    cast("Bob", "travel_mark")
    bob_mark = observe("Bob", 2)
    phases["marks"] = dict(Alice=alice_mark, Bob=bob_mark)
    screenshot("Alice", "mark")
    screenshot("Bob", "mark")
    visit("Bob", 3)
    visit("Alice", 2)
    visit("Alice", 3)
    phases["unloaded"] = {role: observe(role, 3) for role in evidence}
    for role, scene in phases["unloaded"].items():
        if any(CELLS[cell].lower() in active.lower() for cell in (1, 2) for active in scene["active_cells"]):
            raise RuntimeError(f"{role} Mark destination was still loaded")
    recall_starts = {}
    destinations = {"Alice": 1, "Bob": 2}
    for role in evidence:
        command(role, "pose 240 -350 1 0 0")
        # Pose is a same-cell setup move, not gameplay travel. Let its
        # collision correction and delayed position samples settle before
        # measuring Recall so setup motion is not mistaken for a teleport.
        def settled():
            samples = rows(role, "native_player_sample")[-6:]
            return len(samples) == 6 and len({tuple(r["position"]) for r in samples}) == 1
        time.sleep(1)
        wait_for(settled, f"{role} source pose settled before Recall")
        recall_starts[role] = len(records(evidence[role]))
        old_epoch = latest(role, "native_player_sample")["epoch"]
        cast(role, "travel_recall")
        scene = observe(role, destinations[role])
        if latest(role, "native_player_sample")["epoch"] <= old_epoch:
            raise RuntimeError(f"{role} Recall did not advance authority epoch")
        mark = phases["marks"][role]["position"]
        if abs(scene["position"][0] - mark[0]) > 2 or abs(scene["position"][1] - mark[1]) > 2:
            raise RuntimeError(f"{role} Recall used the wrong Mark")
        screenshot(role, "recalled")
    phases["recalled"] = {role: observe(role, destinations[role]) for role in evidence}
    # Several relay delay windows after each Recall must not restore an old cell/epoch.
    time.sleep(2)
    for role in evidence:
        samples = [r for r in records(evidence[role])[recall_starts[role]:]
                   if r.get("event") == "native_player_sample"]
        edge = next(i for i, r in enumerate(samples) if r["cell"] == destinations[role])
        if any(r["cell"] != destinations[role] or r["epoch"] != samples[edge]["epoch"]
               or any(abs(r["position"][axis] - samples[edge]["position"][axis]) > 2 * 1024
                      for axis in (0, 1)) for r in samples[edge:]):
            raise RuntimeError(f"{role} stale movement snapped back after Recall")
    before = {role: dict(location=latest(role, "native_player_sample"), magicka=combat(role)["magicka"])
              for role in evidence}
    generation = combat("Bob")["generation"]
    marker = len(records(evidence["Bob"]))
    command("Bob", "reconnect")
    wait_for(lambda: combat("Bob")["generation"] > generation, "Bob resumed with a new generation")
    phases["resumed"] = observe("Bob", 2)
    time.sleep(2)
    resumed = [r for r in records(evidence["Bob"])[marker:]
               if r.get("event") == "native_combat_sample" and r["generation"] > generation]
    if not resumed or any(r["magic_events"] or r["magic_casts"] or r["magic_impacts"]
                          or any(c["cast_phase"] != 0 for c in r["casts"]) for r in resumed):
        raise RuntimeError("Bob reconnect replayed cast or impact cues")
    for role in evidence:
        now = latest(role, "native_player_sample")
        if (now["cell"], now["epoch"], now["position"]) != tuple(
                before[role]["location"][key] for key in ("cell", "epoch", "position")):
            raise RuntimeError(f"{role} reconnect changed current location")
        if combat(role)["magicka"] != initial_magicka[role] - 10:
            raise RuntimeError(f"{role} reconnect changed paid resources")
    screenshot("Bob", "resumed")
    # Reunite through the authored door and compare both players on both replicas.
    visit("Alice", 2)
    wait_for(lambda: all({p["id"] for p in latest(role, "native_player_sample")["players"]} == {1, 2}
                         for role in evidence), "both clients track both players after reunion")
    peers = {role: latest(role, "native_player_sample")["players"] for role in evidence}
    if any(p["cell"] != 2 for values in peers.values() for p in values):
        raise RuntimeError("Reunion peer locations diverged")
    def peers_converged():
        actual = {combat(role)["self"]: latest(role, "native_player_sample") for role in evidence}
        for role in evidence:
            for p in latest(role, "native_player_sample")["players"]:
                expected = actual[p["id"]]
                if p["epoch"] != expected["epoch"] or any(
                        abs(p["position"][axis] - expected["position"][axis]) > 2 * 1024 for axis in (0, 1)):
                    return False
        return True
    wait_for(peers_converged, "both replicas agree on player epochs and positions")
    peers = {role: latest(role, "native_player_sample")["players"] for role in evidence}
    phases["reunited"] = {role: observe(role, 2) for role in evidence}
    for role in evidence:
        player = combat(role)["self"]
        events = [e for r in rows(role, "native_combat_sample") for e in r["magic_events"]
                  if e["caster_kind"] == 1 and e["caster"] == player and e["success"]]
        cues = [c for r in rows(role, "native_combat_sample") for c in r["magic_casts"]
                if c["kind"] == 1 and c["caster"] == player]
        if len(events) != 2 or len(cues) != 2 or len({c["command"] for c in cues}) != 2:
            raise RuntimeError(f"{role} duplicated or lost its Mark/Recall results or cues")
    for role in evidence:
        command(role, "facepeer")
        screenshot(role, "reunited")
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not exit cleanly")
        finished.add(role)
    report = dict(success=True, scenario="fresh V68 two-desktop cross-cell Mark/Recall",
                  manifest=manifest, runtime=runtime, phases=phases, payment=checks, peers=peers,
                  relay=asdict(relay.stop()), screenshots=[p.name for p in output.glob("*.png")],
                  presentation="Screenshots saved; visual inspection required. Audio unverified.",
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay")
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("success", "scenario", "payment", "relay")}, indent=2), flush=True)
