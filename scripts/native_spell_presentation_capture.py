"""Bounded V74 spell outcomes and shared stock presentation on two desktops."""

from collections import Counter
from dataclasses import asdict
import json
from pathlib import Path
import re
import struct
import subprocess
import time

from native_combat_families_capture import Evidence, record_id, sub, subrecords


AI = ("CalmHumanoid", "CalmCreature", "FrenzyHumanoid", "FrenzyCreature",
      "DemoralizeHumanoid", "DemoralizeCreature", "RallyHumanoid", "RallyCreature",
      "TurnUndead", "CommandCreature", "CommandHumanoid")


def prepare_fixture(config, output):
    from native_player_travel_capture import prepare_fixture as travel_fixture
    user = travel_fixture(config, output)
    engine = Path(__file__).resolve().parent.parent / "components/esm3/loadmgef.cpp"
    table = engine.read_text().split("sMagicEffectIds", 1)[1].split("};", 1)[0]
    ids = {name: index for index, name in enumerate(re.findall(r"MagicEffect::(\w+)", table))}
    sources = {}

    def effect(name, range=0, duration=2, magnitude=1, area=0):
        return struct.pack("<hbbiiiii", ids[name], -1, -1, range, area, duration, magnitude, magnitude)

    def spell(name, effects, type=0):
        sources[name] = (type, effects)

    for name in AI:
        spell("accept_" + name.lower(), [effect(name, 1, 2, 100)])
    spell("accept_fire", [effect("FireDamage", 1)])
    spell("accept_zero", [effect("FireDamage", 1, 1, 0)])
    spell("accept_touch_miss", [effect("FireDamage", 1), effect("Shield", 0, 2, 3)])
    spell("accept_resist", [effect("ResistFire", 0, 5, 100)])
    spell("accept_reflect", [effect("Reflect", 0, 5, 100)])
    spell("accept_absorb", [effect("SpellAbsorption", 0, 5, 100)])
    spell("accept_silence", [effect("Silence", 0, 4, 0)])
    spell("accept_shield", [effect("Shield", 0, 8, 5)])
    spell("accept_target", [effect("FireDamage", 2, 2, 1, 8)])
    spell("accept_actor_noop", [effect("Lock", 1, 0, 10), effect("Mark", 1, 0, 0), effect("Shield", 1, 2, 7)])
    spell("accept_telekinesis", [effect("Telekinesis", 0, 3, 10)])
    spell("accept_soultrap", [effect("Soultrap", 1, 4, 0)])
    spell("accept_soul_kill", [effect("Soultrap", 1, 30, 0), effect("DamageHealth", 1, 0, 200)])
    spell("accept_divine", [effect("DivineIntervention", 0, 0, 0)])
    spell("accept_almsivi", [effect("AlmsiviIntervention", 0, 0, 0)])
    spell("accept_passive", [effect("DrainAttribute", 0, 0, 3), effect("Blind", 0, 0, 2)], 1)
    # DrainAttribute requires a concrete attribute (Strength).
    sources["accept_passive"][1][0] = struct.pack("<hbbiiiii", ids["DrainAttribute"], -1, 0, 0, 0, 0, 3, 3)
    blob = (output / "NpcDoors.esp").read_bytes()
    result, offset, existing = bytearray(), 0, []
    while offset < len(blob):
        tag, size, unknown, flags = struct.unpack_from("<4sIII", blob, offset)
        offset += 16
        payload = blob[offset:offset + size]
        if len(payload) != size:
            raise ValueError("Truncated spell acceptance fixture")
        offset += size
        parts = list(subrecords(payload))
        name = next((v.rstrip(b"\0").decode("ascii") for k, v in parts if k == b"NAME"), "")
        if tag == b"SPEL":
            existing.append(name)
        if tag == b"NPC_" and name in ("npc_hit_female", "npc_hit_beast", "npc_door_actor", "npc_witness_alarm"):
            revised = []
            for key, value in parts:
                if key == b"NPDT" and len(value) == 52:
                    value = bytearray(value)
                    value[4] = value[9] = 0  # Deterministic target resistance: Willpower/Luck.
                    value = bytes(value)
                revised.append(sub(key, value))
            payload = b"".join(revised)
            if name.startswith("npc_hit_"):
                payload += b"".join(sub(b"NPCS", s.encode().ljust(32, b"\0")) for s in sources)
                payload += sub(b"NPCO", struct.pack("<i32s", 3, b"misc_soulgem_petty"))
                payload += sub(b"NPCO", struct.pack("<i32s", 1, b"misc_soulgem_common"))
        if tag == b"CREA" and name == "neighbor_test_creature":
            revised = []
            for key, value in parts:
                if key == b"NPDT" and len(value) == 96:
                    values = list(struct.unpack("<24i", value))
                    values[0] = 2  # Existing bound creature becomes an undead subject for TurnUndead.
                    values[4] = values[9] = 0
                    value = struct.pack("<24i", *values)
                revised.append(sub(key, value))
            payload = b"".join(revised)
        if tag == b"CELL" and name in ("NPC Door Contact Test", "NPC Door Recall Test"):
            marker = "divinemarker" if name == "NPC Door Contact Test" else "templemarker"
            payload += sub(b"FRMR", struct.pack("<I", 2000000 + (marker == "templemarker")))
            payload += sub(b"NAME", marker.encode() + b"\0")
            payload += sub(b"DATA", struct.pack("<6f", -100, -120, 1, 0, 0, 0))
        result += struct.pack("<4sIII", tag, len(payload), unknown, flags) + payload
    for name, (type, effects) in sources.items():
        payload = sub(b"NAME", name.encode() + b"\0") + sub(b"FNAM", name.encode() + b"\0")
        payload += sub(b"SPDT", struct.pack("<iii", type, 0, 4))
        payload += b"".join(sub(b"ENAM", value) for value in effects)
        result += struct.pack("<4sIII", b"SPEL", len(payload), 0, 0) + payload
    for name, (protection, source) in {
            "plain": (None, "accept_fire"), "zero": (None, "accept_zero"),
            "ignored": (None, "accept_actor_noop"),
            "resist": ("ResistFire", "accept_fire"), "reflect": ("Reflect", "accept_fire"),
            "absorb": ("SpellAbsorption", "accept_fire")}.items():
        if protection:
            ability = "accept_stock_" + name
            payload = sub(b"NAME", ability.encode() + b"\0") + sub(b"SPDT", struct.pack("<iii", 1, 0, 0))
            payload += sub(b"ENAM", effect(protection, 0, 0, 100))
            result += struct.pack("<4sIII", b"SPEL", len(payload), 0, 0) + payload
    (output / "NpcDoors.esp").write_bytes(result)
    required = {"travel_power", "travel_mixed_mark", "travel_recall_mark", "travel_soul_kill", "travel_mark", "travel_recall"}
    if not required.issubset(existing):
        raise ValueError("Spell presentation needs the current completeness fixture")
    return user, dict(spells=sorted(required | set(sources)), effects=ids)


