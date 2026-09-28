"""Two-desktop V56 movement capture using ordinary self and actor-target casts."""

from dataclasses import asdict
import json
import time


def verify_movement_capture(output, evidence, processes, relay, manifest):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()
    effect_ids = (0, 1, 2, 7, 8, 9, 10, 11)

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def effects(role, kind, identity):
        data = samples(role)
        if not data:
            return None
        return next((p["effects"] for p in data[-1].get("movement", [])
                     if p["kind"] == kind and p["id"] == identity and p["owned"]), None)

    def wait_for(predicate, label, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(label, flush=True)
                return value
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited: {label}")
            time.sleep(.05)
        raise RuntimeError(f"timed out: {label}")

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

    def cast(role, action):
        player = 1 if role == "Alice" else 2
        wait_for(lambda: samples(role) and all(p["cast_id"] == 0
                 for p in samples(role)[-1]["casts"] if p["id"] == player), f"{role} cast recovery")
        command(role, action)

    screenshots = []

    def screenshot(role, label):
        command(role, "screenshot")
        destination = output / f"{role}-{label}.png"
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(destination)
        screenshots.append(destination.name)

    wait_for(lambda: all(len(samples(role)) >= 3 and effects(role, 1, 1)
                         and effects(role, 1, 2) for role in evidence), "two movement baselines", 75)
    actors = {p["id"] for p in samples("Alice")[-1]["movement"] if p["kind"] == 2}
    if len(actors) != 1:
        raise RuntimeError("capture requires one bound NPC movement identity")
    npc = actors.pop()
    command("Alice", "thirdperson")
    command("Bob", "thirdperson")
    for role in evidence:
        screenshot(role, "baseline")

    npc_results = {}
    for slot, index in enumerate(effect_ids):
        cast("Bob", f'castactor "movement_npc_{index}"')
        expected = 1 if slot in (0, 2) else 100 if index == 7 else 20
        wait_for(lambda: all(effects(role, 2, npc) and effects(role, 2, npc)[slot] >= expected
                             for role in evidence), f"NPC movement {index} reaches both clients")
        npc_results[str(index)] = {role: effects(role, 2, npc) for role in evidence}
        if index == 7:
            for role in evidence:
                screenshot(role, "npc-burden")

    player_results = {}
    for slot, index in enumerate(effect_ids):
        cast("Alice", f'cast "movement_{index}"')
        expected = 1 if slot in (0, 2) else 20
        wait_for(lambda: all(effects(role, 1, 1) and effects(role, 1, 1)[slot] >= expected
                             for role in evidence), f"player movement {index} reaches both clients")
        player_results[str(index)] = {role: effects(role, 1, 1) for role in evidence}
        if index == 10:
            for role in evidence:
                screenshot(role, "player-levitate")

    motion = {role: [r for r in records(path) if r.get("event") == "native_actor_sample"
                     and r.get("placement") == npc] for role, path in evidence.items()}
    if any(len(value) < 2 for value in motion.values()):
        raise RuntimeError("a desktop did not receive NPC motion samples")
    common = {r["tick"]: r for r in motion["Alice"]}.keys() & {r["tick"]: r for r in motion["Bob"]}.keys()
    if not common:
        raise RuntimeError("desktops have no common authoritative NPC sample")
    for tick in common:
        left = next(r for r in motion["Alice"] if r["tick"] == tick)
        right = next(r for r in motion["Bob"] if r["tick"] == tick)
        if (left["x"], left["y"], left["z"]) != (right["x"], right["y"], right["z"]):
            raise RuntimeError("desktop NPC motion diverged at a committed tick")

    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="V56 movement on two OpenMW desktops",
                  synthetic_room=True, real_loadout=True, manifest=manifest, npc=npc,
                  player_effects=player_results, npc_effects=npc_results,
                  common_npc_ticks=len(common), motion_samples={role: len(value) for role, value in motion.items()},
                  screenshots=screenshots,
                  relay=asdict(relay.stop()))
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


