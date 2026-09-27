"""Two rendered clients exercise server-owned exhaustion and authored get-up."""

from dataclasses import asdict
import json
import time


def subjects(row, npc=False):
    if npc:
        return row["actors"]
    return [dict(id=row["self"], fatigue=row["fatigue"], knockout=row["knockout"]), *row["players"]]


def validate_observations(segments, npc=False):
    """Require both observers to see ordered exhaustion, get-up and recovery."""
    identities = (1, 2)
    if npc:
        observed = {p["id"] for segment in segments for rows in segment.values()
                    for row in rows for p in subjects(row, True)}
        if len(observed) != 1:
            raise RuntimeError("NPC capture requires one stable actor identity")
        identities = tuple(observed)
    phases = {(role, player): set() for role in ("Alice", "Bob") for player in identities}
    progress = dict.fromkeys(phases, 0)
    shared = 0
    for segment in segments:
        committed = {}
        for role, rows in segment.items():
            for row in rows:
                for player in subjects(row, npc):
                    if player.get("dead", False):
                        raise RuntimeError("dead actor cannot prove knockout recovery")
                    key = (row["tick"], player["id"])
                    pose = player["knockout"]
                    value = (pose, player["fatigue"])
                    prior = committed.setdefault(key, {})
                    if prior and role not in prior:
                        shared += 1
                        if value != next(iter(prior.values())):
                            raise RuntimeError("observers disagree on a committed knockout tick")
                    prior[role] = value
                    phase = "down" if pose["state"] == 2 and player["fatigue"] < 0 else (
                        "getup" if pose["state"] == 2 and player["fatigue"] >= 0 else (
                            "upright" if pose["state"] == 1 and player["fatigue"] >= 0 else "other"))
                    identity = role, player["id"]
                    phases[identity].add(phase)
                    if progress[identity] < 3 and phase == ("down", "getup", "upright")[progress[identity]]:
                        progress[identity] += 1
    # Per-session latest-wins publication may have entirely disjoint ticks.
    # Compare any overlaps, but require the full ordered recovery on every observer.
    if any(v != {"down", "getup", "upright"} for v in phases.values()) or any(v != 3 for v in progress.values()):
        raise RuntimeError("missing shared knockout/get-up observations")
    return dict(shared_ticks=shared, phases={f"{r}:{p}": sorted(v) for (r, p), v in phases.items()})


def verify_knockout_encounter(output, evidence, processes, relay, manifest, restart_server, restart_client, npc=False):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()
    captures = {}

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def player(row, identity):
        return next(p for p in subjects(row, npc) if p["id"] == identity)

    def wait_for(predicate, description, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(description, flush=True)
                return value
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited: {description}")
            time.sleep(.025)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        temporary.replace(control)
        wait_for(lambda: any(r.get("sequence") == sequence[role]
                            and r.get("event") == "traversal_" + action.split()[0]
                            for r in records(evidence[role])), f"{role}: {action}")

    def screenshot(role, label):
        before = samples(role)[-1]
        command(role, "screenshot")
        name = f"{role}-{label}.png"
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(output / name)
        captures[name] = dict(before=before, after=samples(role)[-1])

    identities = (1, 2)

    def all_at(state, fatigue_negative=None):
        rows = [samples(r) for r in evidence]
        return all(rows) and all(len(subjects(r[-1], npc)) == len(identities) for r in rows) and all(
            (p := player(rows[i][-1], identity))["knockout"]["state"] == state
            and (fatigue_negative is None or (p["fatigue"] < 0) == fatigue_negative)
            for i in range(2) for identity in identities)

    def frame_subjects():
        for role in evidence:
            if npc:
                command(role, "pose 60 -300 1 0.3 0" if role == "Alice" else "pose -180 -140 1 0.3 1.15")
            else:
                command(role, "facepeer")

    def exhaust():
        if npc:
            command("Alice", "pose 60 -130 1 0.25 0")
            # Let the impaired movement updates install the touch-range setup.
            time.sleep(.5)
            command("Alice", 'castactor "expanded_knockout_touch"')
        else:
            for role in evidence:
                command(role, 'cast "expanded_knockout"')
        wait_for(lambda: all_at(2, True), "subjects exhausted on both clients")
        if npc:
            # Frame the whole body and leave touch range before AI resumes on get-up.
            frame_subjects()

    def recover(label):
        pending = dict.fromkeys(evidence, identities[0]) if npc else {"Alice": 2, "Bob": 1}
        def ready():
            # The reconnecting player's effect pauses while offline. Capture
            # whichever peer reaches the tail first, rather than waiting in role order.
            candidates = [(p["knockout"]["frame"], role) for role, identity in pending.items()
                          if (p := player(samples(role)[-1], identity))["knockout"]["state"] == 2
                          and p["fatigue"] >= 0 and p["knockout"]["frame"] >= 70]
            return max(candidates)[1] if candidates else None
        while pending:
            role = wait_for(ready, "peer authored get-up after fatigue returns")
            screenshot(role, label + "-getup")
            del pending[role]
        wait_for(lambda: all_at(1, False), "subjects finish get-up on both clients")
        for role in evidence:
            screenshot(role, label + "-upright")

    wait_for(lambda: all(len(samples(r)) >= 3 and len(samples(r)[-1]["players"]) == 1 for r in evidence),
             "two V51 desktop baselines", 75)
    if npc:
        actors = samples("Alice")[-1]["actors"]
        if len(actors) != 1:
            raise RuntimeError("NPC capture requires one actor")
        identities = (actors[0]["id"],)
    frame_subjects()
    initial = {r: samples(r)[-1] for r in evidence}
    exhaust()
    for role, identity in (dict.fromkeys(evidence, identities[0]) if npc else {"Alice": 2, "Bob": 1}).items():
        wait_for(lambda: player(samples(role)[-1], identity)["knockout"]["frame"] >= 32,
                 f"{role}: peer reaches exhausted loop")
        screenshot(role, "exhausted")
    generation = samples("Bob")[-1]["generation"]
    command("Bob", "reconnect")
    wait_for(lambda: samples("Bob")[-1]["generation"] > generation and all_at(2, True),
             "reconnect samples existing exhaustion")
    frame_subjects()
    recover("reconnect")

    exhaust()
    processes["server"].terminate()
    processes["server"].wait(timeout=15)
    finished.add("server")
    for role in evidence:
        processes[role].terminate()
        processes[role].wait(timeout=15)
        finished.add(role)
    before = {r: samples(r) for r in evidence}
    for role, path in evidence.items():
        path.rename(output / f"{role}-before.ndjson")
        path.with_suffix(".ndjson.control").unlink(missing_ok=True)
        sequence[role] = 0
    restart_server()
    for role in evidence:
        restart_client(role)
        finished.remove(role)
    wait_for(lambda: all_at(2, True), "restart restores active exhaustion on both clients", 75)
    restored = {r: samples(r)[-1] for r in evidence}
    frame_subjects()
    for role in evidence:
        screenshot(role, "restored")
    recover("restart")
    after = {r: samples(r) for r in evidence}
    validation = validate_observations([before, after], npc)
    # A recovery before the restart cannot substitute for a missing restored tail.
    validation["segments"] = [validate_observations([segment], npc) for segment in (before, after)]
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
    report = dict(success=True, scenario="V51 live " + ("NPC" if npc else "player") + " fatigue knockout and get-up", manifest=manifest,
                  synthetic_actor_placements_and_spell=True, initial=initial, restored=restored,
                  final={r: after[r][-1] for r in evidence}, validation=validation,
                  relay=asdict(relay.stop()), screenshots=captures)
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
