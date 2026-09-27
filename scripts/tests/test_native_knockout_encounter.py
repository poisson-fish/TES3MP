import copy
import unittest

from scripts.native_knockout_encounter import (
    established_samples, validate_observations, validate_physical_observations, validate_retarget_observations)


class KnockoutEvidenceTests(unittest.TestCase):
    def test_baseline_trims_startup_but_preserves_later_missing_peers(self):
        joining = dict(self=2, players=[])
        ready = dict(self=2, players=[dict(id=1)])
        self.assertEqual(established_samples([joining]), [])
        self.assertEqual(established_samples([joining, ready, joining]), [ready, joining])

    def test_retarget_requires_shared_npc_contact_inside_recovery(self):
        segment = {role: [] for role in ("Alice", "Bob")}
        hit = dict(attacker=7, target=2, attacker_revision=30, target_revision=30,
                   hit=False, damage=0, stat=0, died=False)
        for tick, state, frame in ((10, 3, 5), (50, 3, 45), (80, 1, 0)):
            alice = dict(id=1, health=90, fatigue=100, dead=False,
                         knockout=dict(state=state, frame=frame, paralyzed=False))
            bob = dict(id=2, health=100, fatigue=100, dead=False,
                       knockout=dict(state=1, frame=0, paralyzed=False))
            for role, own, peer in (("Alice", alice, bob), ("Bob", bob, alice)):
                row = dict(tick=tick, self=own["id"], players=[copy.deepcopy(peer)], actors=[dict(id=7)],
                           actor_hits=[copy.deepcopy(hit)] if tick == 80 else [], player_hits=[])
                row.update({k: copy.deepcopy(v) for k, v in own.items() if k != "id"})
                segment[role].append(row)
        self.assertEqual(validate_retarget_observations(segment)["contacts"], [hit])
        for mutation in ("before", "after", "missing", "duplicate", "disagree", "wrong_npc", "gap"):
            changed = copy.deepcopy(segment)
            if mutation == "missing":
                changed["Bob"][-1]["actor_hits"] = []
            elif mutation == "duplicate":
                changed["Bob"][1]["actor_hits"] = [copy.deepcopy(hit)]
            elif mutation == "disagree":
                changed["Bob"][-1]["actor_hits"][0]["damage"] = 1
            else:
                for rows in changed.values():
                    event = rows[-1]["actor_hits"][0]
                    if mutation == "wrong_npc":
                        event["attacker"] = 8
                    else:
                        event["attacker_revision"] = {"before": 9, "after": 51, "gap": 90}[mutation]
                    if mutation == "gap":
                        second = copy.deepcopy(rows[:2])
                        for row in second:
                            row["tick"] += 100
                        rows.extend(second + [dict(copy.deepcopy(rows[-1]), tick=180, actor_hits=[])])
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):
                validate_retarget_observations(changed)

    def test_player_physical_compares_self_and_remote_and_deduplicates_npc_hits(self):
        hit = dict(attacker=7, target=1, attacker_revision=2, target_revision=3,
                   hit=True, damage=12, stat=0, died=False)
        segment = {role: [] for role in ("Alice", "Bob")}
        for tick, state, frame in ((1, 3, 10), (2, 3, 55), (3, 1, 0)):
            target = dict(id=1, health=88, fatigue=100, dead=False,
                          knockout=dict(state=state, frame=frame, paralyzed=False))
            observer = dict(id=2, health=100, fatigue=100, dead=False,
                            knockout=dict(state=1, frame=0, paralyzed=False))
            for role, own, peer in (("Alice", target, observer), ("Bob", observer, target)):
                row = dict(tick=tick, self=own["id"], players=[copy.deepcopy(peer)],
                           actor_hits=[copy.deepcopy(hit)] if tick == 1 else [], player_hits=[])
                row.update({k: copy.deepcopy(v) for k, v in own.items() if k != "id"})
                segment[role].append(row)
        valid = [segment]
        self.assertEqual(validate_physical_observations(valid, player_id=1)["matching_health_hits"], 1)
        restored = copy.deepcopy(valid)
        for rows in restored[0].values():
            rows[0]["actor_hits"] = []
        validate_physical_observations(restored, require_hit=False, player_id=1)
        for mutation in ("duplicate", "missing_target", "wrong_target", "remote_health", "self_dead",
                         "self_rewind", "no_recovery", "no_hit", "player_hit"):
            changed = copy.deepcopy(valid)
            alice, bob = changed[0]["Alice"], changed[0]["Bob"]
            if mutation == "duplicate":
                bob[1]["actor_hits"] = [copy.deepcopy(hit)]
            elif mutation == "missing_target":
                bob[1]["players"] = []
            elif mutation == "wrong_target":
                for rows in changed[0].values():
                    rows[0]["actor_hits"][0]["target"] = 2
            elif mutation == "remote_health":
                bob[1]["players"][0]["health"] += 1
            elif mutation == "self_dead":
                alice[1]["dead"] = True
            elif mutation == "self_rewind":
                alice[1]["knockout"]["frame"] = 1
            elif mutation == "no_recovery":
                alice.pop()
            else:
                for rows in changed[0].values():
                    rows[0]["actor_hits"] = []
                    if mutation == "player_hit":
                        rows[0]["player_hits"] = [copy.deepcopy(hit)]
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):
                validate_physical_observations(changed, player_id=1)

    def test_physical_requires_health_hit_progress_recovery_and_consistent_observers(self):
        hit = dict(attacker=1, target=7, attacker_revision=2, target_revision=3,
                   hit=True, damage=12, stat=0, died=False)
        rows = [dict(tick=tick, player_hits=[hit] if tick == 1 else [],
                     actors=[dict(id=7, health=100, fatigue=100, dead=False,
                                  knockout=dict(state=state, frame=frame, paralyzed=False))])
                for tick, state, frame in ((1, 3, 10), (2, 3, 50), (3, 1, 0))]
        valid = [{"Alice": rows, "Bob": copy.deepcopy(rows)}]
        self.assertEqual(validate_physical_observations(valid)["matching_health_hits"], 1)
        restored = copy.deepcopy(valid)
        for role in restored[0]:
            restored[0][role][0]["player_hits"] = []
        validate_physical_observations(restored, require_hit=False)
        with self.assertRaises(RuntimeError):
            validate_physical_observations(restored)
        for mutation in ("missing", "dead", "fatigue", "paralyzed", "divergence", "frozen", "rewound",
                         "no_recovery", "duplicate", "wrong_stat", "wrong_target", "wrong_outcome"):
            changed = copy.deepcopy(valid)
            bob = changed[0]["Bob"]
            actor = bob[1]["actors"][0]
            if mutation == "missing":
                changed[0]["Bob"] = []
            elif mutation == "dead":
                actor["dead"] = True
            elif mutation == "fatigue":
                actor["fatigue"] = -1
            elif mutation == "paralyzed":
                actor["knockout"]["paralyzed"] = True
            elif mutation == "divergence":
                actor["knockout"]["frame"] += 1
            elif mutation == "frozen":
                for role in changed[0]:
                    changed[0][role][1]["actors"][0]["knockout"]["frame"] = 10
            elif mutation == "rewound":
                for role in changed[0]:
                    reset = copy.deepcopy(changed[0][role][1])
                    reset["actors"][0]["knockout"]["frame"] = 1
                    changed[0][role].insert(2, reset)
            elif mutation == "no_recovery":
                bob.pop()
            elif mutation == "duplicate":
                bob[1]["player_hits"] = [copy.deepcopy(hit)]
            elif mutation in ("wrong_stat", "wrong_target"):
                for role in changed[0]:
                    changed[0][role][0]["player_hits"][0]["stat" if mutation == "wrong_stat" else "target"] = 2
            else:
                bob[0]["player_hits"][0]["damage"] += 1
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):
                validate_physical_observations(changed)

    def test_npc_recovery_requires_both_observers_and_stable_living_actor(self):
        rows = [dict(tick=tick, actors=[dict(id=7, fatigue=fatigue, dead=False,
                    knockout=dict(state=state, frame=frame, paralyzed=False))])
                for tick, state, frame, fatigue in ((1, 2, 30, -10), (2, 2, 75, 10), (3, 1, 0, 10))]
        valid = [{"Alice": rows, "Bob": copy.deepcopy(rows)}]
        self.assertEqual(validate_observations(valid, npc=True)["shared_ticks"], 3)
        for mutation in ("missing", "dead", "identity", "divergent", "no_tail", "no_recovery"):
            changed = copy.deepcopy(valid)
            actor = changed[0]["Bob"][1]["actors"][0]
            if mutation == "missing":
                changed[0]["Bob"] = []
            elif mutation == "dead":
                actor["dead"] = True
            elif mutation == "identity":
                actor["id"] = 8
            elif mutation == "divergent":
                actor["knockout"]["frame"] += 1
            elif mutation == "no_tail":
                changed[0]["Bob"].pop(1)
            else:
                changed[0]["Bob"].pop()
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):
                validate_observations(changed, npc=True)

    def test_rejects_false_recovery_and_divergence(self):
        rows = []
        for tick, state, frame, fatigue in ((1, 1, 0, 100), (2, 2, 20, -100),
                                          (3, 2, 10, -100), (4, 2, 45, 100), (5, 1, 0, 100)):
            pose = dict(state=state, frame=frame, paralyzed=False)
            rows.append(dict(tick=tick, self=1, fatigue=fatigue, knockout=pose,
                             players=[dict(id=2, fatigue=fatigue, knockout=copy.deepcopy(pose))]))
        valid = [{"Alice": rows, "Bob": copy.deepcopy(rows)}]
        self.assertEqual(validate_observations(valid)["shared_ticks"], 10)
        disjoint = copy.deepcopy(valid)
        for row in disjoint[0]["Bob"]:
            row["tick"] += 100
        self.assertEqual(validate_observations(disjoint)["shared_ticks"], 0)
        cases = []
        changed = copy.deepcopy(valid)
        changed[0]["Bob"][1]["knockout"]["frame"] += 1
        cases.append(changed)
        changed = copy.deepcopy(valid)
        changed[0]["Bob"].pop(3)  # No observed authored tail after fatigue returns.
        cases.append(changed)
        changed = copy.deepcopy(valid)
        for role in changed[0]:
            changed[0][role].pop()  # Initial upright must not stand in for recovery.
        cases.append(changed)
        changed = copy.deepcopy(valid)
        for role in changed[0]:
            changed[0][role][-1]["knockout"]["state"] = 0  # Authority disabled is not upright.
        cases.append(changed)
        for index, case in enumerate(cases):
            with self.subTest(index=index), self.assertRaises(RuntimeError):
                validate_observations(case)