def verify_capture(output, evidence, processes, relay, manifest, content, runtime, binary, environment):
    log, sequence, finished, checks = Evidence(evidence), dict.fromkeys(evidence, 0), set(), []

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

    def trace(role):
        path = output / (role + ".spell-presentation.ndjson")
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def cast(name, role="Alice", target=None, success=True, expected=None, predicted=False):
        player = 1 if role == "Alice" else 2
        for attempt in range(3):
            wait(lambda: all(c["cast_phase"] == 0 for c in latest(role)["casts"] if c["id"] == player), "cast recovered")
            before = {r: len(log.rows(r, "native_combat_sample")) for r in evidence}
            visuals = {r: len(trace(r)) for r in evidence}
            windup_roles = [r for r in evidence if latest(r, "native_player_sample")["cell"]
                            == latest(role, "native_player_sample")["cell"]]
            action = f'{"castpredict" if predicted else "castat"} "{name}" {target[0]} {target[1]}' if target else f'cast "{name}"'
            command(role, action)

            def released():
                return next((c for row in log.rows(role, "native_combat_sample")[before[role]:]
                    for c in row["magic_events"] if c["caster"] == player and c["caster_kind"] == 1
                    and c["source"] == record_id(name)), None)
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline and not released():
                time.sleep(.03)
            event = released()
            if event is None:
                continue
            if event["success"] != success:
                raise RuntimeError(f"{name}: unexpected launch result {event}")

            def outcomes(r):
                return [c for row in log.rows(r, "native_combat_sample")[before[r]:]
                        for c in row["spell_applications"] if c["source"] == name and c["caster"] == player]
            outcome_roles = [r for r in evidence if latest(r, "native_player_sample")["cell"]
                             == latest(role, "native_player_sample")["cell"]]
            wait(lambda: all(any(c["kind"] == (1 if success else 2) for c in outcomes(r)) for r in outcome_roles),
                 f"{name}: committed outcomes on interested desktops")
            if expected is not None:
                wait(lambda: all(any(c["kind"] == 1 and c["outcome"] == expected and
                    (not target or (c["target_kind"], c["target"]) == target) for c in outcomes(r)) for r in outcome_roles),
                    f"{name}: stock outcome {expected}")
            time.sleep(.5)
            presented = {r: trace(r)[visuals[r]:] for r in evidence}
            for r in evidence:
                casting = [t for t in presented[r] if t["event"] == "cast-vfx"]
                if r in windup_roles and (not casting or max(Counter(t["resource"] for t in casting).values()) != 1):
                    raise RuntimeError(f"{name}: {r} missing or duplicated stock cast models: {casting}")
                if any(not t["attached"] for t in casting):
                    raise RuntimeError(f"{name}: {r} detached stock cast models")
                if not success and r in outcome_roles and not any(t["event"] == "failure-sound" and t["attached"] for t in presented[r]):
                    raise RuntimeError(f"{name}: {r} omitted stock failure audio")
                if expected in (1, 4) and any(t["event"].startswith("hit-") for t in presented[r]):
                    raise RuntimeError(f"{name}: {r} presented a suppressed hit")
                if any(t["event"].startswith("hit-") and not t["attached"] for t in presented[r]):
                    raise RuntimeError(f"{name}: {r} detached an actor hit")
            checks.append(dict(source=name, role=role, success=success, predicted=predicted,
                               outcomes={r: outcomes(r) for r in evidence}, presentation=presented))
            with (output / "checks.ndjson").open("a", encoding="utf-8") as recorded:
                recorded.write(json.dumps(checks[-1]) + "\n")
            return checks[-1]
        raise RuntimeError(f"{name}: no admitted cast after bounded retries")

    def reconnect():
        generation = latest("Bob")["generation"]
        marker = len(trace("Bob"))
        command("Bob", "reconnect")
        wait(lambda: latest("Bob")["generation"] > generation, "Bob reconnected")
        time.sleep(1)
        if any(t["event"] != "loop-vfx" for t in trace("Bob")[marker:]):
            raise RuntimeError("Reconnect replayed transient spell presentation")

    wait(lambda: all(latest(r) and latest(r, "actor_presentation_frame") and len(latest(r)["actors"]) >= 2
         for r in evidence), "two V74 desktop baselines", 75)
    for r, y in (("Alice", -400), ("Bob", -465)):
        command(r, f"pose 60 {y} 1 0 0")
        command(r, "thirdperson")
    time.sleep(1)
    cast("travel_power")
    cast("accept_touch_miss")
    cast("accept_zero", target=(1, 2), expected=0)
    cast("accept_fire", target=(1, 2), expected=0, predicted=True)
    cast("accept_actor_noop", target=(1, 2))
    cast("accept_telekinesis")
    wait(lambda: all(any(p["kind"] == 1 and p["id"] == 1 and p["telekinesis"] == 10
                         for p in latest(r)["visibility"]) for r in evidence), "Telekinesis reached both replicas")
    # Stock accepts Silence before the Always flag. The failed attempt pays no second hit.
    cast("accept_silence")
    cast("accept_shield", success=False)
    time.sleep(4)
    for protection, outcome in (("resist", 1), ("reflect", 2), ("absorb", 3)):
        cast("accept_" + protection, role="Bob")
        cast("accept_fire", target=(1, 2), expected=outcome)
        time.sleep(5)
    command("Bob", "pose 60 -600 1 0 0")
    cast("accept_target", target=(1, 2), expected=0)
    command("Bob", "pose 60 -465 1 0 0")
    # Renew the existing bounded resume ticket before the longer AI/travel group.
    reconnect()

    actors = latest("Alice")["actors"]
    npc, creature = actors[0]["id"], actors[-1]["id"]
    for name in AI:
        subject = creature if name.endswith("Creature") or name == "TurnUndead" else npc
        position = next(p for p in reversed(log.rows("Alice", "native_actor_pose")) if p["placement"] == subject)
        command("Alice", f"pose {position['x']} {position['y'] - 60} {position['z']} 0 0")
        cast("accept_" + name.lower(), target=(2, subject), expected=0)
        time.sleep(2)
    position = next(p for p in reversed(log.rows("Alice", "native_actor_pose")) if p["placement"] == creature)
    command("Alice", f"pose {position['x']} {position['y'] - 60} {position['z']} 0 0")
    cast("accept_calmhumanoid", target=(2, creature), expected=5)
    cast("accept_soultrap", target=(2, creature), expected=0)
    souls_before = sum(i["count"] for i in latest("Alice", "traversal_inventory")["player"] if i["soul"])
    cast("accept_soul_kill", target=(2, creature))
    wait(lambda: any(p["id"] == creature and p["dead"] for p in latest("Alice")["actors"]),
         "Soultrap subject committed death")
    wait(lambda: sum(i["count"] for i in latest("Alice", "traversal_inventory")["player"] if i["soul"]) > souls_before,
         "Soultrap filled the caster's committed gem")
    cast("accept_divine")
    cast("travel_mixed_mark")
    cast("accept_almsivi")
    wait(lambda: latest("Alice", "native_player_sample")["cell"] == 2, "Almsivi entered bound temple cell")
    cast("travel_recall_mark")
    wait(lambda: latest("Alice", "native_player_sample")["cell"] == 1, "mixed Recall/Mark returned to Divine marker")

    # Restore a live stock loop without replaying transient cast/hit/failure resources.
    cast("accept_shield")
    reconnect()
    wait(lambda: any(a["kind"] == 1 and a["id"] == 1 and content["effects"]["Shield"] in a["effects"]
                    for a in latest("Bob")["visual_loops"]), "reconnect restored Alice's live shield loop")
    wait(lambda: all(not any(content["effects"]["Shield"] in a["effects"] for a in latest(r)["visual_loops"])
                    for r in evidence), "shield loops expired on both desktops")
    for role in evidence:
        command(role, "screenshot")
        shot = evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png")
        wait(lambda: shot.exists() and shot.read_bytes().endswith(b"\x00\x00\x00\x00IEND\xaeB`\x82"), "saved screenshot")
        shot.rename(output / (role + "-presentation.png"))
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        if processes[role].returncode:
            raise RuntimeError(f"{role}: failed clean exit")
    comparison = compare_stock(output, binary, environment, checks)
    report = dict(success=True, scenario="fresh V74 committed spell presentation", manifest=manifest, runtime=runtime,
                  checks=checks, reconnect_transients_replayed=False, relay=asdict(relay.stop()),
                  stock_comparison=comparison,
                  audio="Stock sound resource dispatch verified; waveform/listening comparison remains separate.")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")


