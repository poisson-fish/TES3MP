import copy
import unittest

from scripts.native_knockout_encounter import validate_observations


class KnockoutEvidenceTests(unittest.TestCase):
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
