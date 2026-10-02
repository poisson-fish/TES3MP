import json
from pathlib import Path
import tempfile
import unittest
import sys
from unittest.mock import patch

from scripts.verify_actor_presentation import verify


class ActorPresentationEvidenceTests(unittest.TestCase):
    def test_capture_reader_preserves_flushed_tail_and_restarts(self):
        with patch.object(sys, "path", [str(Path(__file__).resolve().parents[1]), *sys.path]):
            from scripts.run_native_navigation_capture import records
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "Alice.ndjson"
            path.write_bytes(b'{"tick":1}\n{"tick":')
            self.assertEqual(records(path), [{"tick": 1}])
            with path.open("ab") as stream:
                stream.write(b'2}\n')
            self.assertEqual(records(path), [{"tick": 1}, {"tick": 2}])
            self.assertEqual(records(path), [{"tick": 1}, {"tick": 2}])
            path.rename(path.with_name("Alice-before.ndjson"))
            path.write_bytes(b'{"tick":3}\n')
            self.assertEqual(records(path), [{"tick": 3}])
            path.write_bytes(b'{}\n')
            self.assertEqual(records(path), [{}])

    def test_cast_clock_requires_fractional_release_recovery_and_restart(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def write(speed=1, stepped=False, players=False, missing=False):
                rows = []
                for i in range(240):
                    frame = i // 4 if stepped else i / 4
                    pose = dict(kind=2, id=9, tick=i / 4, life=1, action=0, phase=0,
                                body_action=0, body=1, group="", frame=0,
                                cast=1, cast_phase=3 if frame < 30 else 5,
                                cast_frame=frame, cast_release=30, cast_stop=60,
                                clip_time=(frame if frame < 30 else frame - 30) / 30 * speed)
                    rows.extend([dict(event="native_combat_sample", tick=i),
                                 dict(event="actor_presentation_frame", time_ns=round((i + 1) * 1e9 / 120),
                                      actors=([dict(pose, kind=1, id=1)] + ([] if missing else [dict(pose, kind=1, id=2)]))
                                      if players else [pose])])
                for name in ("Alice-before", "Bob-before", "Alice", "Bob"):
                    root.joinpath(name + ".ndjson").write_text("\n".join(map(json.dumps, rows)))
            write()
            self.assertEqual(len(verify(root, casting=True)), 4)
            write(players=True)
            self.assertEqual(len(verify(root, player_casting=True)), 4)
            write(players=True, missing=True)
            with self.assertRaisesRegex(ValueError, "player 2"):
                verify(root, player_casting=True)
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
