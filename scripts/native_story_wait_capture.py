"""Two rendered clients exercise scheduled stock health/OnDeath, waits and reservations."""
import math
import time


def verify_waits(result, previous, evidence, command, wait, view, screenshot, latest, fair):
    roles = ("Alice", "Bob")
    wait(lambda: all(view(r, 91) for r in roles), "both source story baselines")
    for role in roles:
        command(role, "close")
    npc = wait(lambda: latest("Bob", "native_combat_sample")["actors"], "canonical source combat body")[0]["id"]

    def body(role):
        return next(a for a in latest(role, "native_combat_sample")["actors"] if a["id"] == npc)

    def status(role):
        return view(role, 91).get("encounter")

    def face(role):
        p = [v / 1024 for v in latest(role, "native_player_sample")["position"]]
        actor = latest(role, "native_actor_sample")
        yaw = math.atan2(actor["x"] - p[0], actor["y"] - p[1])
        command(role, "pose " + " ".join(map(str, (*p, .1, yaw))))

    def kill(role):
        face(role)
        deadline = time.monotonic() + 45
        attempts = 0
        while not all(body(r)["dead"] for r in roles):
            if time.monotonic() > deadline:
                raise RuntimeError(role + " could not kill the canonical source")
            command(role, f"swingat 0 1 {npc}")
            attempts += 1
            time.sleep(.35)
        result["checks"].append(dict(killer=role, shared_death=True, attempts=attempts))

    def panel(role, waiting, reserved, label):
        command(role, "journal")
        time.sleep(.15)
        widget = command(role, "encounterstatus")
        text = widget["text"]
        if widget["visible"] != (waiting or reserved) or (waiting and "no story credit" not in text) \
                or (reserved and "next encounter is reserved" not in text) \
                or (waiting and "Expected return" not in text and "not yet known" not in text):
            raise RuntimeError("Journal status disagrees with the committed dependency: " + repr(widget))
        screenshot(role, label)
        result["checks"].append(dict(role=role, phase=label, journal_widget=widget))
        command(role, "close")

    def reconnect(role):
        old = view(role, 91)
        start = len(evidence.rows(role, "personal_dialogue_presented"))
        count = len(evidence.rows(role, "phase8_desktop_status"))
        command(role, "disconnectbrief")
        wait(lambda: any(r.get("status") == "resumed"
            for r in evidence.rows(role, "phase8_desktop_status")[count:]), role + " resumed", 60)
        wait(lambda: view(role, 91)["generation"] > old["generation"], role + " recovered source story")
        rows = evidence.rows(role, "personal_dialogue_presented")[start:]
        if any(r["response"] or r["messages"] or any(r[k] != old[k]
                for k in ("revision", "quests", "choices", "command", "encounter")) for r in rows):
            raise RuntimeError("Source recovery changed progression/dependency or replayed notices")
        result["checks"].append(dict(role=role, reconnect=True, silent=True, status_retained=True))

    def credit(role, expected):
        command(role, 'dialoguestart "npc_door_actor"')
        old = view(role, 91)["revision"]
        command(role, 'dialoguetopic "north_passage_91"')
        current = wait(lambda: v if (v := view(role, 91))["revision"] > old else None,
                       role + " authenticated stock credit response")
        if current["response"] != f"Credited encounters: {expected}.":
            raise RuntimeError("Stock OnDeath credit was missing, foreign or duplicated: " + repr(current))
        screenshot(role, "credit-" + str(expected))
        command(role, "dialogueclose")
        result["checks"].append(dict(role=role, attributed_credit=expected, response=current["response"]))

    if previous:
        if previous.get("fair_opportunities") != fair:
            raise ValueError("Recovery policy differs from prior evidence")
        for role in roles:
            old = previous["final_views"][role]["91"]
            rows = evidence.rows(role, "personal_dialogue_presented")
            if any(r["response"] or r["messages"] or any(r[k] != old[k]
                    for k in ("revision", "quests", "choices", "command", "encounter")) for r in rows):
                raise RuntimeError("Restart replayed notices or reset the source story")
        result["checks"].append(dict(restart=True, silent=True, status_retained=True))
    phase = previous["phase"] if previous else "initial"
    if phase == "initial":
        kill("Alice")
        wait(lambda: status("Bob") and status("Bob")["wait_since"], "foreign-health durable wait")
        if status("Alice") or bool(status("Bob")["reserved_life"]) != fair:
            raise RuntimeError("Own death became a wait or the next-life policy differs")
        panel("Bob", True, fair, "waiting")
        reconnect("Bob")
        panel("Bob", True, fair, "waiting-reconnect")
        result["phase"] = "waiting"
    elif phase == "waiting":
        panel("Bob", True, fair, "waiting-restart")
        wait(lambda: all(not body(r)["dead"] for r in roles) and
             (not status("Bob") or not status("Bob")["wait_since"]), "new canonical source life", 60)
        credit("Alice", 1)
        credit("Bob", 0)
        if fair:
            if not status("Bob") or status("Bob")["reserved_life"] != 2:
                raise RuntimeError("Wake released B's unearned opportunity")
            before = body("Bob")["health"]
            face("Alice")
            for _ in range(10):
                command("Alice", f"swingat 0 1 {npc}")
                time.sleep(.35)
            if any(body(r)["dead"] or body(r)["health"] != before for r in roles):
                raise RuntimeError("Repeated foreign attacks consumed B's reserved life")
            reconnect("Bob")
            panel("Bob", False, True, "reserved-reconnect")
            result["checks"].append(dict(repeated_foreign_attacks=10, reserved_life=2, unchanged_health=before))
        result["phase"] = "reserved" if fair else "available"
    elif phase in ("reserved", "available"):
        if fair:
            panel("Bob", False, True, "reserved-restart")
        kill("Bob")
        wait(lambda: not status("Bob"), "eligible death releases committed reservation")
        panel("Bob", False, False, "earned")
        wait(lambda: all(not body(r)["dead"] for r in roles), "second canonical source respawn", 60)
        credit("Alice", 1)
        credit("Bob", 1)
        reconnect("Bob")
        result["phase"] = "completed"
    elif phase == "completed":
        for role in roles:
            reserved = bool(status(role) and status(role)["reserved_life"])
            if role == "Bob" and reserved:
                raise RuntimeError("Restart restored B's already spent reservation")
            panel(role, False, reserved, "completed-restart")
            credit(role, 1)
        result["phase"] = "verified"
    else:
        raise ValueError("Unknown source recovery phase")
    result["final_views"] = {r: {91: view(r, 91)} for r in roles}
