"""V46 rendered two-client acceptance driven through normal melee intents."""

from dataclasses import asdict
import json
import time


def validate_observations(segments):
    """Compare committed poses without assuming latest-wins peers receive every tick."""
    overlap = 0
    phases = {(role, player): set() for role in ("Alice", "Bob") for player in (1, 2)}
    outcomes = {role: [] for role in ("Alice", "Bob")}
    for segment in segments:
        identities = {}
        for role, rows in segment.items():
            for row in rows:
                outcomes[role].extend(row["player_hits"])
                for swing in row["swings"]:
                    if not swing["command"]:
                        continue
                    phases[role, swing["player"]].add(swing["phase"])
                    key = (swing["player"], swing["command"])
                    identities.setdefault(key, {}).setdefault(role, []).append((row["tick"], swing))
        for observers in identities.values():
            if len(observers) != 2:
                continue
            overlap += 1
            merged = sorted((tick, role, pose) for role, rows in observers.items() for tick, pose in rows)
            previous = None
            for tick, role, pose in merged:
                if previous:
                    prior_tick, _, prior = previous
                    for field in ("source", "target_life", "direction", "strength", "group"):
                        if prior[field] != pose[field]:
                            raise RuntimeError(f"swing identity diverged: {field}")
                    if tick == prior_tick and pose != prior:
                        raise RuntimeError("two clients disagree at the same committed swing tick")
                    if prior["interruption"] and pose["interruption"] != prior["interruption"]:
                        raise RuntimeError("interrupted swing resumed")
                    if (pose["phase"], pose["completion"]) < (prior["phase"], prior["completion"]):
                        raise RuntimeError("committed swing clock regressed")
                previous = tick, role, pose
    if overlap < 4 or any(not {1, 2, 3, 4}.issubset(observed) for observed in phases.values()):
        raise RuntimeError("insufficient shared swing identities or phases")
    by_identity = {}
    for role, hits in outcomes.items():
        keys = [(h["attacker"], h["attacker_revision"], h["target_revision"]) for h in hits]
        if len(keys) != len(set(keys)):
            raise RuntimeError(f"{role}: duplicated melee outcome across reconnect/restart")
        by_identity[role] = dict(zip(keys, hits))
    common = by_identity["Alice"].keys() & by_identity["Bob"].keys()
    # Events emitted while a peer is offline need not be replayed. Its baseline
    # carries the durable resources; every event witnessed by both must agree.
    if not common or any(by_identity["Alice"][key] != by_identity["Bob"][key] for key in common):
        raise RuntimeError("two clients received different reliable melee outcomes")
    return dict(shared_identities=overlap, phases={f"{r}:{p}": sorted(v) for (r, p), v in phases.items()},
                matching_outcomes=len(common), outcomes=outcomes)


