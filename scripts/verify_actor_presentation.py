"""Check bounded, per-render actor traces from the two-desktop capture harness."""

import argparse
import json
import statistics
from pathlib import Path


def percentile(values, fraction):
    return sorted(values)[min(len(values) - 1, int(len(values) * fraction))]


def verify(directory, actor=False):
    result = {}
    content = json.loads(directory.joinpath("result.json").read_text()).get("content", {}) if actor else {}
    melee_speed = float(content.get("melee-speed", 1))
    if actor and content.get("custom") and "melee-speed" not in content:
        raise ValueError("Custom actor trace requires the native fixture's stock weapon speed")
    for path in sorted(directory.glob("*.ndjson")):
        if not path.name.startswith(("Alice", "Bob")):
            continue
        frames, snapshots, latest = [], [], 0
        for line in path.read_text(encoding="utf-8").splitlines():
            row = json.loads(line)
            if row.get("event") == "native_combat_sample":
                latest = row["tick"]
                snapshots.append(latest)
            if row.get("event") == "actor_presentation_frame":
                if any(p["tick"] > latest + .01 for p in row["actors"]):
                    raise ValueError(f"{path.name}: presentation passed committed history")
                frames.append(row)
        if len(frames) < 120:
            raise ValueError(f"{path.name}: insufficient render frames")
        cadence, body_rates, swing_rates = [], [], []
        fractional, body_samples, swing_samples = 0, 0, 0
        for a, b in zip(frames, frames[1:]):
            seconds = (b["time_ns"] - a["time_ns"]) / 1e9
            if seconds <= 0:
                raise ValueError(f"{path.name}: render time regressed")
            cadence.append(seconds * 1000)
            previous = {(p["kind"], p["id"]): p for p in a["actors"]}
            for p in b["actors"]:
                if actor and p["kind"] != 2:
                    continue
                old = previous.get((p["kind"], p["id"]))
                if not old or any(old[key] != p[key] for key in
                                  ("life", "action", "phase", "body_action", "body", "group")):
                    continue
                delta = p["clip_time"] - old["clip_time"]
                if p["body"] >= 2 and p["frame"] > old["frame"] and old["clip_time"] >= 0:
                    body_samples += 1
                    fractional += abs(p["frame"] - round(p["frame"])) > .001
                    expected = (p["frame"] - old["frame"]) / 30
                    # Trace floats are decimal-rounded; the clip end may clamp.
                    if delta < -.002 or delta > expected + .002:
                        raise ValueError(f"{path.name}: body clip diverged from sampled clock")
                    if delta > .002 and seconds < .05:
                        body_rates.append(delta / seconds)
                elif p["body"] == 1 and 1 <= p["phase"] <= 3 and delta > .002 and seconds < .05:
                    swing_samples += 1
                    swing_rates.append(delta / seconds)
        if statistics.median(cadence) > 1000 / 60 or percentile(cadence, .95) > 1000 / 60:
            raise ValueError(f"{path.name}: capture did not sustain 60-FPS render cadence")
        if body_samples and fractional < 30:
            raise ValueError(f"{path.name}: body poses still stepped at snapshot cadence")
        if body_rates and not .9 <= statistics.median(body_rates) <= 1.1:
            raise ValueError(f"{path.name}: body recovery differs from stock clip speed")
        if actor and (body_samples < 30 or swing_samples < 30 or not swing_rates
                         or not .9 <= statistics.median(swing_rates) / melee_speed <= 1.1):
            raise ValueError(f"{path.name}: insufficient actor body/melee progression at stock speed")
        gaps = [b - a for a, b in zip(snapshots, snapshots[1:]) if b > a]
        result[path.name] = dict(
            frames=len(frames), median_frame_ms=statistics.median(cadence),
            p95_frame_ms=percentile(cadence, .95), maximum_frame_ms=max(cadence),
            median_snapshot_gap_ticks=statistics.median(gaps), p95_snapshot_gap_ticks=percentile(gaps, .95),
            body_samples=body_samples, fractional_body_samples=fractional,
            median_body_speed=statistics.median(body_rates) if body_rates else None,
            swing_samples=swing_samples,
            median_swing_speed=statistics.median(swing_rates) if swing_rates else None,
            stock_weapon_speed=melee_speed if actor else None,
        )
    if not all(any(name.startswith(role) for name in result) for role in ("Alice", "Bob")):
        raise ValueError("Both desktop traces are required")
    if actor and not {"Alice-before.ndjson", "Bob-before.ndjson", "Alice.ndjson", "Bob.ndjson"} <= result.keys():
        raise ValueError("Both desktops require traces before and after restart")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--actor", "--creature", dest="actor", action="store_true", help="Require selected actor body and melee evidence")
    args = parser.parse_args()
    report = verify(args.directory, args.actor)
    args.directory.joinpath("presentation-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
