"""Observed V53 casting timeline; uses normal desktop intents over impaired UDP."""

from dataclasses import asdict
import json
import time


def verify_player_payments(report):
    """Compare both observers with unique committed payment outcomes across restart."""
    paid = {}
    for segment in ("pre_restart_events", "post_restart_events"):
        for events in report[segment].values():
            for event in events:
                if event["caster_kind"] != 1 or event["magicka_delta"] >= 0:
                    continue
                key = (event["caster"], event["caster_revision"])
                if key in paid and paid[key] != event:
                    raise RuntimeError("observers disagree on a player payment")
                paid[key] = event
    result = {}
    for role, player in (("Alice", 1), ("Bob", 2)):
        payments = [e for (caster, _), e in paid.items() if caster == player]
        expected = report["initial"][role]["magicka"] + sum(e["magicka_delta"] for e in payments)
        observed = []
        for sample in report["final"].values():
            resource = sample if sample["self"] == player else next(p for p in sample["players"] if p["id"] == player)
            observed.append(resource["magicka"])
        if not payments or any(abs(value - expected) > .01 for value in observed):
            raise RuntimeError(f"player {player} payment/resources did not converge: {observed}, expected {expected}")
        result[role] = dict(payments=len(payments), magicka=expected, observers=observed)
    return result


