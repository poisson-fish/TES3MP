"""Two rendered clients exercise server-owned fatigue/physical falls and authored get-up."""

from dataclasses import asdict
import json
import time


def subjects(row, npc=False):
    if npc:
        return row["actors"]
    return [dict(id=row["self"], **{key: row[key] for key in ("fatigue", "knockout", "health", "dead")
                                 if key in row}), *row["players"]]


def established_samples(rows):
    """Exclude only startup before the first two-player baseline; retain later losses."""
    first = next((i for i, row in enumerate(rows)
                  if {p["id"] for p in subjects(row)} == {1, 2}), len(rows))
    return rows[first:]


def validate_actor_melee(segments):
    outcomes = {role: {} for role in ("Alice", "Bob")}
    for segment in segments:
        for role, rows in segment.items():
            for row in rows:
                for hit in row["actor_hits"]:
                    key = hit["attacker"], hit["attacker_revision"], hit["target_revision"]
                    if hit["attacker"] not in {a["id"] for a in row["actors"]} or key in outcomes[role]:
                        raise RuntimeError("foreign or duplicated actor melee outcome")
                    outcomes[role][key] = hit
    common = outcomes["Alice"].keys() & outcomes["Bob"].keys()
    if not common or any(outcomes["Alice"][k] != outcomes["Bob"][k] for k in common):
        raise RuntimeError("actor melee outcomes disagree between desktops")
    if not any(outcomes["Alice"][k]["hit"] and outcomes["Alice"][k]["damage"] > 0 for k in common):
        raise RuntimeError("no shared actor melee damage")
    return dict(shared_outcomes=len(common), unique_outcomes={r: len(v) for r, v in outcomes.items()})


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


def validate_physical_observations(segments, require_hit=True, player_id=None):
    """Require a living physical fall, advancing authored clock and later recovery per observer."""
    def targets(row):
        return row["actors"] if player_id is None else [p for p in subjects(row) if p["id"] == player_id]

    identities = {p["id"] for segment in segments for rows in segment.values()
                  for row in rows for p in targets(row)}
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
                for hit in row["player_hits" if player_id is None else "actor_hits"]:
                    key = hit["attacker"], hit["attacker_revision"], hit["target_revision"]
                    if key in hits[role]:
                        raise RuntimeError("duplicated physical hit outcome")
                    hits[role][key] = hit
                if len(targets(row)) != 1:
                    raise RuntimeError("physical capture lost its target")
                for actor in targets(row):
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


def validate_retarget_observations(segment):
    """A shared NPC contact with Bob must occur inside Alice's advancing fall clock.

    Reliable events can arrive alongside an older/newer latest-wins snapshot, so
    use their authoritative revision and bracket it with observed down ticks.
    Misses count as selection/contact evidence, never as health damage.
    """
    recovery = validate_physical_observations([segment], require_hit=False, player_id=1)
    contacts = {}
    for role in ("Alice", "Bob"):
        rows = segment[role]
        windows = []
        active = False
        actor_ids = {actor["id"] for row in rows for actor in row["actors"]}
        if len(actor_ids) != 1:
            raise RuntimeError("retarget capture requires one stable NPC")
        for row in rows:
            pose = next(p for p in subjects(row) if p["id"] == 1)["knockout"]
            if pose["state"] == 3:
                if not active:
                    # Offline clocks pause, so only observed ticks establish
                    # overlap; do not infer a start from tick minus frame.
                    windows.append([row["tick"], row["tick"]])
                windows[-1][1] = row["tick"]
                active = True
            else:
                active = False
        contacts[role] = {(hit["attacker"], hit["attacker_revision"], hit["target_revision"]): hit
                          for row in rows for hit in row["actor_hits"]
                          if hit["target"] == 2 and hit["attacker"] in actor_ids
                          and any(start <= hit["attacker_revision"] <= end for start, end in windows)}
    common = contacts["Alice"].keys() & contacts["Bob"].keys()
    if not common:
        raise RuntimeError("missing shared retarget contact during player recovery")
    return dict(recovery=recovery, contacts=[contacts["Alice"][key] for key in sorted(common)])


