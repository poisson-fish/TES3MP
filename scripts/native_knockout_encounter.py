"""Two rendered clients exercise server-owned fatigue/physical falls and authored get-up."""

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


def validate_physical_observations(segments, require_hit=True):
    """Require a living physical fall, advancing authored clock and later recovery per observer."""
    identities = {p["id"] for segment in segments for rows in segment.values()
                  for row in rows for p in row["actors"]}
    if len(identities) != 1:
        raise RuntimeError("physical capture requires one stable actor identity")
    actor_id, = identities
    progress = {role: 0 for role in ("Alice", "Bob")}
    first_frame = {}
    previous_pose = {}
    hits = {role: {} for role in progress}
    shared = 0
    for segment in segments:
        committed = {}
        for role, rows in segment.items():
            for row in rows:
                for hit in row["player_hits"]:
                    key = hit["attacker"], hit["attacker_revision"], hit["target_revision"]
                    if key in hits[role]:
                        raise RuntimeError("duplicated physical hit outcome")
                    hits[role][key] = hit
                for actor in row["actors"]:
                    pose = actor["knockout"]
                    if actor["dead"] or actor["fatigue"] < 0 or pose["paralyzed"] or pose["state"] not in (1, 3):
                        raise RuntimeError("physical proof contains death, fatigue knockout or invalid pose")
                    previous = previous_pose.get(role)
                    if previous and previous["state"] == pose["state"] == 3 and pose["frame"] < previous["frame"]:
                        raise RuntimeError("physical animation clock rewound")
                    previous_pose[role] = pose
                    value = pose, actor["health"], actor["fatigue"]
                    prior = committed.setdefault(row["tick"], {})
                    if prior and role not in prior:
                        shared += 1
                        if value != next(iter(prior.values())):
                            raise RuntimeError("observers disagree on a committed physical tick")
                    prior[role] = value
                    if pose["state"] == 3:
                        if not progress[role]:
                            first_frame[role] = pose["frame"]
                            progress[role] = 1
                        elif progress[role] == 1 and pose["frame"] > first_frame[role]:
                            progress[role] = 2
                    elif progress[role] == 2:
                        progress[role] = 3
    if any(value != 3 for value in progress.values()):
        raise RuntimeError("missing physical fall, authored progress or subsequent upright recovery")
    common = hits["Alice"].keys() & hits["Bob"].keys()
    if any(hits["Alice"][key] != hits["Bob"][key] for key in common):
        raise RuntimeError("observers disagree on physical hit outcome")
    successful = [key for key in common if (h := hits["Alice"][key])["target"] == actor_id
                  and h["hit"] and h["damage"] > 0 and h["stat"] == 0 and not h["died"]]
    if require_hit and not successful:
        raise RuntimeError("missing shared physical health damage")
    return dict(shared_ticks=shared, matching_health_hits=len(successful), recovery=progress)


