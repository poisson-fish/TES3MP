import unittest

from scripts import run_phase7_join_demo as demo


class Phase7JoinDemoTests(unittest.TestCase):
    def test_bounded_rss_accepts_noise_and_rejects_sustained_growth(self) -> None:
        stable = [100] * 20 + [100, 104] * 5 + [102] * 10 + [104] * 10
        evidence = demo.bounded_rss(stable)
        self.assertEqual(evidence["samples"], 50)
        self.assertEqual(evidence["peak_bytes"], 104)

        growing = [100] * 20 + [100, 104] * 5 + [110] * 10 + [120] * 10
        with self.assertRaisesRegex(RuntimeError, "sustained RSS growth"):
            demo.bounded_rss(growing)

    def test_bounded_rss_requires_enough_samples(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "insufficient RSS samples"):
            demo.bounded_rss([100] * 39)


if __name__ == "__main__":
    unittest.main()
