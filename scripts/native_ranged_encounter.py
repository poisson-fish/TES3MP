"""Two desktop views of committed physical flight through an impaired relay."""

from dataclasses import asdict
import json
import time


def verify_ranged_encounter(output, evidence, processes, relay, manifest, expected_actors=0,
                            creature_damage=False):
    from run_native_navigation_capture import records

    sequence = dict.fromkeys(evidence, 0)
    ammunition_item = None

    def rows(role, event):
        return [row for row in records(evidence[role]) if row.get("event") == event]

    def wait_for(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                print(description, flush=True)
                return value
            if any(process.poll() is not None for process in processes.values()):
                raise RuntimeError(f"process exited before {description}")
            time.sleep(.025)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="utf-8")
        for retry in range(20):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                if retry == 19:
                    raise
                time.sleep(.01)
        return wait_for(lambda: any(row.get("sequence") == sequence[role]
                                    and row.get("event") == "traversal_" + action.split()[0]
                                    for row in records(evidence[role])), f"{role}: {action}")

    def ammo(role):
        states = rows(role, "traversal_inventory")
        if not states:
            return 0
        items = states[-1]["player"]
        if ammunition_item is None:
            return max((item["count"] for item in items), default=0)
        return sum(item["count"] for item in items if item["item"] == ammunition_item)

    def flight(role, caster, after_tick=0):
        return [(row["tick"], shot) for row in rows(role, "native_combat_sample")
                for shot in row["projectiles"] if shot["kind"] == 1 and shot["caster"] == caster
                and shot["release"] > after_tick]

    wait_for(lambda: all(rows(role, "native_combat_sample") and ammo(role) >= 20 for role in evidence),
             "two ranged desktop baselines", 75)
    observed_actors = []
    if expected_actors:
        def complete_view(role):
            for row in reversed(rows(role, "native_combat_sample")):
                actors = {actor["id"] for actor in row["actors"]}
                if len(actors) == expected_actors:
                    return actors
            return set()
        wait_for(lambda: all(len(complete_view(role)) == expected_actors
                             and len({row["placement"] for row in rows(role, "native_actor_pose")})
                                 >= expected_actors for role in evidence),
                 "both desktops rendered all bound NPC placements", 75)
        if complete_view("Alice") != complete_view("Bob"):
            raise RuntimeError("desktop NPC placement identities differ")
        observed_actors = sorted(complete_view("Alice"))
    initial = {role: ammo(role) for role in evidence}
    if initial != {"Alice": 20, "Bob": 20}:
        raise RuntimeError(f"unexpected initial ammunition counts: {initial}")
    ammunition_item = next(item["item"] for item in rows("Alice", "traversal_inventory")[-1]["player"]
                             if item["count"] == 20)
    if creature_damage:
        creature = max(observed_actors)
        baseline = {role: next(actor for actor in rows(role, "native_combat_sample")[-1]["actors"]
                               if actor["id"] == creature) for role in evidence}
        if baseline["Alice"]["health"] != baseline["Bob"]["health"] or any(
                actor["dead"] for actor in baseline.values()):
            raise RuntimeError("creature baseline differs between desktops")
        pose = next(row for row in reversed(rows("Alice", "native_actor_pose"))
                    if row["placement"] == creature)
        command("Alice", f"pose {pose['x']} {pose['y'] - 120} {pose['z']} 0 0")
        time.sleep(.7)
        command("Alice", "shoot 1")
        def creature_hits(role):
            return [hit for row in rows(role, "native_combat_sample") for hit in row["player_hits"]
                    if hit["attacker"] == 1 and hit["target"] == creature]
        wait_for(lambda: all(creature_hits(role) for role in evidence),
                 "both desktops received the player arrow impact on the creature", 15)
        hits = {role: creature_hits(role) for role in evidence}
        if len(hits["Alice"]) != 1 or hits["Alice"] != hits["Bob"] or not hits["Alice"][0]["hit"]:
            raise RuntimeError("creature impact missed, diverged or duplicated")
        hit = hits["Alice"][0]
        terminal = {role: next((shot for row in rows(role, "native_combat_sample")
                                for shot in row["projectiles"]
                                if shot["kind"] == 1 and shot["caster"] == 1
                                and shot["terminal"] == 1), None) for role in evidence}
        if any(shot is None for shot in terminal.values()):
            raise RuntimeError("creature impact has no committed arrow terminal")
        key = tuple(terminal["Alice"][name] for name in ("kind", "caster", "life", "command"))
        if key != tuple(terminal["Bob"][name] for name in ("kind", "caster", "life", "command")):
            raise RuntimeError("creature arrow identity differs between desktops")
        def rendered_impact(role):
            return [shot for row in rows(role, "projectile_presentation_frame")
                    for shot in row["projectiles"] if shot["terminal"] == 1
                    and tuple(shot[name] for name in ("kind", "caster", "life", "command")) == key]
        wait_for(lambda: all(rendered_impact(role) for role in evidence),
                 "both desktops rendered the creature arrow impact cue")
        cues = {role: rendered_impact(role) for role in evidence}
        for role, frames in cues.items():
            if any(sum((point - origin) ** 2 for point, origin in zip(frame["position"][:2],
                        (pose["x"], pose["y"]))) > 100 ** 2 for frame in frames):
                raise RuntimeError(f"{role} rendered the impact away from the creature")
        wait_for(lambda: all(next(actor for actor in rows(role, "native_combat_sample")[-1]["actors"]
                                  if actor["id"] == creature)["revision"] >= hit["target_revision"]
                             for role in evidence), "both desktops received the creature's committed state")
        def creature_state(role):
            return next(actor for actor in rows(role, "native_combat_sample")[-1]["actors"]
                        if actor["id"] == creature)
        states = {role: creature_state(role) for role in evidence}
        expected_health = max(0, baseline["Alice"]["health"] - hit["damage"])
        if any(abs(state["health"] - expected_health) > .1 or state["dead"] != hit["died"]
               for state in states.values()):
            raise RuntimeError("creature health or life differs from the committed arrow impact")
        wait_for(lambda: ammo("Alice") == 19, "one creature arrow consumed")
        if ammo("Bob") != 20:
            raise RuntimeError("observer ammunition changed during creature hit")
        time.sleep(.3)  # Let the committed 250 ms terminal cue finish before reconnect.
        cues = {role: rendered_impact(role) for role in evidence}
        before_generation = rows("Bob", "native_combat_sample")[-1]["generation"]
        command("Bob", "reconnect")
        wait_for(lambda: rows("Bob", "native_combat_sample")[-1]["generation"] > before_generation,
                 "Bob reconnected after the creature hit")
        if creature_state("Bob")["health"] != states["Alice"]["health"] or creature_state("Bob")["dead"] != hit["died"]:
            raise RuntimeError("creature life diverged after reconnect")
        if creature_hits("Bob") != hits["Bob"] or ammo("Alice") != 19 or ammo("Bob") != 20:
            raise RuntimeError("creature impact or ammunition replayed after reconnect")
        time.sleep(.3)
        if len(rendered_impact("Bob")) != len(cues["Bob"]):
            raise RuntimeError("creature impact cue replayed after reconnect")
        report = dict(success=True, scenario="V66 player arrow damage to bound creature on two desktops",
                      manifest=manifest, creature=creature, baseline=baseline, impact=hit,
                      impact_presentation={role: dict(frames=len(cues[role]), terminal=cues[role][0])
                                           for role in evidence},
                      final={role: creature_state(role) for role in evidence},
                      initial_ammunition=initial, final_ammunition={role: ammo(role) for role in evidence},
                      relay=asdict(relay.stop()))
        output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2), flush=True)
        return
    start_tick = rows("Alice", "native_combat_sample")[-1]["tick"]
    first = None
    attempted_releases = 0
    for y, pitch in ((-480, 0), (-480, .1), (-480, -.1), (-350, 0)):
        command("Alice", f"pose 60 {y} 1 {pitch} 0")
        time.sleep(.7)
        command("Alice", "shoot 0.25")
        attempted_releases += 1
        try:
            first = wait_for(lambda: next(((tick, shot) for tick, shot in flight("Alice", 1, start_tick)
                                           if shot["terminal"] == 0), None),
                             "Alice committed ranged release", 3)
            break
        except RuntimeError as error:
            if "timed out" not in str(error) or ammo("Alice") != initial["Alice"]:
                raise
    if first is None:
        raise RuntimeError("no mapped ranged release after bounded aim setups")
    key = (first[1]["kind"], first[1]["caster"], first[1]["life"], first[1]["command"])
    def matching(role):
        return [(tick, shot) for tick, shot in flight(role, 1, start_tick)
                if (shot["kind"], shot["caster"], shot["life"], shot["command"]) == key]
    wait_for(lambda: any(shot["terminal"] == 0 for _, shot in matching("Bob")),
             "Bob observed the committed flight")
    wait_for(lambda: all(any(shot["terminal"] for _, shot in matching(role)) for role in evidence),
             "both clients observed terminal contact")
    shared = {role: dict(matching(role)) for role in evidence}
    overlap = shared["Alice"].keys() & shared["Bob"].keys()
    if not overlap or any(shared["Alice"][tick] != shared["Bob"][tick] for tick in overlap):
        raise RuntimeError("same-tick physical flight diverged between clients")
    def rendered(role):
        return [shot for row in rows(role, "projectile_presentation_frame")
                for shot in row["projectiles"]
                if (shot["kind"], shot["caster"], shot["life"], shot["command"]) == key]
    wait_for(lambda: all(any(shot.get("terminal") == 1 for shot in rendered(role))
                         for role in evidence), "both desktops rendered the terminal impact cue")
    wait_for(lambda: all(len({tuple(shot["position"]) for shot in rendered(role)}) >= 2
                         for role in evidence), "both desktops rendered moving committed arrows")
    visual_frames = {role: len(rendered(role)) for role in evidence}
    wait_for(lambda: ammo("Alice") == initial["Alice"] - 1, "one committed arrow consumed")
    if ammo("Bob") != initial["Bob"]:
        raise RuntimeError("observer ammunition changed during Alice's shot")
    wait_for(lambda: all(any(hit["attacker"] == 1 for row in rows(role, "native_combat_sample")
                             for hit in row["player_hits"]) for role in evidence),
             "both clients received the first attributed impact")
    before_generation = rows("Bob", "native_combat_sample")[-1]["generation"]
    command("Bob", "reconnect")
    wait_for(lambda: rows("Bob", "native_combat_sample")[-1]["generation"] > before_generation,
             "Bob reconnected to the flight receipt")
    restored = rows("Bob", "native_combat_sample")[-1]
    receipt = next((shot for shot in restored["projectiles"]
                    if (shot["kind"], shot["caster"], shot["life"], shot["command"]) == key), None)
    if not receipt or receipt["terminal"] != 1 or ammo("Alice") != initial["Alice"] - 1 or ammo("Bob") != initial["Bob"]:
        raise RuntimeError("reconnect lost terminal contact or duplicated ammunition cost")
    hits = {role: [hit for row in rows(role, "native_combat_sample") for hit in row["player_hits"]
                   if hit["attacker"] == 1] for role in evidence}
    for role, events in hits.items():
        identities = [(event["attacker_revision"], event["target_revision"]) for event in events]
        if len(identities) != len(set(identities)) or len(events) > 1:
            raise RuntimeError(f"{role} received duplicate physical impacts")
    start_health = rows("Alice", "native_combat_sample")[0]["actors"][0]["health"]
    target = hits["Alice"][0]["target"]
    victim = next(row for row in reversed(rows("Alice", "native_actor_pose"))
                  if row["placement"] == target)
    command("Alice", f"pose {victim['x']} {victim['y'] - 120} {victim['z']} 0 0")
    time.sleep(1)
    previous_health = rows("Alice", "native_combat_sample")[-1]["actors"][0]["health"]
    for retry in range(4):
        command("Alice", "shoot 1")
        attempted_releases += 1
        try:
            wait_for(lambda: ammo("Alice") == initial["Alice"] - 2,
                     "second committed arrow consumed", 2)
            break
        except RuntimeError as error:
            if "timed out" not in str(error) or retry == 3:
                raise
    if expected_actors:
        def attributed(role):
            return [hit for row in rows(role, "native_combat_sample")
                    for hit in row["player_hits"] if hit["attacker"] == 1]
        wait_for(lambda: all(len({hit["target"] for hit in attributed(role) if hit["died"]}) >= 2
                             for role in evidence),
                 "both clients observed deaths of two distinct neighbors", 15)
        deaths = {role: [(hit["target"], hit["attacker_revision"], hit["target_revision"])
                         for hit in attributed(role) if hit["died"]] for role in evidence}
        if deaths["Alice"] != deaths["Bob"] or len(deaths["Alice"]) != len(set(deaths["Alice"])):
            raise RuntimeError("two-client neighbor death identities diverged or duplicated")
        killed = {event[0] for event in deaths["Alice"]}
        if len(killed) < 2 or not killed <= set(observed_actors):
            raise RuntimeError("ranged deaths escaped the bound NPC placement set")
        for role in evidence:
            actors = {actor["id"]: actor for actor in rows(role, "native_combat_sample")[-1]["actors"]}
            if set(actors) != set(observed_actors) or any(not actors[placement]["dead"] for placement in killed):
                raise RuntimeError(f"{role} lost a bound NPC or its committed death")
        if ammo("Alice") != initial["Alice"] - 2 or ammo("Bob") != initial["Bob"]:
            raise RuntimeError("two-neighbor combat duplicated ammunition cost")
        report = dict(success=True, scenario="V66 four-neighbor combat on two desktops",
                      manifest=manifest, observed_actors=observed_actors,
                      neighbor_deaths=deaths["Alice"], attempted_releases=attempted_releases,
                      initial_ammunition=initial, final_ammunition={role: ammo(role) for role in evidence},
                      first_flight=key, visual_frames=visual_frames, relay=asdict(relay.stop()))
        output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2), flush=True)
        return
    wait_for(lambda: rows("Alice", "native_combat_sample")[-1]["actors"][0]["health"] < previous_health,
             "second arrow hit the original target", 12)
    second_key = wait_for(lambda: next(((shot["kind"], shot["caster"], shot["life"], shot["command"])
                                        for _, shot in flight("Alice", 1, first[1]["release"])
                                        if shot["terminal"]), None), "short second flight resolved")
    wait_for(lambda: all(any(shot.get("terminal") == 1
                             and (shot["kind"], shot["caster"], shot["life"], shot["command"]) == second_key
                             for row in rows(role, "projectile_presentation_frame")
                             for shot in row["projectiles"]) for role in evidence),
             "both desktops rendered the short-flight terminal cue")
    shots = 2
    wait_for(lambda: all(rows(role, "native_combat_sample")[-1]["actors"][0]["dead"]
                         for role in evidence), "both desktops agreed on the ranged death")
    wait_for(lambda: rows("Bob", "traversal_inventory")[-1]["container_count"] > 0,
             "ranged corpse has committed loot")
    wait_for(lambda: all(any(hit["died"] for row in rows(role, "native_combat_sample")
                             for hit in row["player_hits"] if hit["attacker"] == 1)
                         for role in evidence), "both clients received the attributed killing impact")
    hits = {role: [hit for row in rows(role, "native_combat_sample") for hit in row["player_hits"]
                   if hit["attacker"] == 1] for role in evidence}
    identities = {role: [(hit["attacker_revision"], hit["target_revision"])
                         for hit in events] for role, events in hits.items()}
    if (identities["Alice"] != identities["Bob"]
            or len(identities["Alice"]) != len(set(identities["Alice"]))
            or len(hits["Alice"]) > shots or sum(hit["died"] for hit in hits["Alice"]) != 1):
        raise RuntimeError("ranged impacts or death duplicated across clients")
    if abs(max(0, start_health - sum(hit["damage"] for hit in hits["Alice"] if hit["hit"]))
           - rows("Alice", "native_combat_sample")[-1]["actors"][0]["health"]) > .1:
        raise RuntimeError("ranged target health differs from committed impacts")
    pose = next(row for row in reversed(rows("Bob", "native_actor_pose"))
                if row["placement"] == target)
    command("Bob", f"pose {pose['x']} {pose['y'] - 55} {pose['z']} 0 0")
    time.sleep(1)
    corpse = rows("Bob", "traversal_inventory")[-1]
    command("Bob", "open")
    command("Bob", "takeall")
    wait_for(lambda: rows("Bob", "traversal_inventory")[-1]["container_count"] == 0
             and rows("Bob", "traversal_inventory")[-1]["player_count"] > corpse["player_count"],
             "one committed corpse transfer")
    wait_for(lambda: rows("Alice", "traversal_inventory")[-1]["container_count"] == 0,
             "Alice observed the emptied corpse")
    looted_count = rows("Bob", "traversal_inventory")[-1]["player_count"]
    looted_ammo = ammo("Bob")
    before_generation = rows("Bob", "native_combat_sample")[-1]["generation"]
    command("Bob", "reconnect")
    wait_for(lambda: rows("Bob", "native_combat_sample")[-1]["generation"] > before_generation,
             "looter reconnected after transfer")
    wait_for(lambda: rows("Bob", "traversal_inventory")[-1]["container_count"] == 0,
             "empty corpse persisted across reconnect")
    if rows("Bob", "traversal_inventory")[-1]["player_count"] != looted_count:
        raise RuntimeError("corpse loot duplicated on reconnect")
    if ammo("Alice") != initial["Alice"] - shots or ammo("Bob") != looted_ammo:
        raise RuntimeError("ammunition changed after the encounter or reconnect")
    for role in evidence:
        if not rows(role, "native_combat_sample")[-1]["actors"][0]["dead"]:
            raise RuntimeError(f"{role} lost the committed ranged death")
        if expected_actors and {actor["id"] for actor in rows(role, "native_combat_sample")[-1]["actors"]} != set(observed_actors):
            raise RuntimeError(f"{role} lost a bound NPC after reconnect")
        current_hits = [hit for row in rows(role, "native_combat_sample")
                        for hit in row["player_hits"] if hit["attacker"] == 1]
        if [(hit["attacker_revision"], hit["target_revision"]) for hit in current_hits] != identities[role]:
            raise RuntimeError(f"{role} replayed a ranged impact after reconnect")
    world_start = max(shot["release"] for _, shot in flight("Alice", 1, start_tick))
    # The earlier capture covers expiry and reconnect. Aim at nearby floor
    # geometry here so the hull contact lands within the terminal cue window.
    command("Alice", "pose 60 -480 1 1.2 0")
    command("Alice", "shoot 0.25")
    attempted_releases += 1
    quick = wait_for(lambda: next(((tick, shot) for tick, shot in flight("Alice", 1, world_start)
                                   if shot["release"] > world_start), None),
                     "quick world shot released", 8)
    quick_key = (quick[1]["kind"], quick[1]["caster"], quick[1]["life"], quick[1]["command"])
    terminal = wait_for(lambda: next(((tick, shot) for tick, shot in flight("Alice", 1, quick[1]["release"] - 1)
                                      if (shot["kind"], shot["caster"], shot["life"], shot["command"]) == quick_key
                                      and shot["terminal"]), None),
                        "quick world hull resolved", 8)
    if (terminal[0] - quick[1]["release"] >= 30
            or abs(terminal[1]["position"][2]) > 5
            or not -500 < terminal[1]["position"][1] < -350):
        raise RuntimeError("quick world shot did not contact a nearby hull")
    wait_for(lambda: all(any(shot.get("terminal") == 1
                             and (shot["kind"], shot["caster"], shot["life"], shot["command"]) == quick_key
                             for row in rows(role, "projectile_presentation_frame")
                             for shot in row["projectiles"]) for role in evidence),
             "both desktops rendered the quick world-hull cue", 8)
    if any(len([hit for row in rows(role, "native_combat_sample") for hit in row["player_hits"]
                if hit["attacker"] == 1]) != len(hits[role]) for role in evidence):
        raise RuntimeError("world hull produced an actor impact")
    if ammo("Alice") != initial["Alice"] - shots - 1 or ammo("Bob") != looted_ammo:
        raise RuntimeError("quick world shot duplicated ammunition")
    report = dict(success=True, scenario="V66 four-neighbor ranged combat on two desktops" if expected_actors
                  else "V64 aimed ranged flight, quick world hull, cue and loot on two desktops",
                  manifest=manifest, flight=key, shared_ticks=len(overlap), visual_frames=visual_frames,
                  observed_actors=observed_actors,
                  shots=shots, quick_world_flight=quick_key,
                  quick_world_ticks=terminal[0] - quick[1]["release"], terminal_cues=[key, second_key, quick_key],
                  attempted_releases=attempted_releases, initial_ammunition=initial,
                  final_ammunition={role: ammo(role) for role in evidence}, impacts=hits,
                  corpse_items=corpse["container_count"], looted_player_items=looted_count,
                  relay=asdict(relay.stop()))
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