def verify_swing_encounter(output, evidence, processes, relay, manifest, content,
                           restart_server, restart_client):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def latest(role, player):
        rows = samples(role)
        return next((s for s in rows[-1]["swings"] if s["player"] == player), None) if rows else None

    def wait_for(predicate, description, timeout=25):
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
        for attempt in range(30):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                if attempt == 29:
                    raise
                time.sleep(.03)
        return wait_for(lambda: any(r.get("sequence") == sequence[role]
                                   and r.get("event") == "traversal_" + action.split()[0]
                                   for r in records(evidence[role])), f"{role}: {action}")

    def screenshot(role, label):
        command(role, "screenshot")
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(output / f"{role}-{label}.png")

    def begin(role, direction=0, strength=1):
        player = 1 if role == "Alice" else 2
        prior = latest(role, player)
        old = prior["command"] if prior else 0
        # Admission may reject while the NPC has this player in hit recovery.
        for attempt in range(8):
            command(role, f"swing {direction} {strength}")
            deadline = time.monotonic() + 1.5
            while time.monotonic() < deadline:
                pose = latest(role, player)
                if pose and pose["command"] != old:
                    return pose.copy()
                time.sleep(.025)
        raise RuntimeError(f"{role}: no admitted swing")

    def terminal(player, identity):
        poses = [latest(role, player) for role in evidence]
        return poses if all(p and p["command"] == identity and
                            (p["phase"] == 4 or p["interruption"]) for p in poses) else None

    def stage(role):
        observer = "Alice" if role == "Bob" else "Bob"
        # The observer occupies the NPC at closer range, freeing the attacker
        # from repeated hit recovery while framing its full body from the side.
        command(observer, "pose 140 -34 1 0.1 -2.56")
        command(role, "pose 60 -150 1 0 0")
        time.sleep(.4)  # Allow the impaired movement samples to reach authority.

    def windup_for(role):
        player = 1 if role == "Alice" else 2
        for attempt in range(8):
            pose = begin(role)
            if pose["phase"] == 1 and not pose["interruption"]:
                return pose
            wait_for(lambda: terminal(player, pose["command"]), "late sample settles before next wind-up")
        raise RuntimeError("no live wind-up sample available for disruption")

    wait_for(lambda: all(len(samples(role)) >= 3 and len(samples(role)[-1]["swings"]) == 2
                         for role in evidence), "two V46 desktop baselines", 75)
    initial = {role: samples(role)[-1] for role in evidence}
    completed = []
    captured_players = set()
    for direction, strength in ((0, 1), (1, .6), (2, .3)):
        for role, player in (("Bob", 2), ("Alice", 1)):
            stage(role)
            for attempt in range(8):
                pose = begin(role, direction, strength)
                if direction == 0:
                    observer = "Alice" if role == "Bob" else "Bob"
                    observed = wait_for(lambda: (p if (p := latest(observer, player)) and
                                                 p["command"] == pose["command"] else None),
                                        "peer receives the captured swing identity")
                    if observed["phase"] in (1, 2) and not observed["interruption"]:
                        command(observer, "facepeer")
                        screenshot(observer, f"peer-{role}-swing-{attempt}")
                        captured_players.add(player)
                end = wait_for(lambda: terminal(player, pose["command"]), f"{role} swing terminal")
                if all(p["phase"] == 4 and not p["interruption"] for p in end):
                    completed.append(end[0])
                    break
            else:
                raise RuntimeError(f"{role}: direction {direction} never completed")
    if captured_players != {1, 2}:
        raise RuntimeError("missing in-progress peer screenshot for one participant")

    # Disconnect the attacker during wind-up; the remaining observer must see a
    # terminal cancellation and reconnect must retain that same identity/reason.
    stage("Bob")
    pending = windup_for("Bob")
    generation = samples("Bob")[-1]["generation"]
    command("Bob", "disconnectbrief")
    cancelled = wait_for(lambda: (p if (p := latest("Alice", 2)) and
                                  p["command"] == pending["command"] and p["interruption"] == 1 else None),
                         "observer sees disconnected swing cancellation")
    wait_for(lambda: samples("Bob")[-1]["generation"] > generation and
             latest("Bob", 2) == cancelled, "reconnect retains terminal swing cancellation")
    screenshot("Bob", "reconnected")

    # Capture the durable image by killing the server before either desktop.
    # Bring the swinging player back first so the other arrival cannot cancel it.
    stage("Alice")
    windup = windup_for("Alice")
    processes["server"].terminate()
    processes["server"].wait(timeout=15)
    finished.add("server")
    for role in evidence:
        processes[role].terminate()
        processes[role].wait(timeout=15)
        finished.add(role)
    before = {role: samples(role) for role in evidence}
    for role, path in evidence.items():
        path.rename(output / f"{role}-before.ndjson")
        path.with_suffix(".ndjson.control").unlink(missing_ok=True)
        sequence[role] = 0
    restart_server()
    restart_client("Alice")
    finished.remove("Alice")
    wait_for(lambda: samples("Alice"), "attacker returns after server restart", 75)
    restart_client("Bob")
    finished.remove("Bob")
    wait_for(lambda: len(samples("Bob")) >= 3, "observer returns after server restart", 75)
    restored = {role: samples(role)[0] for role in evidence}
    if not any(p["player"] == 1 and p["command"] == windup["command"] and
               p["phase"] in (1, 2, 3) and not p["interruption"]
               for row in samples("Alice") for p in row["swings"]):
        raise RuntimeError("restart did not expose the saved swing still in progress")
    resumed = wait_for(lambda: terminal(1, windup["command"]), "saved swing reaches a shared terminal state")
    if any(p["interruption"] for p in resumed):
        raise RuntimeError("saved swing was cancelled instead of completing after restart")
    for role in evidence:
        if not any(p["command"] == windup["command"] and p["player"] == 1
                   for row in samples(role) for p in row["swings"]):
            raise RuntimeError("restart lost pending swing identity")
        if restored[role]["actors"][0]["health"] > before[role][-1]["actors"][0]["health"] + .01:
            raise RuntimeError("restart lost committed NPC damage")
    # New intents must work after recovery, with new outcomes on both peers.
    for role, player in (("Alice", 1), ("Bob", 2)):
        stage(role)
        pose = begin(role, 1, .6)
        wait_for(lambda: terminal(player, pose["command"]), f"{role} can swing after restart")
    time.sleep(1)  # Drain reliable outcomes before comparing the complete streams.
    after = {role: samples(role) for role in evidence}
    validation = validate_observations([before, after])
    if not all(any(h["attacker"] == player and h["hit"] and h["damage"] > 0
                   for h in validation["outcomes"][role]) for role in evidence for player in (1, 2)):
        raise RuntimeError("missing physical damage from both participants")
    for role in evidence:
        screenshot(role, "final")
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
    report = dict(success=True, scenario="V46 live player swing presentation", manifest=manifest,
                  synthetic_actor_and_placements=True, unchanged_gameplay_records=content,
                  initial=initial, completed=completed, interrupted=cancelled,
                  restart_windup=windup, restored=restored, final={r: after[r][-1] for r in evidence},
                  validation=validation, relay=asdict(relay.stop()),
                  screenshots=[p.name for p in output.glob("*.png")])
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(success=True, shared_identities=validation["shared_identities"],
                          relay=report["relay"])), flush=True)