def verify_deep_water_capture(output, evidence, processes, relay, manifest, restart_server, restart_client):
    """Two clients observe a content-water NPC across a real server process restart."""
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def npc_health(row):
        return row["actors"][0]["health"]

    def breathing(row):
        return next(p["effects"][0] for p in row["movement"] if p["kind"] == 2 and p["owned"])

    def wait_for(predicate, label, timeout=50):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(label, flush=True)
                return value
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited: {label}")
            time.sleep(.05)
        raise RuntimeError(f"timed out: {label}")

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

    wait_for(lambda: all(len(samples(role)) >= 3 and samples(role)[-1]["actors"] for role in evidence),
             "two deep-water baselines", 75)
    initial = {role: samples(role)[-1] for role in evidence}
    baseline = wait_for(lambda: next((r for r in samples("Alice")
        if npc_health(r) < npc_health(initial["Alice"]) - 1), None),
        "unprotected NPC drowns in content water")
    wait_for(lambda: samples("Alice") and samples("Alice")[-1]["tick"] >= 210,
             "deep-water NPC reaches a stable cast position")
    command("Bob", 'castactor "movement_npc_0"')
    active = wait_for(lambda: next((samples("Alice")[-1] for _ in (0,)
        if samples("Alice") and samples("Bob") and breathing(samples("Alice")[-1]) > 0
        and breathing(samples("Bob")[-1]) > 0), None),
        "WaterBreathing reaches both desktops")
    protected_health = npc_health(active)
    wait_for(lambda: next((r for r in samples("Alice")[-1:]
        if r["tick"] >= active["tick"] + 30 and breathing(r) > 0), None),
        "WaterBreathing remains active for one second")
    if npc_health(samples("Alice")[-1]) != protected_health:
        raise RuntimeError("NPC continued drowning with WaterBreathing")

    for name in ("server", "Alice", "Bob"):
        processes[name].terminate()
        processes[name].wait(timeout=15)
        finished.add(name)
    for role, path in evidence.items():
        path.rename(output / f"{role}-before.ndjson")
        path.with_suffix(".ndjson.control").unlink(missing_ok=True)
        sequence[role] = 0
    restart_server()
    for role in evidence:
        restart_client(role)
        finished.discard(role)
    wait_for(lambda: all(len(samples(role)) >= 3 and samples(role)[-1]["actors"] for role in evidence),
             "two desktops reconnect after server restart", 75)
    restored = {role: samples(role)[0] for role in evidence}
    if any(npc_health(row) != protected_health or breathing(row) <= 0 for row in restored.values()):
        raise RuntimeError("server process restart lost WaterBreathing or committed NPC health")
    expired = wait_for(lambda: next((r for r in samples("Alice")[-1:]
        if breathing(r) == 0 and npc_health(r) < protected_health), None),
        "NPC drowns after restored WaterBreathing expires", 90)
    def converged():
        alice = {r["tick"]: r for r in samples("Alice") if r["tick"] >= expired["tick"]}
        bob = {r["tick"]: r for r in samples("Bob") if r["tick"] >= expired["tick"]}
        return next((tick for tick in alice.keys() & bob.keys()
            if breathing(alice[tick]) == breathing(bob[tick]) == 0
            and npc_health(alice[tick]) == npc_health(bob[tick]) < protected_health), None)
    common_tick = wait_for(converged, "both desktops converge on post-expiry damage")
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="V56 deep-water content drowning and WaterBreathing",
                  synthetic_room=True, real_loadout=True, process_restart=True, manifest=manifest,
                  water_height=1000, initial_health=npc_health(initial["Alice"]),
                  unprotected_health=npc_health(baseline), protected_health=protected_health,
                  restored_health={role: npc_health(row) for role, row in restored.items()},
                  expired_tick=expired["tick"], common_post_expiry_tick=common_tick,
                  post_expiry_health=npc_health(expired),
                  relay=asdict(relay.stop()))
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
