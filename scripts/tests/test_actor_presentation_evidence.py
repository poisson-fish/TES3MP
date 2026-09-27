import json
from pathlib import Path
import tempfile
import unittest

from scripts.verify_actor_presentation import verify


class ActorPresentationEvidenceTests(unittest.TestCase):
    def test_cast_clock_requires_fractional_release_recovery_and_restart(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def write(speed=1, stepped=False):
                rows = []
                for i in range(240):
                    frame = i // 4 if stepped else i / 4
                    pose = dict(kind=2, id=9, tick=i / 4, life=1, action=0, phase=0,
                                body_action=0, body=1, group="", frame=0,
                                cast=1, cast_phase=3 if frame < 30 else 5,
                                cast_frame=frame, cast_release=30, cast_stop=60,
                                clip_time=frame / 30 * speed)
                    rows.extend([dict(event="native_combat_sample", tick=i),
                                 dict(event="actor_presentation_frame", time_ns=round((i + 1) * 1e9 / 120),
                                      actors=[pose])])
                for name in ("Alice-before", "Bob-before", "Alice", "Bob"):
                    root.joinpath(name + ".ndjson").write_text("\n".join(map(json.dumps, rows)))
            write()
            self.assertEqual(len(verify(root, casting=True)), 4)
            write(speed=2)
            with self.assertRaisesRegex(ValueError, "stock speed"):
                verify(root, casting=True)
            write(stepped=True)
            with self.assertRaisesRegex(ValueError, "fractional cast"):
                verify(root, casting=True)
            write()
            root.joinpath("Bob-before.ndjson").unlink()
            with self.assertRaisesRegex(ValueError, "before and after restart"):
                verify(root, casting=True)

    def test_actor_speed_uses_native_weapon_and_requires_both_restart_segments(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            content = dict(custom="authored-body", **{"melee-speed": 1.35})
            root.joinpath("result.json").write_text(json.dumps(dict(content=content)))
            rows = []
            for i in range(240):
                body = i >= 120
                pose = dict(kind=2, id=9, tick=i / 4, life=1, action=1, phase=1,
                            body_action=int(body), body=2 if body else 1,
                            group="knockout" if body else "weapononehand", frame=i / 4,
                            clip_time=i / 120 * (1 if body else 1.35))
                rows.extend([dict(event="native_combat_sample", tick=i),
                             dict(event="actor_presentation_frame", time_ns=round((i + 1) * 1e9 / 120),
                                  actors=[pose])])
            for name in ("Alice-before", "Bob-before", "Alice", "Bob"):
                root.joinpath(name + ".ndjson").write_text("\n".join(map(json.dumps, rows)))
            self.assertEqual(len(verify(root, actor=True)), 4)
            for speed in (1, 2):
                content["melee-speed"] = speed
                root.joinpath("result.json").write_text(json.dumps(dict(content=content)))
                with self.assertRaisesRegex(ValueError, "stock speed"):
                    verify(root, actor=True)
            content["melee-speed"] = 1.35
            root.joinpath("result.json").write_text(json.dumps(dict(content=content)))
            root.joinpath("Bob-before.ndjson").unlink()
            with self.assertRaisesRegex(ValueError, "before and after restart"):
                verify(root, actor=True)