def compare_stock(output, binary, environment, checks):
    """Use the in-scene stock CastSpell resolver and active-spell update.

    The test driver supplies an explicit target without script Cast's reflection bypass.
    Native failure audio is checked separately.
    """
    comparisons = []
    for name, source, outcome in (("plain", "accept_fire", 0), ("zero", "accept_zero", 0),
            ("ignored", "accept_actor_noop", None), ("resist", "accept_fire", 1),
            ("reflect", "accept_fire", 2), ("absorb", "accept_fire", 3)):
        user = output / ("stock-" + name)
        user.mkdir()
        (user / "openmw.cfg").write_text((output / "content/openmw/openmw.cfg").read_text())
        (user / "settings.cfg").write_text("[Video]\nresolution x = 1000\nresolution y = 700\nwindow mode = 2\nframerate limit = 30\nminimize on focus loss = false\n")
        startup = user / "startup.txt"
        startup.write_text('StopScript CharGen\nset CharGenState to -1\nEnablePlayerControls\n'
            'npc_door_actor->SetWillpower 0\nnpc_door_actor->SetLuck 0\n'
            'npc_witness_alarm->Position 60 -90 1 0\n'
            + (f'npc_door_actor->AddSpell "accept_stock_{name}"\n' if name in ("resist", "reflect", "absorb") else ""))
        path = user / "presentation.ndjson"
        env = environment.copy()
        env["TES3MP_SPELL_PRESENTATION_TRACE"] = str(path)
        env["TES3MP_STOCK_SPELL_PRESENTATION_CAST"] = source
        with (user / "stdout.log").open("wb") as stdout, (user / "stderr.log").open("wb") as stderr:
            process = subprocess.Popen([str(binary / "openmw.exe"), f"--config={user}", "--replace=config",
                f"--user-data={user}", f"--resources={binary / 'resources'}", "--start=NPC Door Contact Test",
                "--skip-menu=1", "--new-game=0", "--no-grab=1", "--no-sound=0", "--script-console=1",
                f"--script-run={startup}"], cwd=binary, env=env, stdout=stdout, stderr=stderr,
                creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                deadline = time.monotonic() + 35
                while time.monotonic() < deadline:
                    rows = [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []
                    if any(t["event"] == "cast-vfx" for t in rows):
                        release_deadline = time.monotonic() + 8
                        expected_event = "reflect-vfx" if name == "reflect" else "absorb-vfx" if name == "absorb" else "hit-sound"
                        while time.monotonic() < release_deadline:
                            rows = [json.loads(line) for line in path.read_text().splitlines()]
                            if name != "resist" and any(t["event"] == expected_event for t in rows):
                                time.sleep(.3)
                                break
                            time.sleep(.03)
                        break
                    if process.poll() is not None:
                        raise RuntimeError(f"Stock {name} exited: inspect {user}")
                    time.sleep(.05)
                else:
                    raise RuntimeError(f"Stock {name} did not cast: inspect {user}")
                rows = [json.loads(line) for line in path.read_text().splitlines()]
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=15)
        native = next((c for c in checks if c["source"] == source and
            (outcome is None or any(a["kind"] == 1 and a["outcome"] == outcome for a in c["outcomes"]["Alice"]))), None)
        if native is None:
            raise RuntimeError(f"Missing native stock comparison {name}")
        def resources(values):
            return Counter((t["event"], t["resource"].lower(), t["attached"]) for t in values
                           if t["event"] not in ("loop-vfx",))
        stock = resources(rows)
        for role in ("Alice", "Bob"):
            actual = resources(native["presentation"][role])
            if actual != stock:
                raise RuntimeError(f"Stock {name}/{role} mismatch: stock={stock}, native={actual}")
        comparisons.append(dict(scenario=name, resources=[dict(event=k[0], resource=k[1], attached=k[2], count=v)
            for k, v in stock.items()], stock_trace=str(path)))
        print(f"stock {name}: cast/hit/protection VFX and sound resources matched both desktops", flush=True)
    return comparisons
