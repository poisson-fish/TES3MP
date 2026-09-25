#!/usr/bin/env python3
"""Fresh real-loadout two-desktop native NPC navigation and combat captures."""

import argparse
from dataclasses import asdict
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import time

from run_phase12_movement_capture import UdpImpairmentRelay, free_port


class NavigationImpairment:
    def __init__(self):
        self.sequence = {}

    def delivery(self, direction, now):
        n = self.sequence.get(direction, 0) + 1
        self.sequence[direction] = n
        if n % 10 == 0:
            return None
        return now + 0.1 + (n % 5 - 2) * 0.0125 + (0.125 if n % 23 == 0 else 0)


def records(path):
    if not path.exists():
        return []
    # The writer flushes complete events; ignore a concurrently written tail.
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    return [json.loads(line) for line in lines if line.endswith("\n")]


def poses(path):
    return [r for r in records(path) if r.get("event") == "native_actor_pose"]


def distance(a, b):
    return math.hypot(a["x"] - b["x"], a["y"] - b["y"])


def trajectory_errors(motions):
    # Latest-wins delivery may select disjoint ticks for the two peers.
    errors = []
    other = sorted(motions[1].values(), key=lambda sample: sample["tick"])
    for tick, sample in motions[0].items():
        for a, b in zip(other, other[1:]):
            if a["tick"] <= tick <= b["tick"] and b["tick"] - a["tick"] <= 10:
                ratio = (tick - a["tick"]) / (b["tick"] - a["tick"])
                errors.append(distance(sample, {axis: a[axis] + ratio * (b[axis] - a[axis])
                                                for axis in ("x", "y")}))
                break
    return errors


