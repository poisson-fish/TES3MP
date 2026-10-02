"""Check bounded, per-render actor traces from the two-desktop capture harness."""

import argparse
import json
import statistics
from pathlib import Path


def percentile(values, fraction):
    return sorted(values)[min(len(values) - 1, int(len(values) * fraction))]


def verify(directory, actor=False, casting=False, player_casting=False, deaths=False, melee=True):
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
        if deaths:
            played = {(p["kind"], p["id"]) for frame in frames for p in frame["actors"]
                      if p.get("dead") and p["clip_time"] >= 0
                      and p["group"].startswith(("death", "swimdeath"))
                      and abs(p["frame"] - round(p["frame"])) > .001}
            if len(played) < 3 or not any(kind == 1 for kind, identity in played):
                raise ValueError(f"{path.name}: missing played fractional NPC/creature/player deaths")
        cadence, body_rates, swing_rates, cast_rates = [], [], [], []
        cast_samples = cast_fractional = 0
        cast_phases = set()
        player_casts = {i: dict(samples=0, fractional=0, phases=set(), rates=[]) for i in (1, 2)}
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
                if (casting or player_casting) and (p["kind"] == 2 or player_casting) and p.get("cast_phase", 0) >= 3 and p["body"] == 1:
                    cast_phases.add(p["cast_phase"])
                    player_cast = player_casts.get(p["id"]) if p["kind"] == 1 else None
                    if player_cast is not None: player_cast["phases"].add(p["cast_phase"])
                    if p["cast_frame"] >= p["cast_stop"]:
                        raise ValueError(f"{path.name}: cast passed committed recovery")
                    if old and old.get("cast") == p["cast"] and old["life"] == p["life"] and old["body"] == 1:
                        delta = p["clip_time"] - old["clip_time"]
                        frames_delta = p["cast_frame"] - old["cast_frame"]
                        if frames_delta < -.001:
                            raise ValueError(f"{path.name}: cast regressed or advanced through a pause")
                        # Clip evidence is relative to the current section's start
                        # key. Release changes that origin; the cast clock remains
                        # continuous and must never regress across the boundary.
                        same_section = old.get("cast_phase") == p["cast_phase"] \
                            and old["clip_time"] >= 0 and p["clip_time"] >= 0
                        if same_section and (delta < -.002 or (frames_delta == 0 and abs(delta) > .002)):
                            raise ValueError(f"{path.name}: cast regressed or advanced through a pause")
                        if same_section and frames_delta > .001:
                            if player_cast is not None:
                                player_cast["samples"] += 1
                                player_cast["fractional"] += abs(p["cast_frame"] - round(p["cast_frame"])) > .001
                                if delta > .002 and seconds < .05: player_cast["rates"].append(delta / seconds)
                            cast_samples += 1
                            cast_fractional += abs(p["cast_frame"] - round(p["cast_frame"])) > .001
                            if delta > .002 and seconds < .05:
                                cast_rates.append(delta / seconds)
                if not old or any(old[key] != p[key] for key in
                                  ("life", "action", "phase", "body_action", "body", "group")):
                    continue
                delta = p["clip_time"] - old["clip_time"]
                committed_body = p["body"] >= 2 or p.get("dead", False)
                if committed_body and old["clip_time"] >= 0 and p["clip_time"] >= 0 \
                        and p["frame"] == old["frame"] and abs(delta) > .002:
                    raise ValueError(f"{path.name}: paused body clip advanced without a committed frame")
                if committed_body and p["frame"] > old["frame"] and old["clip_time"] >= 0:
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
        if actor and (body_samples < 30 or (melee and (swing_samples < 30 or not swing_rates
                         or not .9 <= statistics.median(swing_rates) / melee_speed <= 1.1))):
            raise ValueError(f"{path.name}: insufficient actor body/melee progression at stock speed")
        if casting and (cast_samples < 60 or cast_fractional < 30 or not {3, 5} <= cast_phases
                        or not cast_rates or not .9 <= statistics.median(cast_rates) <= 1.1):
            raise ValueError(f"{path.name}: insufficient fractional cast release/recovery at stock speed")
        if player_casting:
            for player, values in player_casts.items():
                if (values["samples"] < 60 or values["fractional"] < 30 or not {3, 5} <= values["phases"]
                        or not values["rates"] or not .9 <= statistics.median(values["rates"]) <= 1.1):
                    raise ValueError(f"{path.name}: player {player} lacks fractional cast/recovery at stock speed")
        player_cast_report = {player: dict(samples=v["samples"], fractional=v["fractional"], phases=sorted(v["phases"]),
            median_speed=statistics.median(v["rates"]) if v["rates"] else None) for player, v in player_casts.items()}
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
            player_casts=player_cast_report,
            cast_samples=cast_samples, fractional_cast_samples=cast_fractional,
            cast_phases=sorted(cast_phases), median_cast_speed=statistics.median(cast_rates) if cast_rates else None,
        )
    if not all(any(name.startswith(role) for name in result) for role in ("Alice", "Bob")):
        raise ValueError("Both desktop traces are required")
    if (actor or casting or player_casting) and not {"Alice-before.ndjson", "Bob-before.ndjson", "Alice.ndjson", "Bob.ndjson"} <= result.keys():
        raise ValueError("Both desktops require traces before and after restart")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--actor", "--creature", dest="actor", action="store_true", help="Require selected actor body and melee evidence")
    parser.add_argument("--casting", action="store_true", help="Require fractional cast release/recovery on both desktops across restart")
    parser.add_argument("--player-casting", action="store_true", help="Require each player on both desktops before/after restart")
    parser.add_argument("--deaths", action="store_true", help="Require played fractional NPC, creature and player deaths")
    parser.add_argument("--body-only", action="store_true", help="Validate actor body recovery without requiring a melee attack from the subject")
    args = parser.parse_args()
    report = verify(args.directory, args.actor, args.casting, args.player_casting, args.deaths, not args.body_only)
    args.directory.joinpath("presentation-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
