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