def verify_combat(output, evidence, processes, relay, manifest, immediate_reconnect=False):
    """One real-loadout NPC swing presented to two desktops over impaired UDP."""
    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def wait_for(predicate, description, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited while waiting for {description}")
            time.sleep(.1)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        temporary.replace(control)
        return wait_for(lambda: next((r for r in records(evidence[role])
                                     if r.get("sequence") == sequence[role]
                                     and r.get("event") == "traversal_" + action.split()[0]), None), action)

    wait_for(lambda: all(len(poses(path)) >= 30 and len(samples(role)) >= 5
                         for role, path in evidence.items()), "two live combat replicas")
    initial = {role: samples(role)[-1] for role in evidence}
    actor = poses(evidence["Alice"])[-1]
    for index, role in enumerate(evidence):
        command(role, f"pose {actor['x']} {actor['y'] - 60 - 20 * index} {actor['z']} 0 0")
    victim = wait_for(lambda: next((role for role in evidence
                                    if any(sample["health"] < initial[role]["health"]
                                           for sample in samples(role))), None),
                      "native NPC weapon damage", 35)
    def hits(role):
        return [hit for sample in samples(role) for hit in sample["actor_hits"]
                if hit["target"] == initial[victim]["self"] and hit["hit"] and hit["damage"] > 0]

    wait_for(lambda: all(len(hits(role)) == 1 for role in evidence),
             "one reliable actor hit on each desktop")
    other = "Bob" if victim == "Alice" else "Alice"
    wait_for(lambda: any(p["id"] == initial[victim]["self"]
                         and abs(p["health"] - samples(victim)[-1]["health"]) < .01
                         for p in samples(other)[-1]["players"]), "peer sees target health")
    if not immediate_reconnect:
        time.sleep(1)
    before = {role: samples(role)[-1] for role in evidence}
    marker = len(records(evidence[victim]))
    command(victim, "reconnect")
    wait_for(lambda: any(r.get("event") == "phase8_desktop_status" and r.get("status") == "resumed"
                         for r in records(evidence[victim])[marker:]), f"{victim} resumed")
    wait_for(lambda: len([r for r in records(evidence[victim])[marker:]
                          if r.get("event") == "native_combat_sample"]) >= 3, "fresh combat state")
    after = {role: samples(role)[-1] for role in evidence}
    if after[victim]["generation"] <= before[victim]["generation"]:
        raise RuntimeError("Victim did not receive a new combat session generation")
    if after[victim]["health"] > before[victim]["health"]:
        raise RuntimeError("Reconnect restored damaged player health")
    if any(len(hits(role)) != 1 for role in evidence):
        raise RuntimeError("Reconnect duplicated the reliable NPC hit")
    for role in evidence:
        command(role, "screenshot")
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="V24 live NPC weapon damage", victim=victim,
                  health_loss=initial[victim]["health"] - before[victim]["health"],
                  reliable_hits={role: hits(role) for role in evidence},
                  before=before, after=after, immediate_reconnect=immediate_reconnect,
                  relay=asdict(relay.stop()), manifest=manifest,
                  screenshots=[p.name for p in output.glob("*.png")],
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay")
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


def verify_life_encounter(output, evidence, processes, relay, manifest, maximum_attacks):
    """Real-loadout desktop attack capture, one corpse transfer and immediate resume."""
    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def inventories(role):
        return [r for r in records(evidence[role]) if r.get("event") == "traversal_inventory"]

    def wait_for(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            exited = [(name, p.poll()) for name, p in processes.items()
                      if name not in finished and p.poll() is not None]
            if exited:
                raise RuntimeError(f"process exited: {description}: {exited}")
            time.sleep(.1)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        temporary.replace(control)
        return wait_for(lambda: next((r for r in records(evidence[role])
                                     if r.get("sequence") == sequence[role]
                                     and r.get("event") == "traversal_" + action.split()[0]), None), action)

    wait_for(lambda: all(len(poses(path)) >= 20 and len(samples(role)) >= 3
                         for role, path in evidence.items()), "live actor and combat baselines")
    for role in evidence:
        command(role, "screenshot")
    starting = {role: samples(role)[-1] for role in evidence}
    actor = poses(evidence["Alice"])[-1]
    command("Alice", f"pose {actor['x']} {actor['y'] - 55} {actor['z']} 0 0")
    command("Bob", f"pose {actor['x'] + 35} {actor['y'] - 70} {actor['z']} 0 0")
    attacks = 0
    deadline = time.monotonic() + 65
    while attacks < maximum_attacks and time.monotonic() < deadline and not samples("Alice")[-1]["actors"][0]["dead"]:
        command("Alice", "attack")
        attacks += 1
        time.sleep(.5)
    wait_for(lambda: all(samples(role)[-1]["actors"][0]["dead"] for role in evidence),
             "both clients show one NPC death", 10)
    death = {role: samples(role)[-1] for role in evidence}
    observed_hits = {role: [hit for sample in samples(role) for hit in sample["player_hits"]]
                     for role in evidence}
    if not all(any(hit["died"] for hit in hits) for hits in observed_hits.values()):
        raise RuntimeError("Reliable attributed killing hit missing on a client")
    # Reconnect at the death edge, while earlier movement packets can still be in flight.
    marker = len(records(evidence["Bob"]))
    command("Bob", "reconnect")
    wait_for(lambda: any(r.get("event") == "phase8_desktop_status"
                         and r.get("status") in ("resumed", "resume_failed")
                         for r in records(evidence["Bob"])[marker:]), "immediate post-death resume", 20)
    statuses = [r["status"] for r in records(evidence["Bob"])[marker:]
                if r.get("event") == "phase8_desktop_status"]
    if "resumed" not in statuses:
        raise RuntimeError(f"Immediate post-death resume failed: {statuses}")
    wait_for(lambda: samples("Bob")[-1]["generation"] > death["Bob"]["generation"]
             and samples("Bob")[-1]["actors"][0]["dead"]
             and inventories("Bob")[-1]["container_count"] > 0,
             "resumed corpse baseline")
    original_corpse = inventories("Bob")[-1]
    command("Bob", "open")
    command("Bob", "takeall")
    wait_for(lambda: inventories("Bob")[-1]["container_count"] == 0
             and inventories("Bob")[-1]["player_count"] > original_corpse["player_count"],
             "durable shared corpse loot")
    wait_for(lambda: inventories("Alice")[-1]["container_count"] == 0,
             "second client sees emptied corpse")
    for role in evidence:
        command(role, "screenshot")
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="V25 live NPC death and corpse loot", attacks=attacks,
                  initial=starting, death=death, reliable_player_hits=observed_hits,
                  original_corpse=original_corpse, looted=inventories("Bob")[-1],
                  resume_statuses=statuses, relay=asdict(relay.stop()), manifest=manifest,
                  screenshots=[p.name for p in output.glob("*.png")],
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay")
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


def verify_unarmed_effect(output, evidence, processes, relay, manifest, maximum_attacks):
    """Two desktops observe one OpenMW hand-to-hand fatigue hit and reconnect."""
    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def samples(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_combat_sample"]

    def inventory(role):
        return [r for r in records(evidence[role]) if r.get("event") == "traversal_inventory"]

    def wait_for(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited while waiting for {description}")
            time.sleep(.1)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        for attempt in range(30):
            try:
                temporary.replace(control)
                break
            except PermissionError:
                if attempt == 29:
                    raise
                time.sleep(.03)
        return wait_for(lambda: next((r for r in records(evidence[role])
                                     if r.get("sequence") == sequence[role]
                                     and r.get("event") == "traversal_" + action.split()[0]), None), action)

    wait_for(lambda: all(len(samples(role)) >= 3 and len(inventory(role)) >= 1
                         for role in evidence), "two combat and inventory baselines")
    actor = poses(evidence["Alice"])[-1]
    command("Alice", f"pose {actor['x']} {actor['y'] - 55} {actor['z']} 0 0")
    command("Bob", f"pose {actor['x'] + 35} {actor['y'] - 70} {actor['z']} 0 0")
    initial = {role: samples(role)[-1] for role in evidence}

    def fatigue_hits(role):
        return [(sample["tick"], hit) for sample in samples(role)
                for hit in sample["player_hits"]
                if hit["attacker"] == initial["Alice"]["self"] and hit["stat"] == 1
                and hit["hit"] and hit["damage"] > 0]

    def attempts_seen(role):
        return [(sample["tick"], hit) for sample in samples(role)
                for hit in sample["player_hits"]
                if hit["attacker"] == initial["Alice"]["self"] and hit["stat"] == 1]

    attempts = 0
    while attempts < maximum_attacks and not all(fatigue_hits(role) for role in evidence):
        previous = {role: len(attempts_seen(role)) for role in evidence}
        command("Alice", "attack")
        attempts += 1
        wait_for(lambda: all(len(attempts_seen(role)) > previous[role] for role in evidence),
                 "both clients receive the unarmed attack result", 10)
    wait_for(lambda: all(fatigue_hits(role) for role in evidence), "shared unarmed fatigue event", 10)
    hit_damage = fatigue_hits("Alice")[0][1]["damage"]
    threshold = initial["Alice"]["actors"][0]["fatigue"] - hit_damage + .01
    def shared_effect():
        left = {sample["actors"][0]["fatigue"] for sample in samples("Alice")
                if sample["actors"] and sample["actors"][0]["fatigue"] <= threshold}
        right = {sample["actors"][0]["fatigue"] for sample in samples("Bob")
                 if sample["actors"] and sample["actors"][0]["fatigue"] <= threshold}
        return left & right
    wait_for(shared_effect, "two-client fatigue convergence", 10)
    before = {role: samples(role)[-1] for role in evidence}
    if any(before[role]["actors"][0]["health"] != initial[role]["actors"][0]["health"]
           or before[role]["actors"][0]["fatigue"] >= initial[role]["actors"][0]["fatigue"]
           for role in evidence):
        raise RuntimeError("Unarmed hit changed health or did not reduce actor fatigue")
    time.sleep(1)  # Let the attack receipt settle before exercising reconnect.
    marker = len(records(evidence["Bob"]))
    command("Bob", "reconnect")
    wait_for(lambda: any(r.get("event") == "phase8_desktop_status" and r.get("status") == "resumed"
                         for r in records(evidence["Bob"])[marker:]), "Bob reconnect", 20)
    wait_for(lambda: samples("Bob")[-1]["generation"] > before["Bob"]["generation"]
             and abs(samples("Bob")[-1]["actors"][0]["fatigue"]
                     - samples("Alice")[-1]["actors"][0]["fatigue"]) < .01,
             "reconnected fatigue convergence")
    after = {role: samples(role)[-1] for role in evidence}
    if any(len(fatigue_hits(role)) != 1
           or after[role]["actors"][0]["health"] != initial[role]["actors"][0]["health"]
           for role in evidence):
        raise RuntimeError("Reconnect duplicated the effect event or changed health")
    for role in evidence:
        command(role, "screenshot")
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} did not finish cleanly")
        finished.add(role)
    report = dict(success=True, scenario="Native OpenMW unarmed fatigue effect", attempts=attempts,
                  initial=initial, before=before, after=after,
                  reliable_hits={role: fatigue_hits(role) for role in evidence},
                  relay=asdict(relay.stop()), manifest=manifest,
                  screenshots=[p.name for p in output.glob("*.png")])
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


def verify_doors(output, evidence, processes, relay, manifest):
    """Real content and desktop activation; no synthetic placements or server commands."""
    sequence = dict.fromkeys(evidence, 0)
    finished = set()

    def wait_for(predicate, description, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited while waiting for {description}")
            time.sleep(.1)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        temporary.replace(control)
        return wait_for(lambda: next((r for r in records(evidence[role])
                                     if r.get("sequence") == sequence[role]
                                     and r.get("event") == "traversal_" + action.split()[0]), None), action)

    def door_records(role):
        return [r for r in records(evidence[role]) if r.get("event") == "native_door_presented"]

    def settled(role):
        data = poses(evidence[role])[-20:]
        return len(data) == 20 and all(distance(p, data[-1]) < .01 for p in data)

    wait_for(lambda: all(len(poses(path)) > 30 and door_records(role) and settled(role)
                         for role, path in evidence.items()), "two settled desktop replicas")
    initial = {role: poses(path)[-1] for role, path in evidence.items()}
    command("Alice", "pose -70 -340 -125 0 2.25")
    command("Bob", "pose -130 -230 -125 0 2.4")
    time.sleep(.5)
    screenshots = {role: command(role, "screenshot") for role in evidence}
    focus = screenshots["Alice"]
    if focus["focus"] != "in_velothismall_ndoor_01":
        raise RuntimeError(f"Door not in Alice's focus: {focus['focus']}")
    command("Alice", 'activate "in_velothismall_ndoor_01"')
    wait_for(lambda: all(any(r["doors"][0]["blocked"] for r in door_records(role)) for role in evidence),
             "both clients presenting the NPC obstruction")
    for role in evidence:
        command(role, "screenshot")
    wait_for(lambda: all(door_records(role)[-1]["doors"][0]["direction"] == 0
             and door_records(role)[-1]["doors"][0]["angle"] > 3.1 and settled(role) for role in evidence),
             "both clients: door fully open and NPC destination resumed")
    for role in evidence:
        command(role, "screenshot")
    data = {role: records(path) for role, path in evidence.items()}
    metrics = {}
    for role, events in data.items():
        frames = [r for r in events if r.get("event") == "native_actor_pose"]
        excursion = max(distance(initial[role], p) for p in frames)
        final = frames[-1]
        if excursion < 30 or distance(final, {"x": 32, "y": -320}) > 20:
            raise RuntimeError(f"{role}: NPC did not retreat and resume its destination")
        doors = [d for r in events if r.get("event") == "native_door_presented" for d in r["doors"]]
        if any(d["angle"] != d["rendered_angle"] for d in doors):
            raise RuntimeError(f"{role}: visual door angles diverged")
        metrics[role] = dict(rendered_frames=len(frames), excursion=excursion, final=final,
                             blocked_observations=sum(d["blocked"] for d in doors))
    if distance(metrics["Alice"]["final"], metrics["Bob"]["final"]) > .1:
        raise RuntimeError("Clients did not converge")
    motion = [{r["tick"]: r for r in data[role] if r.get("event") == "native_actor_sample"} for role in evidence]
    common_ticks = motion[0].keys() & motion[1].keys()
    if any(distance(motion[0][t], motion[1][t]) != 0
                               or motion[0][t]["z"] != motion[1][t]["z"] for t in common_ticks):
        raise RuntimeError("Clients received contradictory actor samples")
    errors = trajectory_errors(motion)
    if len(errors) < 15 or max(errors) >= 8:
        raise RuntimeError("Overlapping door-avoidance trajectories diverged")
    # Exercise each peer's real resume handshake through the same impaired relay.
    # Only fresh post-resume observations may satisfy convergence.
    resumes = {}
    for role in evidence:
        before_inventory = next(r for r in reversed(records(evidence[role]))
                                if r.get("event") == "traversal_inventory")
        marker = len(records(evidence[role]))
        command(role, "reconnect")
        wait_for(lambda: any(r.get("event") == "phase8_desktop_status" and r.get("status") == "resumed"
                             for r in records(evidence[role])[marker:]), f"{role} session resume")
        wait_for(lambda: sum(r.get("event") == "native_actor_pose"
                             for r in records(evidence[role])[marker:]) >= 30
                 and any(r.get("event") == "native_door_presented"
                         for r in records(evidence[role])[marker:]) and settled(role),
                 f"{role} fresh resumed actor and door presentation")
        # V18 binds the selected actor/inventory and doors; loose room items are
        # outside its domain, so the V14 whole-ground `observe` assertion is inapplicable.
        observation = command(role, "screenshot")
        inventory = next(r for r in reversed(records(evidence[role]))
                         if r.get("event") == "traversal_inventory")
        fresh = poses(evidence[role])[-1]
        other = poses(evidence["Bob" if role == "Alice" else "Alice"])[-1]
        authoritative = next(r for r in reversed(records(evidence[role]))
                             if r.get("event") == "native_actor_sample")
        if (observation["resumes"] != 1 or observation["local_ai_active"]
                or inventory["player"] != before_inventory["player"]
                or distance(fresh, other) > .1 or abs(fresh["z"] - other["z"]) > .1
                or distance(fresh, authoritative) > .1
                or any(d["direction"] != 0 or d["angle"] < 3.1 or d["angle"] != d["rendered_angle"]
                       for d in door_records(role)[-1]["doors"])):
            raise RuntimeError(f"{role}: resumed inventory/actor/door state did not converge")
        resumes[role] = dict(resumes=observation["resumes"], final=fresh,
                             peer_difference=distance(fresh, other), inventory_preserved=True)
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        completed = [r for r in records(evidence[role]) if r.get("event") == "phase8_desktop_complete"]
        if (processes[role].returncode or len(completed) != 1 or not completed[0]["success"]
                or not completed[0]["player_identity_stable"] or completed[0]["resumes"] != 1):
            raise RuntimeError(f"{role} did not finish cleanly")
    report = dict(success=True, scenario="V18 Hlavora, Vivec Redoran Records",
                  clients=metrics, resumes=resumes, common_ticks=len(common_ticks), relay=asdict(relay.stop()), manifest=manifest,
                  overlapping_samples=len(errors), maximum_trajectory_difference=max(errors),
                  screenshots=[p.name for p in output.glob("*.png")],
                  profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay")
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


def verify_traveler(output, evidence, processes, relay, manifest, restart_server, restart_clients):
    """Two real desktops leave; durable empty-world travel survives a process restart."""
    finished = set()
    sequence = dict.fromkeys(evidence, 0)

    def wait_for(predicate, description, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            if any(p.poll() is not None for name, p in processes.items() if name not in finished):
                raise RuntimeError(f"process exited: {description}")
            time.sleep(.1)
        raise RuntimeError(f"timed out: {description}")

    def command(role, action):
        sequence[role] += 1
        control = evidence[role].with_suffix(".ndjson.control")
        temporary = control.with_suffix(".tmp")
        temporary.write_text(f"{sequence[role]} {action}\n", encoding="ascii")
        temporary.replace(control)

    pattern = re.compile(r"native travel committed: tick=(\d+) status=(\S+) cells=(\d+)/(\d+) "
                         r"steps=(\d+)/2 completed=(\d) position=([-\d.]+),([-\d.]+),([-\d.]+)")

    def progress(name):
        path = output / (name + ".stderr.log")
        text = path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""
        return [dict(tick=int(m[0]), status=m[1], completed=bool(int(m[5])),
                     x=float(m[6]), y=float(m[7]), z=float(m[8])) for m in pattern.findall(text)]

    wait_for(lambda: all(len(poses(path)) >= 30 for path in evidence.values()), "both traveler replicas")
    for role, path in evidence.items():
        command(role, "screenshot")
        wait_for(lambda: any(r.get("event") == "traversal_inventory" for r in records(path)),
                 role + " initial inventory observation")
    before = {role: poses(path)[-1] for role, path in evidence.items()}
    identities = {role: next(r["player"] for r in reversed(records(path))
                            if r.get("event") == "traversal_inventory") for role, path in evidence.items()}
    for role in evidence:
        command(role, "quit")
        processes[role].wait(timeout=15)
        if processes[role].returncode:
            raise RuntimeError(f"{role} failed to leave cleanly")
        finished.add(role)
    disconnect = wait_for(lambda: re.findall(r"native actor disconnect committed: tick=(\d+) active_sessions=0",
                              output.joinpath("server.stderr.log").read_text(encoding="utf-8", errors="replace")),
                          "durable disconnect of both clients")
    left_tick = int(disconnect[-1])
    empty_trip = wait_for(lambda: [p for p in progress("server") if p["tick"] > left_tick + 90],
                         "unattended movement before restart")[-1]
    if empty_trip["completed"] or distance(empty_trip, before["Bob"]) < 4:
        raise RuntimeError("No distinct unfinished unattended travel before restart")
    processes["server"].terminate()
    processes["server"].wait(timeout=15)
    finished.add("server")
    print(f"Both clients left; restarting mid-trip after tick {empty_trip['tick']}", flush=True)
    restart_server()
    continued = wait_for(lambda: [p for p in progress("server-restarted") if p["tick"] > empty_trip["tick"] + 90],
                         "unattended movement after restart")[-1]
    if continued["completed"] or distance(continued, empty_trip) < 4:
        raise RuntimeError("Restart did not continue the unfinished empty-world trip")
    # Keep pre-restart evidence separately; the new clients must independently
    # establish identity and consume fresh committed snapshots.
    for role, path in evidence.items():
        path.rename(output / (role + "-before.ndjson"))
        control = path.with_suffix(".ndjson.control")
        control.unlink(missing_ok=True)
        sequence[role] = 0
    restart_clients()
    finished.difference_update(evidence)
    final_server = wait_for(lambda: next((p for p in reversed(progress("server-restarted")) if p["completed"]), None),
                            "server travel completion", timeout=240)
    if distance(final_server, {"x": -550, "y": 70}) > 16:
        raise RuntimeError("Server completed away from the retained destination")
    wait_for(lambda: all(len(poses(path)) >= 30
                         and all(distance(p, final_server) < .1 and abs(p["z"] - final_server["z"]) < .1
                                 for p in poses(path)[-20:]) for path in evidence.values()),
             "returning desktop convergence")
    clients = {}
    for role, path in evidence.items():
        # A walkable point on the observed route, looking back at the destination.
        command(role, "pose -400 30 385 0 -1.32")
        pose_sequence = sequence[role]
        wait_for(lambda: any(r.get("event") == "traversal_pose" and r.get("sequence") == pose_sequence
                             for r in records(path)), role + " returned camera")
        command(role, "screenshot")
        shot = wait_for(lambda: next((r for r in reversed(records(path)) if r.get("event") == "traversal_screenshot"), None),
                        role + " returned screenshot")
        inventory = next(r for r in reversed(records(path)) if r.get("event") == "traversal_inventory")
        if inventory["player"] != identities[role] or shot["local_ai_active"]:
            raise RuntimeError(f"{role} lost identity or enabled local AI")
        clients[role] = dict(final=poses(path)[-1], identity_preserved=True, local_ai_active=False)
        command(role, "quit")
        processes[role].wait(timeout=15)
        finished.add(role)
        finished.add(role + "-returned")
        if processes[role].returncode:
            raise RuntimeError(f"{role} failed to finish")
    report = dict(success=True, scenario="V20 real-content unattended interior travel and process restart",
                  cell="Seyda Neen, Arrille's Tradehouse", npc="raflod the braggart", synthetic_placements=False,
                  manifest=manifest, departed_tick=left_tick, before_restart=empty_trip,
                  after_restart=continued, completed=final_server, clients=clients,
                  relay=asdict(relay.stop()), screenshots=[p.name for p in output.glob("*.png")])
    output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)


def run(args):
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Never overwrite a campaign.
    binary = args.build.resolve()
    config = args.content_config.resolve()
    settings = root / "files/settings-default.cfg"
    cell = "Vivec, Redoran Records" if args.doors else "Seyda Neen, Arrille's Tradehouse"
    version = 25 if args.life_encounter or args.unarmed_effect else 24 if args.combat else 20 if args.traveler else 18 if args.doors else 16
    npc = "hlavora sadas" if args.doors else "raflod the braggart"
    destination = "-550 70 385 16" if args.traveler else "32 -320 -127 120" if args.doors else "-550 -245 385 40" if args.life_encounter or args.unarmed_effect else "-550 70 385 40"
    manifest = hashlib.sha256(f"native-navigation-capture-{version}".encode() + config.joinpath("openmw.cfg").read_bytes()
                              + settings.read_bytes()).hexdigest()
    password = output / "join-password.txt"
    password.write_text(secrets.token_hex(24), encoding="ascii")
    port, relay_port = free_port(), free_port()
    output.joinpath("native.txt").write_text(
        f'native-inventory-{version}\nmanifest {manifest}\nconfig "{config.as_posix()}"\nplayers 1 2\n'
        f'actors "{npc if args.life_encounter else "player"}" "{npc if args.life_encounter else "player"}"\nloot 1 0\ninterior "{cell}"\ndoors auto\ncell interior:1\nareas 1\n'
        f'npc "{npc}" "{settings.as_posix()}"\ndestination {destination}\n'
        + ('processing 1 2\n' if args.traveler or args.combat or args.life_encounter or args.unarmed_effect else '')
        + ('melee "weapononehand" "chop" 1\n' if args.combat or args.life_encounter or args.unarmed_effect else '')
        + ('respawn 27000\n' if args.life_encounter or args.unarmed_effect else ''), encoding="utf-8")
    common = dict(content_manifest_id=manifest, cell_spaces="interior:1", allowed_cells="interior:1",
                  spawn_cell="interior:1", spawn_positions="-81920:-204800:-128000" if args.doors else "-768000:-409600:394240", default_appearance_id="2",
                  movement_profile="sneak:1024;walk:4097;run:8192;jump:4096")
    if args.traveler:
        common["spawn_positions"] = "-563200:71680:394240"
    if args.life_encounter or args.unarmed_effect:
        common["spawn_positions"] = "-563200:-307200:394240"
    server_config = common | dict(native_inventory_file="native.txt", bind_address="127.0.0.1", port=port,
                                 tick_interval_ms=33, disconnect_grace_ms=30000,
                                 join_password_file="join-password.txt", player_identity_file="players.txt")
    output.joinpath("server.cfg").write_text("".join(f"{k}={v}\n" for k, v in server_config.items()), encoding="utf-8")
    template = next(line.split() for line in root.joinpath(
        "scripts/fixtures/desktop_evidence_established_player_v5.txt").read_text().splitlines()
                    if line and not line.startswith("TES3MP_"))
    identities = []
    for index, role in enumerate(("Alice", "Bob")):
        credential = secrets.token_bytes(32)
        output.joinpath(role + ".credential").write_bytes(credential)
        tokens = template.copy()
        tokens[0:5] = [str(index + 1), str(index * 2 + 1), "2", manifest, hashlib.sha256(credential).hexdigest()]
        tokens[6:16] = ["0", "1", "0", "0", str((-750 + 80 * index) * 1024), "-409600", "394240", "0", "0", "0"]
        if args.doors:
            tokens[10:13] = [str((-80 - 80 * index) * 1024), "-204800", "-128000"]
        if args.traveler:
            tokens[10:13] = ["-563200", str((70 + 40 * index) * 1024), "394240"]
        if args.life_encounter or args.unarmed_effect:
            tokens[10:13] = [str((-563 + 25 * index) * 1024), "-307200", "394240"]
        name = tokens[-1]
        tokens = [role.encode().hex() if token == name else token for token in tokens]
        identities.append(" ".join(tokens))
        user = output / (role + "-user")
        user.mkdir()
        shutil.copyfile(config / "openmw.cfg", user / "openmw.cfg")
        user.joinpath("settings.cfg").write_text(
            "[Video]\nresolution x = 1000\nresolution y = 700\nwindow mode = 2\nwindow border = true\n"
            "minimize on focus loss = false\nframerate limit = 30\n", encoding="utf-8")
    output.joinpath("players.txt").write_text("TES3MP_PLAYER_IDENTITIES_V5\n" + "\n".join(identities) + "\n",
                                            encoding="utf-8", newline="\n")
    startup = output / "startup.txt"
    startup.write_text("StopScript CharGen\nset CharGenState to -1\nEnableInventoryMenu\nEnableMagicMenu\n"
                       "EnableMapMenu\nEnableStatsMenu\nEnablePlayerControls\n", encoding="utf-8")
    environment = os.environ.copy()
    dependencies = root / "deps/installed/x64-windows/bin"
    environment["PATH"] = os.pathsep.join((str(binary), str(dependencies), environment.get("PATH", "")))
    environment["OSG_LIBRARY_PATH"] = str(dependencies)
    streams, processes = [], {}
    relay = UdpImpairmentRelay(relay_port, port, "direct")
    relay.schedule = NavigationImpairment()
    def start(name, command):
        stdout = output.joinpath(name + ".stdout.log").open("wb")
        stderr = output.joinpath(name + ".stderr.log").open("wb")
        streams.extend((stdout, stderr))
        process = subprocess.Popen(command, cwd=binary, env=environment, stdout=stdout, stderr=stderr,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        processes[name] = process
        return process
    evidence = {role: output / (role + ".ndjson") for role in ("Alice", "Bob")}
    survivor = "Bob" if args.leave == "Alice" else "Alice"
    client_commands = {}
    try:
        server = start("server", [str(binary / "tes3mp_server.exe"), str(output / "server.cfg")])
        time.sleep(2)
        if server.poll() is not None:
            raise RuntimeError("server startup failed; inspect server.stderr.log")
        relay.start()
        for role in evidence:
            user = output / (role + "-user")
            command = [str(binary / "openmw.exe"), "--tes3mp-enable=1", "--tes3mp-host=127.0.0.1",
                       f"--tes3mp-port={relay_port}", "--tes3mp-timeout-ms=15000",
                       f"--tes3mp-password-file={password}", f"--tes3mp-player-credential-file={output / (role + '.credential')}",
                       f"--config={user}", "--replace=config", f"--user-data={user}", f"--resources={binary / 'resources'}",
                       f"--start={cell}", "--skip-menu=1", "--new-game=0", "--no-grab=1", "--no-sound=1",
                       "--script-console=1", f"--script-run={startup}", "--tes3mp-automation-role=native-traversal",
                       f"--tes3mp-automation-output={evidence[role]}", f"--tes3mp-content-manifest-id={manifest}",
                       "--tes3mp-content-cell-spaces=interior:1", "--tes3mp-content-allowed-cells=interior:1",
                       f"--tes3mp-content-cell-space-map=1={cell}", "--tes3mp-content-appearance-id=2",
                       "--tes3mp-content-appearance-record=player"]
            start(role, command)
            client_commands[role] = command
        if args.unarmed_effect:
            verify_unarmed_effect(output, evidence, processes, relay, manifest, args.attack_limit)
            return
        if args.life_encounter:
            verify_life_encounter(output, evidence, processes, relay, manifest, args.attack_limit)
            return
        if args.combat:
            verify_combat(output, evidence, processes, relay, manifest, args.immediate_reconnect)
            return
        if args.traveler:
            def restart_clients():
                for role, command in client_commands.items():
                    processes[role] = start(role + "-returned", command)
            verify_traveler(output, evidence, processes, relay, manifest,
                            lambda: start("server-restarted", [str(binary / "tes3mp_server.exe"), str(output / "server.cfg")]),
                            restart_clients)
            return
        if args.doors:
            verify_doors(output, evidence, processes, relay, manifest)
            return
        deadline = time.monotonic() + 75
        ready_at, left_at, disconnect_pose = None, None, None
        while time.monotonic() < deadline:
            if server.poll() is not None or processes[survivor].poll() is not None:
                raise RuntimeError("server or survivor exited during navigation")
            if left_at is None and processes[args.leave].poll() is not None:
                raise RuntimeError("departing client exited before the disconnect request")
            samples = {role: poses(path) for role, path in evidence.items()}
            if ready_at is None and all(len(value) >= 30 for value in samples.values()):
                ready_at = time.monotonic()
                print("Both desktop clients render the moving native actor", flush=True)
            if ready_at and left_at is None and time.monotonic() >= ready_at + 2:
                disconnect_pose = samples[survivor][-1]
                evidence[args.leave].with_suffix(".ndjson.control").write_text("1 disconnect\n", encoding="ascii")
                left_at = time.monotonic()
                print(f"Requested transport disconnect for {args.leave}", flush=True)
            if left_at and time.monotonic() > left_at + 4:
                tail = samples[survivor][-30:]
                if len(tail) == 30 and all(distance(p, tail[-1]) < 0.01 for p in tail) and abs(tail[-1]["y"] - 70) < 16:
                    break
            time.sleep(0.2)
        else:
            raise RuntimeError("bounded desktop navigation/disconnect timed out")
        all_records = {role: records(path) for role, path in evidence.items()}
        if not any(r.get("event") == "phase8_desktop_status" and r.get("status") == "reconnecting"
                   for r in all_records[args.leave]):
            raise RuntimeError("departing client did not close its transport")
        metrics = {}
        for role, data in all_records.items():
            frames = [r for r in data if r.get("event") == "native_actor_pose"]
            motion = {r["tick"]: r for r in data if r.get("event") == "native_actor_sample"}
            steps = [distance(a, b) for a, b in zip(frames, frames[1:])]
            metrics[role] = dict(rendered_frames=len(frames), received_samples=len(motion), max_frame_step=max(steps),
                                 first=frames[0], last=frames[-1])
            if len(frames) < 60 or max(steps) >= 16 or distance(frames[0], frames[-1]) <= 25:
                raise RuntimeError(f"{role}: insufficient or discontinuous rendered movement")
        motions = [{r["tick"]: r for r in all_records[role] if r.get("event") == "native_actor_sample"} for role in evidence]
        common_ticks = motions[0].keys() & motions[1].keys()
        if any(distance(motions[0][tick], motions[1][tick]) != 0
                                       or motions[0][tick]["z"] != motions[1][tick]["z"] for tick in common_ticks):
            raise RuntimeError("clients did not receive matching authoritative actor samples")
        # Latest-wins delivery may select disjoint ticks for the two peers. Compare
        # the overlapping trajectories at the same server ticks as well as exact
        # equality wherever both received the same tick.
        interpolated_errors = trajectory_errors(motions)
        if len(interpolated_errors) < 15 or max(interpolated_errors) >= 4:
            raise RuntimeError("overlapping authoritative trajectories diverged")
        final = metrics[survivor]["last"]
        post_disconnect = distance(disconnect_pose, final)
        if post_disconnect < 50:
            raise RuntimeError("survivor did not observe enough movement after disconnect")
        latest = next(reversed(motions[0 if survivor == "Alice" else 1].values()))
        if distance(final, latest) >= 0.1:
            raise RuntimeError("survivor did not converge to the committed actor position")
        disconnected = re.findall(r"native actor disconnect committed: tick=(\d+) active_sessions=1",
                                   output.joinpath("server.stderr.log").read_text(encoding="utf-8"))
        if len(disconnected) != 1:
            raise RuntimeError("server did not durably disconnect exactly one participant")
        disconnect_tick = int(disconnected[0])
        remaining = sorted((r for r in motions[0 if survivor == "Alice" else 1].values()
                            if r["tick"] > disconnect_tick), key=lambda r: r["tick"])
        if not remaining or distance(remaining[0], final) < 50:
            raise RuntimeError("native actor did not continue after the durable disconnect")
        evidence[survivor].with_suffix(".ndjson.control").write_text("1 quit\n", encoding="ascii")
        processes[survivor].wait(timeout=15)
        completed = [r for r in records(evidence[survivor]) if r.get("event") == "phase8_desktop_complete"]
        if processes[survivor].returncode != 0 or len(completed) != 1 or not all(
                completed[0][key] for key in ("success", "saw_peer")):
            raise RuntimeError("survivor did not finish cleanly")
        stats = relay.stop()
        report = dict(success=True, leaving=args.leave, common_ticks=len(common_ticks),
                      overlapping_samples=len(interpolated_errors), maximum_trajectory_difference=max(interpolated_errors),
                      post_disconnect_distance=post_disconnect, relay=asdict(stats), clients=metrics,
                      durable_disconnect_tick=disconnect_tick, movement_after_durable_disconnect=distance(remaining[0], final),
                      profile="100 ms one-way, +/-25 ms jitter, 10% loss, periodic 125 ms extra delay",
                      manifest=manifest, content_config_sha256=hashlib.sha256(config.joinpath("openmw.cfg").read_bytes()).hexdigest())
        output.joinpath("result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2), flush=True)
    finally:
        relay.stop()
        for process in reversed(list(processes.values())):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
        for stream in streams:
            stream.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--content-config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--leave", choices=("Alice", "Bob"))
    parser.add_argument("--doors", action="store_true", help="V18 real-interior door avoidance on two connected clients")
    parser.add_argument("--traveler", action="store_true", help="V20 both clients leave, server restarts mid-trip, clients return")
    parser.add_argument("--combat", action="store_true", help="V24 live NPC weapon hit, impaired two-client presentation and reconnect")
    parser.add_argument("--immediate-reconnect", action="store_true", help="Reconnect at the live hit convergence edge")
    parser.add_argument("--life-encounter", action="store_true", help="V25 live NPC kill, corpse loot and immediate reconnect")
    parser.add_argument("--unarmed-effect", action="store_true", help="Live OpenMW unarmed fatigue effect on two desktops and reconnect")
    parser.add_argument("--attack-limit", type=int, default=40)
    args = parser.parse_args()
    if sum((args.doors, args.traveler, args.combat, args.life_encounter, args.unarmed_effect)) > 1:
        parser.error("choose one capture mode")
    if args.immediate_reconnect and not args.combat:
        parser.error("--immediate-reconnect requires --combat")
    if not args.doors and not args.traveler and not args.combat and not args.life_encounter and not args.unarmed_effect and not args.leave:
        parser.error("--leave is required for the V16 navigation capture")
    run(args)