def verify_knockout_encounter(output, evidence, processes, relay, manifest, restart_server, restart_client,
                             npc=False, physical=False, content=None):
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
    if physical:
        if not npc or not content or not content["profile"].startswith("vanilla-knockdown-"):
            raise RuntimeError("physical capture requires a vanilla-knockdown NPC fixture")

        def strike():
            health = {r: player(samples(r)[-1], identities[0])["health"] for r in evidence}
            command("Bob", "pose 140 -34 1 0.3 -1.57")
            command("Alice", "pose 60 -150 1 0.3 0")
            time.sleep(.5)
            for attempt in range(12):
                command("Alice", "swing 0 1")
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    if all_at(3, False):
                        if any(player(samples(r)[-1], identities[0])["health"] >= health[r] for r in evidence):
                            raise RuntimeError("physical knockdown did not reduce target health")
                        return
                    time.sleep(.025)
            raise RuntimeError("no shared physical knockdown from sword hits")

        def physical_recovery(label):
            # Capture the late authored clip separately from the upright commit.
            # Images must be inspected; frame progress alone is not rendered-body proof.
            for role in evidence:
                wait_for(lambda: (p := player(samples(role)[-1], identities[0]))["knockout"]["state"] == 3
                         and p["knockout"]["frame"] >= 55, role + ": late physical clip")
                screenshot(role, label + "-late")
            wait_for(lambda: all_at(1, False), "physical get-up completes on both observers")
            for role in evidence:
                screenshot(role, label + "-upright")

        strike()
        frame_subjects()
        for role in evidence:
            wait_for(lambda: (p := player(samples(role)[-1], identities[0]))["knockout"]["state"] == 3
                     and p["knockout"]["frame"] >= 32, role + ": physical fall reaches ground")
            screenshot(role, "physical-down")
        physical_recovery("physical")
        normal = {r: samples(r) for r in evidence}
        normal_validation = validate_physical_observations([normal])

        missed_reconnect_windows = 0
        for attempt in range(3):
            reconnect_start = {r: len(samples(r)) for r in evidence}
            strike()
            retained_health = {r: player(samples(r)[-1], identities[0])["health"] for r in evidence}
            generation = samples("Bob")[-1]["generation"]
            command("Bob", "reconnect")
            wait_for(lambda: samples("Bob")[-1]["generation"] > generation, "observer reconnects")
            if any(player(samples(r)[-1], identities[0])["health"] != retained_health[r] for r in evidence):
                raise RuntimeError("reconnect lost committed physical damage")
            if all_at(3, False) and all(player(samples(r)[-1], identities[0])["knockout"]["frame"] <= 60
                                       for r in evidence):
                break
            # A network reconnect can outlast the stock clip. Preserve that
            # attempt in the stream, but never count it as active-pose evidence.
            missed_reconnect_windows += 1
            frame_subjects()
            wait_for(lambda: all_at(1, False), "missed reconnect window finishes normally")
        else:
            raise RuntimeError("reconnect never returned within the physical clip capture window")
        reconnected = {r: samples(r)[-1] for r in evidence}
        frame_subjects()
        physical_recovery("physical-reconnect")
        # A hit event may arrive after disconnect and need not replay. The
        # rejoining baseline must retain its health loss and advancing body clock.
        reconnect_validation = validate_physical_observations([
            {r: samples(r)[reconnect_start[r]:] for r in evidence}], require_hit=False)

        strike()
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
        for role in evidence:
            restart_client(role)
            finished.remove(role)
        # Warm both renderers before authority resumes its short physical clip.
        wait_for(lambda: all(any(r.get("event") == "phase8_desktop_started" for r in records(path))
                             for path in evidence.values()), "both restarted desktops initialized", 15)
        restart_server()
        wait_for(lambda: all_at(3, False), "restart restores physical knockdown on both clients", 75)
        restored = {r: samples(r)[-1] for r in evidence}
        for role in evidence:
            old = player(before[role][-1], identities[0])
            new = player(restored[role], identities[0])
            if new["health"] != old["health"] or new["knockout"]["frame"] < old["knockout"]["frame"]:
                raise RuntimeError("restart lost physical damage or rewound the committed clip")
        frame_subjects()
        for role in evidence:
            screenshot(role, "physical-restored")
        physical_recovery("physical-restart")
        after = {r: samples(r) for r in evidence}
        validation = validate_physical_observations([before, after])
        validation["normal"] = normal_validation
        validation["reconnect"] = reconnect_validation
        validation["restored"] = validate_physical_observations([after], require_hit=False)
        for role in evidence:
            command(role, "quit")
            processes[role].wait(timeout=15)
            finished.add(role)
            if processes[role].returncode:
                raise RuntimeError(f"{role} did not finish cleanly")
        report = dict(success=True, scenario="V51 live NPC physical knockdown and get-up", manifest=manifest,
                      synthetic_actor_stats_and_placements=True, content=content, initial=initial,
                      missed_reconnect_windows=missed_reconnect_windows,
                      reconnected=reconnected, restored=restored, validation=validation,
                      final={r: after[r][-1] for r in evidence}, relay=asdict(relay.stop()), screenshots=captures)
        output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(dict(success=True, content=content, validation=validation)), flush=True)
        return
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
