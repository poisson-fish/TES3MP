"""Rendered bounded source Disable/return/Enable and silent recovery checks."""
import math
import time


def verify_encounter(result, previous, evidence, command, wait, view, screenshot, latest, return_path=False):
    roles = ("Alice", "Bob")
    npc = "npc_door_actor"
    result["returned"] = bool(previous and previous.get("returned"))
    def private(role):
        return role == "Alice" and not result["returned"]
    wait(lambda: all(view(r, s) for r in roles for s in (91, 92)), "four encounter story baselines")
    frame = wait(lambda: latest("Bob", "native_actor_sample"), "public NPC position")
    x, y, z = (frame[k] for k in ("x", "y", "z"))

    def position(role):
        return [v / 1024 for v in latest(role, "native_player_sample")["position"]]

    def stopped(role):
        rows = evidence.rows(role, "native_player_sample")[-8:]
        return len(rows) == 8 and all(math.dist(r["position"], rows[-1]["position"]) <= 8 for r in rows)

    def face(role, target):
        p = position(role)
        yaw = math.atan2(target[0] - p[0], target[1] - p[1])
        command(role, "pose " + " ".join(map(str, (*p, .1, yaw))))

    def probe(role, removed, label):
        # A short stock physics/render ray through the NPC's torso, clear of room geometry.
        row = command(role, f"actorprobe {x-45} {y} {z+64} {x+45} {y} {z+64}",
                      "traversal_collision_probe")
        expected = not removed
        if row["physics_hit"] != expected or row["projectile_hit"] != expected or row["rendered_actors"] != int(expected):
            raise RuntimeError(f"{role} visibility/collision disagrees at {label}: {row}")
        face(role, (x, y))
        screenshot(role, label)
        result["checks"].append(dict(role=role, phase=label, removed=removed,
                                     rendered_npc=expected, collision_npc=expected, probe=row))

    def recover(role):
        old = {s: view(role, s) for s in (91, 92)}
        starts = {s: len(evidence.rows(role, "personal_dialogue_presented")) for s in old}
        status_count = len(evidence.rows(role, "phase8_desktop_status"))
        command(role, "disconnectbrief")
        wait(lambda: any(r.get("status") == "resumed"
                         for r in evidence.rows(role, "phase8_desktop_status")[status_count:]), role + " resumed", 60)
        wait(lambda: all(view(role, s)["generation"] > old[s]["generation"] for s in old),
             role + " recovered encounter banks")
        sample_start = len(evidence.rows(role, "native_player_sample"))
        wait(lambda: len(evidence.rows(role, "native_player_sample")) >= sample_start + 8,
             role + " recovered live prediction bodies")
        for s in old:
            recovered = evidence.rows(role, "personal_dialogue_presented")[starts[s]:]
            for row in recovered:
                if row["story"] != s:
                    continue
                if row["response"] or row["messages"] or any(row[k] != old[s][k]
                        for k in ("revision", "quests", "choices", "command")):
                    raise RuntimeError("Recovery changed progression or replayed notifications")
        wait(lambda: stopped(role), role + " stopped after resume")
        probe(role, private(role), "reconnect")
        result["checks"].append(dict(role=role, reconnect=True, silent=True))

    for role in roles:
        command(role, "close")
    wait(lambda: all(stopped(r) for r in roles), "both encounter players settled")
    if previous:
        for role in roles:
            for s in (91, 92):
                old = previous["final_views"][role][str(s)]
                for row in evidence.rows(role, "personal_dialogue_presented"):
                    if row["story"] == s and (row["response"] or row["messages"]
                            or any(row[k] != old[k] for k in ("revision", "quests", "choices", "command"))):
                        raise RuntimeError("Restart reset story state or replayed notifications")
            probe(role, private(role), "restart")
        result["checks"].append(dict(restart=True, silent=True, independent_views=True))
    else:
        for role in roles:
            probe(role, False, "before-removal")
        command("Alice", f'dialoguestart "{npc}"')
        command("Alice", 'dialoguetopic "north_passage_91 (story 91)"')
        wait(lambda: view("Alice", 91)["revision"] == 1, "A offered private completion")
        command("Alice", "dialoguechoice 7")
        wait(lambda: view("Alice", 91)["revision"] == 2 and view("Alice", 91)["quests"][0]["finished"],
             "A completed stock Journal/Disable")
        command("Alice", "close")
        sample_start = len(evidence.rows("Alice", "native_player_sample"))
        if view("Bob", 91)["quests"] or view("Bob", 91)["revision"]:
            raise RuntimeError("A consumed B's unfinished story")
        wait(lambda: all(len(latest(r, "native_player_sample")["players"]) == 1 for r in roles),
             "scope separation removed peers")
        wait(lambda: len(evidence.rows("Alice", "native_player_sample")) >= sample_start + 8,
             "new scope installed in live prediction")
        for role in roles:
            probe(role, role == "Alice", "after-removal")

        # B approaches and stops at the retained public hull; A follows the same
        # route beyond the disabled placement using committed server physics.
        for role in ("Bob", "Alice"):
            face(role, (x, y))
            start = position(role)
            direction = (x-start[0], y-start[1])
            length = math.hypot(*direction)
            unit = [v / length for v in direction]
            command(role, "autowalk")
            if role == "Bob":
                wait(lambda: math.dist(position(role)[:2], (x, y)) < 70, "B reached retained NPC hull")
                time.sleep(1)
            else:
                wait(lambda: sum((position(role)[i] - (x, y)[i]) * unit[i] for i in (0, 1)) > 45,
                     "A walked through removed NPC position", 15)
            command(role, "autowalk")
            wait(lambda: stopped(role), role + " movement stopped")
            distance = math.dist(position(role)[:2], (x, y))
            if role == "Bob" and not 20 < distance < 70:
                raise RuntimeError("B's retained NPC did not block authoritative movement")
            result["checks"].append(dict(role=role, movement_collision=True, position=position(role),
                                         npc_position=[x, y, z], distance=distance))
        # B can still execute the unchanged stock topic after A has finished.
        command("Bob", f'dialoguestart "{npc}"')
        command("Bob", 'dialoguetopic "north_passage_91 (story 91)"')
        wait(lambda: view("Bob", 91)["revision"] == 1 and len(view("Bob", 91)["choices"]) == 2,
             "B retained unfinished opportunity")
        command("Bob", "close")
    for role in roles:
        recover(role)
    if return_path and not previous:
        completed = view("Alice", 91)
        p = position("Alice")
        face("Alice", (p[0] + 10 * (p[0]-x), p[1] + 10 * (p[1]-y)))
        command("Alice", "autowalk")
        wait(lambda: all(len(latest(r, "native_player_sample")["players"]) == 2 for r in roles),
             "A crossed safe boundary and rejoined B", 15)
        command("Alice", "autowalk")
        result["returned"] = True
        sample_start = len(evidence.rows("Alice", "native_player_sample"))
        wait(lambda: len(evidence.rows("Alice", "native_player_sample")) >= sample_start + 8
             and stopped("Alice"), "shared prediction and movement stopped")
        if any(view("Alice", 91)[k] != completed[k] for k in ("revision", "quests", "choices", "command")):
            raise RuntimeError("Return changed A's completed story")
        if view("Bob", 91)["quests"][0]["finished"] or len(view("Bob", 91)["choices"]) != 2:
            raise RuntimeError("Return consumed B's unfinished opportunity")
        for role in roles:
            probe(role, False, "shared-return")
        result["checks"].append(dict(returned=True, shared_peers=True, A_completed=True, B_unfinished=True,
                                     A_position=position("Alice"), NPC_position=[x, y, z]))
        for role in roles:
            recover(role)
    for role in roles:
        transients = [v for v in evidence.rows(role, "personal_dialogue_presented") if v["response"] or v["messages"]]
        expected = 0 if previous else 2 if role == "Alice" else 1
        if len(transients) != expected or len({(v["story"], v["revision"]) for v in transients}) != expected:
            raise RuntimeError("Repeated or missing encounter notification")
    result["final_views"] = {r: {s: view(r, s) for s in (91, 92)} for r in roles}