def verify_knockout_encounter(output, evidence, processes, relay, manifest, restart_server, restart_client,
                             npc=False, physical=False, content=None, retarget=False, runtime=None, actor_melee=False):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()
    captures = {}
    sample_cache = {}

    def samples(role):
        path = evidence[role]
        if not path.exists():
            return []
        stat = path.stat()
        identity, offset, rows = sample_cache.get(role, (None, 0, []))
        if identity != stat.st_ino or stat.st_size < offset:
            offset, rows = 0, []
        with path.open("rb") as stream:
            stream.seek(offset)
            data = stream.read()
        complete = data.rfind(b"\n") + 1
        for line in data[:complete].splitlines():
            row = json.loads(line)
            if row.get("event") == "native_combat_sample":
                rows.append(row)
        sample_cache[role] = stat.st_ino, offset + complete, rows
        return established_samples(rows)

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
        for attempt in range(100):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                # The Windows desktop briefly holds this file while reading it.
                # Retry the same sequence; never submit a second gameplay command.
                if attempt == 99:
                    raise
                time.sleep(.01)
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
        return all(rows) and all(set(identities).issubset(p["id"] for p in subjects(r[-1], npc)) for r in rows) and all(
            (p := player(rows[i][-1], identity))["knockout"]["state"] == state
            and (fatigue_negative is None or (p["fatigue"] < 0) == fatigue_negative)
            for i in range(2) for identity in identities)

    def frame_subjects():
        revisions = {r: max((row["revision"] for row in records(evidence[r])
                             if row.get("event") == "native_player_sample"), default=0) for r in evidence}
        for role in evidence:
            if physical and not npc:
                command(role, "pose 60 -300 1 0.45 0" if role == "Alice" else (
                    "pose 140 -34 1 0.3 -2.15" if retarget else "pose -180 -300 1 0.3 1.57"))
                if role == "Alice":
                    command(role, "thirdperson")
            elif npc:
                command(role, "pose 60 -300 1 0.3 0" if role == "Alice" else "pose -180 -140 1 0.3 1.15")
            else:
                command(role, "facepeer")
        if physical and not npc:
            # Control acknowledgement is local. Wait for the actual impaired
            # movement publication before disconnecting or saving the setup.
            for role, position in (("Alice", [61440, -307200, 1024]),
                                   ("Bob", [143360, -34816, 1024] if retarget else [-184320, -307200, 1024])):
                wait_for(lambda: (rows := [r for r in records(evidence[role])
                                          if r.get("event") == "native_player_sample"])
                         and rows[-1]["revision"] > revisions[role]
                         and sum((a - b) ** 2 for a, b in zip(rows[-1]["position"], position))
                         <= ((32 * 1024) ** 2 if retarget and role == "Bob" else 0),
                         role + ": authority retains camera position")

    def exhaust():
        if npc:
            caster = "Bob" if actor_melee else "Alice"
            if actor_melee:
                # Keep the sword user's hit recovery on the nearer participant;
                # the second player supplies the touch spell from the side.
                command("Alice", "pose 60 -100 1 0.3 0")
                start = {r: len(samples(r)) for r in evidence}
                wait_for(lambda: all(any(hit["target"] == 1 for row in samples(r)[start[r]:]
                                        for hit in row["actor_hits"]) for r in evidence),
                         "authority targets the nearer participant before the caster approaches")
            command(caster, "pose 140 -34 1 0.25 -1.57" if actor_melee else "pose 60 -130 1 0.25 0")
            position = [143360, -34816, 1024] if actor_melee else [61440, -133120, 1024]
            # A local pose acknowledgement does not prove that impaired movement
            # has committed. Sending a touch spell earlier can correctly miss.
            wait_for(lambda: (rows := [r for r in records(evidence[caster])
                                      if r.get("event") == "native_player_sample"])
                     and rows[-1]["position"] == position, "caster reaches authoritative touch range")
            command(caster, 'castactor "expanded_knockout_touch"')
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
    melee_evidence = {}

    def capture_actor_melee(label):
        if not actor_melee:
            return
        start = {r: len(samples(r)) for r in evidence}
        command("Bob", "pose -180 -140 1 0.3 1.15")
        command("Alice", "pose 60 -100 1 0.3 0")
        def hit_both():
            return all(any(hit["hit"] and hit["damage"] > 0 for row in samples(r)[start[r]:]
                           for hit in row["actor_hits"]) for r in evidence)
        wait_for(hit_both, "actor authoritative melee reaches both observers")
        for role in evidence:
            screenshot(role, label + "-melee")
        frame_subjects()
        melee_evidence[label] = {r: [hit for row in samples(r)[start[r]:] for hit in row["actor_hits"]]
                                 for r in evidence}
        for role, hits in melee_evidence[label].items():
            keys = [(h["attacker"], h["attacker_revision"], h["target_revision"]) for h in hits]
            if len(keys) != len(set(keys)):
                raise RuntimeError(role + ": duplicated actor hit")

    frame_subjects()
    capture_actor_melee("initial")
    initial = {r: samples(r)[-1] for r in evidence}
    if physical:
        if not content or not content["profile"].startswith("vanilla-knockdown-"):
            raise RuntimeError("physical capture requires a vanilla-knockdown fixture")
        if not npc:
            identities = (1,)

        def validate(segments, require_hit=True):
            return validate_physical_observations(segments, require_hit, None if npc else identities[0])

        def strike():
            health = {r: player(samples(r)[-1], identities[0])["health"] for r in evidence}
            # Alice steps closer than the high-agility observer to become the
            # next target. Recovery framing then exercises selection after retreat.
            command("Bob", "pose 140 -34 1 0.3 -1.57" if npc else "pose 140 -34 1 0.3 -2.85")
            command("Alice", "pose 60 -150 1 0.3 0" if npc else "pose 60 -95 1 0.3 0")
            time.sleep(.5)
            for attempt in range(12):
                if npc:
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
            pending = set(evidence)
            while pending:
                def ready():
                    candidates = [(p["knockout"]["frame"], role) for role in pending
                                  if (p := player(samples(role)[-1], identities[0]))["knockout"]["state"] == 3
                                  and p["knockout"]["frame"] >= (55 if npc else 65)]
                    return max(candidates)[1] if candidates else None
                role = wait_for(ready, "late physical clip")
                screenshot(role, label + "-late")
                pending.remove(role)
            wait_for(lambda: all_at(1, False), "physical get-up completes on both observers")
            for role in evidence:
                screenshot(role, label + "-upright")

        strike()
        frame_subjects()
        for role in evidence:
            wait_for(lambda: (p := player(samples(role)[-1], identities[0]))["knockout"]["state"] == 3
                     and p["knockout"]["frame"] >= 32, role + ": physical fall reaches ground")
            screenshot(role, "physical-down")
        if retarget:
            for role in evidence:
                wait_for(lambda: any(hit["target"] == 2 for row in samples(role)[-5:] for hit in row["actor_hits"])
                         and player(samples(role)[-1], 1)["knockout"]["state"] == 3,
                         role + ": NPC attacks Bob during Alice recovery")
                screenshot(role, "physical-combat")
        physical_recovery("physical")
        normal = {r: samples(r) for r in evidence}
        normal_validation = validate([normal])
        retarget_validation = {}
        if retarget:
            retarget_validation["normal"] = validate_retarget_observations(normal)
            # Leave no reachable target for longer than the old 128-tick lock
            # reproduction, then return for the reconnect encounter below.
            command("Bob", "pose -180 -300 1 0.3 1.57")
            time.sleep(.7)
            start = {r: samples(r)[-1] for r in evidence}
            wait_for(lambda: all(samples(r)[-1]["tick"] >= start[r]["tick"] + 128 for r in evidence),
                     "both players remain out of reach for 128 committed ticks")
            for role in evidence:
                if any(p["health"] != next(q for q in subjects(start[role]) if q["id"] == p["id"])["health"]
                       for p in subjects(samples(role)[-1])):
                    raise RuntimeError("retreated players took melee damage")
            retarget_validation["both_retreat"] = dict(start=start, end={r: samples(r)[-1] for r in evidence})

        missed_reconnect_windows = 0
        for attempt in range(3):
            reconnect_start = {r: len(samples(r)) for r in evidence}
            strike()
            if not npc:
                frame_subjects()
            retained_health = {r: player(samples(r)[-1], identities[0])["health"] for r in evidence}
            reconnecting = "Bob" if npc else "Alice"
            generation = samples(reconnecting)[-1]["generation"]
            command(reconnecting, "reconnect")
            wait_for(lambda: samples(reconnecting)[-1]["generation"] > generation, "physical participant reconnects")
            if any(player(samples(r)[-1], identities[0])["health"] != retained_health[r] for r in evidence):
                raise RuntimeError("reconnect lost committed physical damage")
            if all_at(3, False):
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
        reconnect_validation = validate([
            {r: samples(r)[reconnect_start[r]:] for r in evidence}], require_hit=False)
        if retarget:
            retarget_validation["reconnect"] = validate_retarget_observations(
                {r: samples(r)[reconnect_start[r]:] for r in evidence})

        strike()
        if not npc:
            frame_subjects()
        processes["server"].terminate()
        processes["server"].wait(timeout=15)
        finished.add("server")
        # Drain already-published impaired packets before shutting the observers
        # down, so their last sample can witness the durable stopping point.
        time.sleep(.7)
        for role in evidence:
            processes[role].terminate()
            processes[role].wait(timeout=15)
            finished.add(role)
        before = {r: samples(r) for r in evidence}
        for role, path in evidence.items():
            path.rename(output / f"{role}-before.ndjson")
            path.with_suffix(".ndjson.control").unlink(missing_ok=True)
            sequence[role] = 0
        if retarget:
            # Restore the observer first. Alice's offline recovery clock stays
            # paused while Bob joins, so slow startup cannot consume her tail
            # before his renderer is ready. Bob still receives live NPC attacks.
            restart_client("Bob")
            finished.remove("Bob")
            restart_server()
            wait_for(lambda: any(r.get("event") == "native_combat_sample" for r in records(evidence["Bob"])),
                     "restarted observer receives authority", 75)
            restart_client("Alice")
            finished.remove("Alice")
        else:
            for role in evidence:
                restart_client(role)
                finished.remove(role)
            # Warm both renderers before authority resumes its short physical clip.
            wait_for(lambda: all(any(r.get("event") == "phase8_desktop_started" for r in records(path))
                                 for path in evidence.values()), "both restarted desktops initialized", 15)
            restart_server()
        wait_for(lambda: all_at(3, False), "restart restores physical knockdown on both clients", 75)
        restored = {r: samples(r)[-1] for r in evidence}
        latest_before = max((before[r][-1] for r in evidence), key=lambda row: row["tick"])
        for role in evidence:
            old = player(latest_before, identities[0])
            new = player(restored[role], identities[0])
            if new["health"] != old["health"] or new["knockout"]["frame"] < old["knockout"]["frame"]:
                raise RuntimeError("restart lost physical damage or rewound the committed clip")
        frame_subjects()
        for role in evidence:
            screenshot(role, "physical-restored")
        physical_recovery("physical-restart")
        if retarget:
            command("Bob", "pose -180 -300 1 0.3 1.57")
            wait_for(lambda: (rows := [r for r in records(evidence["Bob"])
                                      if r.get("event") == "native_player_sample"])
                     and rows[-1]["position"] == [-184320, -307200, 1024],
                     "authority retains final retreat")
            settled_tick = max(samples(r)[-1]["tick"] for r in evidence) + 30
            wait_for(lambda: all(samples(r)[-1]["tick"] >= settled_tick for r in evidence),
                     "final combat publications settle")
            states = [{p["id"]: (p["health"], p["dead"], p["knockout"])
                       for p in subjects(samples(r)[-1])} for r in evidence]
            if states[0] != states[1]:
                raise RuntimeError("final player health/pose failed to converge")
            retarget_validation["final_players"] = states[0]
        capture_actor_melee("restored")
        after = {r: samples(r) for r in evidence}
        validation = validate([before, after])
        if actor_melee:
            validation["actor_melee"] = validate_actor_melee([before, after])
        validation["normal"] = normal_validation
        validation["reconnect"] = reconnect_validation
        validation["restored"] = validate([after], require_hit=False)
        if retarget:
            retarget_validation["restored"] = validate_retarget_observations(after)
            validation["retarget"] = retarget_validation
        for role in evidence:
            command(role, "quit")
            processes[role].wait(timeout=15)
            finished.add(role)
            if processes[role].returncode:
                raise RuntimeError(f"{role} did not finish cleanly")
        report = dict(success=True, scenario="V51 live " + ("NPC" if npc else "player") + " physical knockdown and get-up", manifest=manifest,
                      synthetic_actor_stats_and_placements=True, content=content, initial=initial, melee=melee_evidence,
                      runtime=runtime,
                      missed_reconnect_windows=missed_reconnect_windows,
                      reconnected=reconnected, restored=restored, validation=validation,
                      profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay",
                      final={r: after[r][-1] for r in evidence}, relay=asdict(relay.stop()), screenshots=captures)
        output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(dict(success=True, content=content,
                              validation={k: v for k, v in validation.items() if k != "retarget"},
                              retarget_contacts={k: len(v["contacts"]) for k, v in retarget_validation.items()
                                                 if "contacts" in v})), flush=True)
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
    capture_actor_melee("restored")
    after = {r: samples(r) for r in evidence}
    validation = validate_observations([before, after], npc)
    if actor_melee:
        validation["actor_melee"] = validate_actor_melee([before, after])
    # A recovery before the restart cannot substitute for a missing restored tail.
    validation["segments"] = [validate_observations([segment], npc) for segment in (before, after)]
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
    report = dict(success=True, scenario="V51 live " + ("NPC" if npc else "player") + " fatigue knockout and get-up", manifest=manifest,
                  synthetic_actor_placements_and_spell=True, content=content, runtime=runtime, melee=melee_evidence, initial=initial, restored=restored,
                  final={r: after[r][-1] for r in evidence}, validation=validation,
                  relay=asdict(relay.stop()), screenshots=captures)
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
