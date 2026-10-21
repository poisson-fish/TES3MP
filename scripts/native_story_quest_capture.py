"""Two rendered clients exercise M5 Seyda Neen and Tamriel Rebuilt quest banks, item provenance, and restart continuation."""
import time


def verify_quests(result, previous, evidence, command, wait, view, screenshot, latest):
    roles = ("Alice", "Bob")
    npc = "npc_door_actor"
    wait(lambda: all(view(r, 91) for r in roles), "both quest story baselines")
    wait(lambda: all(latest(r, "traversal_inventory") for r in roles), "both initial inventory baselines")
    for role in roles:
        command(role, "close")

    def inv_summary(role):
        row = latest(role, "traversal_inventory")
        if not row:
            return None
        stacks = row.get("player", [])
        return dict(
            revision=row.get("inventory_revision", 0),
            stacks=len(stacks),
            total_count=sum(s.get("count", 0) for s in stacks),
        )

    def quest_map(role):
        v = view(role, 91)
        if not v:
            return {}
        return {q["topic"].lower(): (q["index"], bool(q["finished"])) for q in v.get("quests", [])}

    def has_quests(role, expected):
        qm = quest_map(role)
        return all(qm.get(k.lower()) == val for k, val in expected.items())

    def reconnect(role, label):
        old = view(role, 91)
        old_inv = inv_summary(role)
        start = len(evidence.rows(role, "personal_dialogue_presented"))
        count = len(evidence.rows(role, "phase8_desktop_status"))
        command(role, "disconnectbrief")
        wait(lambda: any(r.get("status") == "resumed"
             for r in evidence.rows(role, "phase8_desktop_status")[count:]), role + " resumed", 60)
        wait(lambda: view(role, 91)["generation"] > old["generation"], role + " recovered quest story")
        rows = evidence.rows(role, "personal_dialogue_presented")[start:]
        if any(r["response"] or r["messages"] or any(r[k] != old[k]
                for k in ("revision", "quests", "choices", "command")) for r in rows):
            raise RuntimeError("Quest recovery changed progression or replayed notices: " + role)
        new_inv = inv_summary(role)
        if new_inv["stacks"] != old_inv["stacks"] or new_inv["total_count"] != old_inv["total_count"]:
            raise RuntimeError(f"Quest recovery altered inventory for {role}: {old_inv} -> {new_inv}")
        result["checks"].append(dict(
            role=role, phase=label, reconnect=True, silent=True,
            quests=quest_map(role), inventory=new_inv,
        ))

    if previous:
        for role in roles:
            old = previous["final_views"][role]["91"]
            rows = evidence.rows(role, "personal_dialogue_presented")
            if any(r["response"] or r["messages"] or any(r[k] != old[k]
                    for k in ("revision", "quests", "choices", "command")) for r in rows):
                raise RuntimeError("Restart replayed notices or lost quest state for " + role)
        if not has_quests("Alice", {
            "ms_fargothring": (100, True),
            "ms_nuccius": (100, True),
            "mv_deadtaxman": (200, False),
            "tr_m1_gs_mq_4": (50, True),
        }):
            raise RuntimeError("Alice lost completed Seyda Neen / TR quests across restart")
        if inv_summary("Alice")["total_count"] != 202 or inv_summary("Bob")["total_count"] != 33:
            raise RuntimeError(f"Restart altered committed item balances: Alice={inv_summary('Alice')} Bob={inv_summary('Bob')}")
        result["checks"].append(dict(
            restart=True, silent=True,
            alice_inventory=inv_summary("Alice"), bob_inventory=inv_summary("Bob"),
        ))

        # Bob resumes his pending Nuccius / Taxman choices from before server restart, then completes TR Duel of Riddles.
        command("Bob", f'dialoguestart "{npc}"')
        command("Bob", "dialoguechoice 3")
        wait(lambda: has_quests("Bob", {"ms_fargothring": (100, True), "ms_nuccius": (100, True)})
             and inv_summary("Bob")["total_count"] == 4,
             "Bob completed MS_Nuccius after restart")
        command("Bob", "dialoguechoice 4")
        wait(lambda: has_quests("Bob", {
            "ms_fargothring": (100, True),
            "ms_nuccius": (100, True),
            "mv_deadtaxman": (200, False),
        }) and inv_summary("Bob")["total_count"] == 202,
             "Bob completed MV_DeadTaxman after restart")

        command("Bob", 'dialoguetopic "puzzling riddles"')
        wait(lambda: len(view("Bob", 91)["choices"]) == 2, "Bob opened TR Duel of Riddles after restart")
        for choice, stage in ((1, 10), (4, 20), (8, 50)):
            command("Bob", f"dialoguechoice {choice}")
            wait(lambda: has_quests("Bob", {"tr_m1_gs_mq_4": (stage, stage == 50)}),
                 f"Bob advanced TR_M1_GS_MQ_4 to {stage}")

        command("Bob", "dialogueclose")
        for role in roles:
            command(role, "journal")
            time.sleep(.15)
            screenshot(role, "quests-restart-journal")
            command(role, "close")
            reconnect(role, "post-restart-reconnect")

        result["checks"].append(dict(
            bob_completed_after_restart=True,
            alice_quests=quest_map("Alice"),
            bob_quests=quest_map("Bob"),
            alice_inventory=inv_summary("Alice"),
            bob_inventory=inv_summary("Bob"),
        ))
    else:
        for role in roles:
            if inv_summary(role)["total_count"] != 1 or inv_summary(role)["stacks"] != 1:
                raise RuntimeError(f"Unexpected starting inventory for {role}: {inv_summary(role)}")

        # Alice opens Seyda Neen quest chain while Bob remains at initial state.
        command("Alice", f'dialoguestart "{npc}"')
        command("Alice", 'dialoguetopic "seyda_neen_quests"')
        wait(lambda: view("Alice", 91)["revision"] == 1 and len(view("Alice", 91)["choices"]) == 2
             and inv_summary("Alice")["total_count"] == 34,
             "Alice offered Fargoth ring choice")
        if view("Bob", 91)["revision"] != 0 or view("Bob", 91)["quests"] or inv_summary("Bob")["total_count"] != 1:
            raise RuntimeError("Alice dialogue leaked to Bob")
        screenshot("Alice", "seyda-fargoth-choice")

        # Concurrent Bob greeting on Seyda Neen quests.
        command("Bob", f'dialoguestart "{npc}"')
        command("Bob", 'dialoguetopic "seyda_neen_quests"')
        wait(lambda: view("Bob", 91)["revision"] == 1 and len(view("Bob", 91)["choices"]) == 2
             and inv_summary("Bob")["total_count"] == 34,
             "Bob concurrently offered Fargoth ring choice")

        # Alice completes MS_FargothRing (100), MS_Nuccius (100), and MV_DeadTaxman (200) with item provenance.
        command("Alice", "dialoguechoice 2")
        wait(lambda: has_quests("Alice", {"ms_fargothring": (100, True)})
             and inv_summary("Alice")["total_count"] == 33,
             "Alice completed MS_FargothRing and transferred ring_keley")
        command("Alice", "dialoguechoice 3")
        wait(lambda: has_quests("Alice", {"ms_fargothring": (100, True), "ms_nuccius": (100, True)})
             and inv_summary("Alice")["total_count"] == 4,
             "Alice completed MS_Nuccius, received ring_nuccius, and paid 30 gold")
        command("Alice", "dialoguechoice 4")
        wait(lambda: has_quests("Alice", {
            "ms_fargothring": (100, True),
            "ms_nuccius": (100, True),
            "mv_deadtaxman": (200, False),
        }) and inv_summary("Alice")["total_count"] == 202,
             "Alice completed MV_DeadTaxman and received 200 gold reward")

        if view("Bob", 91)["quests"] or inv_summary("Bob")["total_count"] != 34:
            raise RuntimeError("Alice Seyda Neen quest completion mutated Bob's quests or inventory")

        # Bob completes MS_FargothRing (100) and leaves MS_Nuccius choice 3 pending across reconnect and restart.
        command("Bob", "dialoguechoice 2")
        wait(lambda: has_quests("Bob", {"ms_fargothring": (100, True)})
             and len(view("Bob", 91)["choices"]) == 1
             and inv_summary("Bob")["total_count"] == 33,
             "Bob completed MS_FargothRing with pending Nuccius choice")
        screenshot("Bob", "pending-nuccius-choice")

        # Alice completes Tamriel Rebuilt Duel of Riddles (TR_M1_GS_MQ_4 -> 10 -> 20 -> 50).
        command("Alice", 'dialoguetopic "puzzling riddles"')
        wait(lambda: len(view("Alice", 91)["choices"]) == 2, "Alice opened TR Duel of Riddles")
        screenshot("Alice", "tr-riddles-choice")
        for choice, stage in ((1, 10), (4, 20), (8, 50)):
            command("Alice", f"dialoguechoice {choice}")
            wait(lambda: has_quests("Alice", {"tr_m1_gs_mq_4": (stage, stage == 50)}),
                 f"Alice advanced TR_M1_GS_MQ_4 to {stage}")

        command("Alice", "dialogueclose")
        command("Alice", "journal")
        time.sleep(.15)
        screenshot("Alice", "quests-journal")
        command("Alice", "close")
        command("Bob", "dialogueclose")

        for role in roles:
            reconnect(role, "initial-reconnect")

        result["checks"].append(dict(
            seyda_neen_quests=True,
            tr_duel_of_riddles=True,
            item_provenance=True,
            alice_quests=quest_map("Alice"),
            bob_quests=quest_map("Bob"),
            alice_inventory=inv_summary("Alice"),
            bob_inventory=inv_summary("Bob"),
        ))

    result["final_views"] = {r: {91: view(r, 91)} for r in roles}