def verify_cast_encounter(output, evidence, processes, relay, manifest, content,
                          restart_server, restart_clients):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def wait_for(predicate, description, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                print(description, flush=True)
                return result
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited: {description}")
            time.sleep(.05)
        raise RuntimeError(f"timed out: {description}")

    def submit(role, action):
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
                time.sleep(.01)  # Windows reader briefly owns the control file.

    def command(role, action):
        submit(role, action)
        return wait_for(lambda: any(r.get("sequence") == sequence[role]
                                   and r.get("event") == "traversal_" + action.split()[0]
                                   for r in records(evidence[role])), f"{role}: {action}")

    def events(role, caster_kind=None):
        return [e for r in samples(role) for e in r["magic_events"]
                if caster_kind is None or e["caster_kind"] == caster_kind]

    def both_outcomes():
        return all(any(e["caster"] == player and e["success"] for e in events(role, 1))
                   for role in evidence for player in (1, 2))

    wait_for(lambda: all(len(samples(role)) >= 3 and samples(role)[-1]["actors"] for role in evidence),
             "two desktop combat baselines", 75)
    initial = {role: samples(role)[-1] for role in evidence}
    for role, x in (("Alice", 60), ("Bob", -160)):
        command(role, f"pose {x} -400 1 0 0")
    observed_windup = wait_for(lambda: next((r for r in samples("Alice")[-1:]
        if any(a["cast_phase"] == 3 and 5 <= a["cast_elapsed"] <= a["cast_release"] - 10
               for a in r["actors"])), None), "NPC animation wind-up")
    for role in evidence:
        command(role, "screenshot")
        # Control sequences restart with each desktop process. Preserve this
        # pre-crash visual instead of letting the final screenshot replace it.
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(
            output / f"{role}-windup.png")
    for role in evidence:
        command(role, "thirdperson")
        command(role, "facepeer")
    # Submit without waiting between clients: both intentions race the same NPC cast.
    for role in evidence:
        submit(role, f'castactor "{content["spell"]}"')
    player_windup = wait_for(lambda: next((samples(role)[-1] for role in evidence
        if sum(1 for c in samples(role)[-1]["casts"] if c["cast_phase"] == 3) == 2), None),
        "concurrent player wind-ups")
    def rendered_windup(role):
        frames = [r for r in records(evidence[role]) if r.get("event") == "actor_presentation_frame"]
        return frames and sum(p["kind"] == 1 and p.get("cast_phase") == 3
            and 10 <= p["cast_frame"] < p["cast_release"] - 4 for p in frames[-1]["actors"]) == 2
    wait_for(lambda: all(rendered_windup(role) for role in evidence), "both desktops render concurrent mid-wind-up poses")
    for role in evidence:
        command(role, "screenshot")
        evidence[role].with_suffix(f".ndjson.control.{sequence[role]}.png").rename(
            output / f"{role}-player-windup.png")
    wait_for(both_outcomes, "both clients observe both player casts")
    wait_for(lambda: all(any(e["success"] for e in events(role, 2)) for role in evidence),
             "both clients observe autonomous NPC release")
    wait_for(lambda: samples("Alice")[-1]["actors"][0]["health"] < initial["Alice"]["actors"][0]["health"]
             and samples("Alice")[-1]["health"] < initial["Alice"]["health"],
             "durable NPC and player health outcomes")
    concurrent = {role: samples(role)[-1] for role in evidence}
    command("Alice", "screenshot")
    marker = len(records(evidence["Bob"]))
    generation = samples("Bob")[-1]["generation"]
    command("Bob", "reconnect")
    wait_for(lambda: any(r.get("event") == "phase8_desktop_status" and r.get("status") == "resumed"
                         for r in records(evidence["Bob"])[marker:])
             and samples("Bob")[-1]["generation"] > generation, "Bob reconnects to durable combat")
    # Removing the nearer player makes the NPC select the second participant.
    prior_bob_health = samples("Bob")[-1]["health"]
    interruption = wait_for(lambda: next((r for r in samples("Alice")[-1:]
        if any(a["cast_phase"] == 3 and a["cast_elapsed"] <= a["cast_release"] - 15
               for a in r["actors"])), None), "pre-release cast before target disconnect")
    command("Alice", "disconnectbrief")
    interrupted_actor = interruption["actors"][0]
    cancelled = wait_for(lambda: next((r for r in samples("Bob")
        if r["tick"] > interruption["tick"] and r["actors"]
        and r["actors"][0]["cast_id"] != interrupted_actor["cast_id"]), None), "cast cancelled on target disconnect")
    if abs(cancelled["actors"][0]["magicka"] - interrupted_actor["magicka"]) > .01:
        raise RuntimeError("pre-release interruption spent NPC magicka")
    if any(a["cast_id"] == interrupted_actor["cast_id"] and a["cast_phase"] >= 4
           for role in evidence for r in samples(role) for a in r["actors"]):
        raise RuntimeError("interrupted cast reached release")
    wait_for(lambda: samples("Bob")[-1]["health"] < prior_bob_health,
             "NPC independently damages the remaining player")
    wait_for(lambda: samples("Alice")[-1]["generation"] > initial["Alice"]["generation"],
             "Alice returns after the controlled disconnect interval", 25)
    command("Bob", "screenshot")
    # Interrupt a player before payment while a peer keeps the area active.
    wait_for(lambda: all(not c["cast_id"] for role in evidence for c in samples(role)[-1]["casts"]),
             "player recovery completes")
    submit("Alice", f'castactor "{content["spell"]}"')
    player_interruption = wait_for(lambda: next((r for r in samples("Alice")[-1:]
        if any(c["id"] == 1 and c["cast_phase"] == 3 and c["cast_elapsed"] < c["cast_release"] - 12
               for c in r["casts"])), None), "player wind-up before disconnect")
    interrupted_player = next(c for c in player_interruption["casts"] if c["id"] == 1)
    command("Alice", "disconnectbrief")
    player_cancelled = wait_for(lambda: next((r for r in samples("Bob")[-1:]
        if r["tick"] > player_interruption["tick"] and any(c["id"] == 1 and not c["cast_id"] for c in r["casts"])), None),
        "player cast interrupted independently")
    alice_resource = next(p for p in player_cancelled["players"] if p["id"] == 1)
    if abs(alice_resource["magicka"] - player_interruption["magicka"]) > .01:
        raise RuntimeError("interrupted player wind-up paid magicka")
    wait_for(lambda: samples("Alice")[-1]["generation"] > player_interruption["generation"],
             "player returns after interrupted wind-up", 25)
    for role in evidence:
        submit(role, f'castactor "{content["spell"]}"')
    # Crash while both casts are pending. Offline time cannot consume the release.
    windup = wait_for(lambda: next((r for r in samples("Alice")[-1:]
        if all(c["cast_phase"] == 3 and c["cast_elapsed"] < c["cast_release"] - 3 for c in r["casts"])
        and len(r["casts"]) == 2), None), "restart during concurrent player wind-up")
    processes["server"].terminate()
    processes["server"].wait(timeout=15)
    finished.add("server")
    for role in evidence:
        processes[role].terminate()
        processes[role].wait(timeout=15)
        finished.add(role)
    before = {role: samples(role) for role in evidence}
    before_events = {role: events(role) for role in evidence}
    for role, path in evidence.items():
        path.rename(output / (role + "-before.ndjson"))
        path.with_suffix(".ndjson.control").unlink(missing_ok=True)
        sequence[role] = 0
    restart_server()
    # Bring Alice back first to retain her cast; Bob may cancel while absent.
    restart_clients("Alice")
    finished.discard("Alice")
    wait_for(lambda: samples("Alice"), "cast target returns after restart", 75)
    restart_clients("Bob")
    finished.discard("Bob")
    wait_for(lambda: all(len(samples(role)) >= 3 for role in evidence), "two desktops return after process restart", 75)
    restored = {role: samples(role)[0] for role in evidence}
    for role in evidence:
        command(role, "thirdperson")
        command(role, "facepeer")
    saved_cast = next(c["cast_id"] for c in windup["casts"] if c["id"] == 1)
    if not any(c["id"] == 1 and c["cast_id"] == saved_cast for role in evidence for r in samples(role) for c in r["casts"]):
        raise RuntimeError("restart did not retain the player's pending cast identity")
    for role in evidence:
        if restored[role]["actors"][0]["health"] > concurrent[role]["actors"][0]["health"] + .01:
            raise RuntimeError("restart lost committed NPC damage")
    wait_for(lambda: all(any(e["success"] for e in events(role, 2)) for role in evidence),
             "NPC casting resumes after restart")
    wait_for(lambda: all(not c["cast_id"] for role in evidence for c in samples(role)[-1]["casts"]),
             "retained player cast completes recovery")
    for role in evidence:
        submit(role, f'castactor "{content["spell"]}"')
    wait_for(both_outcomes, "both players cast after restart")
    wait_for(lambda: all(not c["cast_id"] for role in evidence for c in samples(role)[-1]["casts"]),
             "new player casts complete recovery")
    wait_for(lambda: samples("Alice")[-1]["actors"][0]["health"] < restored["Alice"]["actors"][0]["health"],
             "new damage survives resumed encounter")
    final = {role: samples(role)[-1] for role in evidence}
    all_samples = {role: before[role] + samples(role) for role in evidence}
    matched = 0
    alice = {s["tick"]: s for s in all_samples["Alice"]}
    for bob in all_samples["Bob"]:
        a = alice.get(bob["tick"])
        if a:
            if a["actors"] != bob["actors"]:
                raise RuntimeError("two clients disagree at the same committed tick")
            matched += 1
    # Latest-wins snapshots are sent on different ticks to each peer. In
    # addition to exact matches, compare bounded bracketing resource samples
    # and the common animation clock while both peers observe the same cast.
    aligned = timing_matches = 0
    for left, right in ((before["Alice"], before["Bob"]), (samples("Alice"), samples("Bob"))):
        by_tick = sorted({r["tick"]: r for r in right}.values(), key=lambda r: r["tick"])
        index = 0
        for sample in left:
            while index + 1 < len(by_tick) and by_tick[index + 1]["tick"] < sample["tick"]:
                index += 1
            if index + 1 >= len(by_tick): break
            b, c = by_tick[index:index + 2]
            if not b["tick"] <= sample["tick"] <= c["tick"] or c["tick"] - b["tick"] > 64:
                continue
            a, b_actor, c_actor = sample["actors"][0], b["actors"][0], c["actors"][0]
            for field in ("health", "magicka", "fatigue"):
                low, high = sorted((b_actor[field], c_actor[field]))
                if not low - .05 <= a[field] <= high + .05:
                    raise RuntimeError(f"two-client resource divergence: {field}")
            aligned += 1
        clocks = {(a["id"], a["cast_id"]): a["cast_elapsed"] - r["tick"]
                  for r in right for a in r["actors"] if a["cast_phase"] == 3}
        for r in left:
            for a in r["actors"]:
                key = (a["id"], a["cast_id"])
                if a["cast_phase"] == 3 and key in clocks:
                    if a["cast_elapsed"] - r["tick"] != clocks[key]:
                        raise RuntimeError("two-client animation clock divergence")
                    timing_matches += 1
    if aligned < 5 or timing_matches < 2:
        raise RuntimeError("insufficient overlapping authoritative resource/animation samples")
    for role in evidence:
        combined = before_events[role] + events(role)
        identities = [json.dumps(e, sort_keys=True) for e in combined]
        if len(identities) != len(set(identities)):
            raise RuntimeError("duplicate successful magic outcome across reconnect/restart")
        if content["profile"].endswith("item") and not any(e["caster_kind"] == 2
                and e["source_kind"] == 1 and e["success"] for e in combined):
            raise RuntimeError("equipped WhenUsed never won a live action")
    phases = {a["cast_phase"] for rows in all_samples.values() for s in rows for a in s["actors"]}
    if not {3, 5}.issubset(phases):
        raise RuntimeError("wind-up and recovery were not both presented")
    for role in evidence:
        command(role, "screenshot")
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
    report = dict(success=True, scenario="V53 shared cast timeline and concurrent real-record casting",
                  synthetic_actor_and_placements=True, unchanged_gameplay_records=content,
                  manifest=manifest, initial=initial, concurrent=concurrent, windup=windup, observed_windup=observed_windup,
                  restored=restored, final=final, interruption=interruption, cancelled=cancelled,
                  player_windup=player_windup, player_interruption=player_interruption, player_cancelled=player_cancelled,
                  observed_phases=sorted(phases), matching_ticks=matched, aligned_samples=aligned, matching_animation_clocks=timing_matches,
                  pre_restart_events=before_events, post_restart_events={role: events(role) for role in evidence},
                  relay=asdict(relay.stop()), screenshots=[p.name for p in output.glob("*.png")])
    report["player_payments"] = verify_player_payments(report)
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("success", "scenario", "unchanged_gameplay_records", "matching_ticks", "relay")}), flush=True)
