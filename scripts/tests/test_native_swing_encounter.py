import copy
import unittest

from scripts.native_swing_encounter import validate_observations


class SwingEvidenceTests(unittest.TestCase):
    def observations(self):
        rows = []
        for command in (1, 2):
            for phase in (1, 2, 3, 4):
                rows.append(dict(tick=command * 10 + phase, player_hits=[], swings=[
                    dict(player=p, command=command, source=p, target_life=1,
                         direction=0, strength=.6, group="weapononehand", phase=phase,
                         completion=.5, interruption=0) for p in (1, 2)]))
        rows[-1]["player_hits"] = [dict(attacker=1, attacker_revision=20, target_revision=21,
                                       hit=True, damage=3)]
        return [{"Alice": rows, "Bob": copy.deepcopy(rows)}]

    def test_rejects_false_convergence(self):
        valid = self.observations()
        self.assertEqual(validate_observations(valid)["shared_identities"], 4)
        cases = []
        changed = copy.deepcopy(valid)
        changed[0]["Bob"][1]["swings"][0]["strength"] = .8
        cases.append(changed)
        changed = copy.deepcopy(valid)
        changed[0]["Bob"][1]["swings"][0]["completion"] = .1
        cases.append(changed)
        changed = copy.deepcopy(valid)
        changed[0]["Bob"] = [r for r in changed[0]["Bob"] if r["swings"][0]["phase"] != 2]
        cases.append(changed)
        changed = copy.deepcopy(valid)
        changed[0]["Bob"][-1]["player_hits"].clear()
        cases.append(changed)
        # Duplicate durable events in a new process must also fail.
        cases.append(valid + copy.deepcopy(valid))
        for index, case in enumerate(cases):
            with self.subTest(index=index), self.assertRaises(RuntimeError):
                validate_observations(case)

    def test_allows_disjoint_latest_wins_ticks(self):
        valid = self.observations()
        for row in valid[0]["Bob"]:
            row["tick"] += .5
            for pose in row["swings"]:
                pose["completion"] = .7
        self.assertEqual(validate_observations(valid)["shared_identities"], 4)


if __name__ == "__main__":
    unittest.main()
