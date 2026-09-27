import copy
import unittest

from scripts.native_knockout_encounter import validate_observations, validate_physical_observations


class KnockoutEvidenceTests(unittest.TestCase):
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
