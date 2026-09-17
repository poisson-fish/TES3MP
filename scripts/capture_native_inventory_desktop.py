#!/usr/bin/env python3
"""Exercise native inventory through two real OpenMW GUI clients, without screen control.

Consumes an operator-prepared native-inventory-3 runtime with a stocked placed
chest. Run transfer, then restart against the SAME runtime.
Does not seed/reset identities, credentials, or the canonical save. Automation
must be enabled in the client build. Game assets and runtime files stay external.
"""

from __future__ import annotations

import argparse
from collections import Counter
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

import desktop_evidence_harness as harness


def capture(args: argparse.Namespace) -> None:
    args.output.mkdir(parents=True, exist_ok=False)
    config = args.runtime / "server.cfg"
    settings = dict(line.split("=", 1) for line in config.read_text().splitlines()
                    if "=" in line and not line.lstrip().startswith("#"))
    settings = {key.strip(): value.strip() for key, value in settings.items()}
    if "native_inventory_file" not in settings or "inventory_content_file" in settings:
        raise RuntimeError("Capture requires exclusive native inventory configuration")
    port = int(settings["port"])
    password = Path(settings["join_password_file"])
    if not password.is_absolute():
        password = args.runtime / password
    save = Path(settings["player_identity_file"] + ".world-v2")
    if not save.is_absolute():
        save = args.runtime / save
    before = hashlib.sha256(save.read_bytes()).hexdigest() if save.exists() else None
    if args.phase == "restart" and before is None:
        raise RuntimeError("Durable restart requires the existing canonical save")
    roles = ("native-put", "native-take") if args.phase == "transfer" else (
        "native-recover-one", "native-recover-two")
    script = args.output / "startup.txt"
    script.write_text("StopScript CharGen\nset CharGenState to -1\n"
                      "EnableInventoryMenu\nEnablePlayerControls\n", encoding="utf-8")
    processes: list[subprocess.Popen] = []
    with ExitStack() as stack:
        def launch(command: list[str], name: str) -> subprocess.Popen:
            stdout = stack.enter_context((args.output / (name + ".stdout.log")).open("wb"))
            stderr = stack.enter_context((args.output / (name + ".stderr.log")).open("wb"))
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                       env=harness._client_environment(args),
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            processes.append(process)
            return process

        server = launch([str(args.server), str(config)], "server")
        try:
            time.sleep(1)
            if server.poll() is not None:
                raise RuntimeError("Native server failed to start; inspect server.stderr.log")
            clients = []
            for index in (0, 1):
                if args.phase == "transfer" and index == 1:
                    # A real late join: first GUI take must be committed before
                    # the second process is even created.
                    deadline = time.monotonic() + 45
                    path = args.output / (roles[0] + ".ndjson")
                    while True:
                        records = [json.loads(line) for line in path.read_text().split("\n")[:-1] if line] if path.exists() else []
                        if any(r.get("event") == "native_gui_first_take_observed" for r in records):
                            break
                        if time.monotonic() > deadline or any(r.get("success") is False for r in records):
                            raise RuntimeError("First client's committed take missing before late join")
                        time.sleep(0.1)
                role = roles[index]
                evidence = args.output / (role + ".ndjson")
                (args.output / (role + "-user")).mkdir()
                command = harness.build_client_command(args, args.pack, port, password,
                                                       role, evidence, set(roles), "native inventory")
                command = [value for value in command if not value.startswith(
                    ("--tes3mp-player-credential-file=", "--new-game=", "--start=", "--tes3mp-content-container-map=", "--tes3mp-content-item-prototype-map="))]
                command += [f"--tes3mp-player-credential-file={args.credentials[index]}",
                            f"--start={args.cell}",
                            "--new-game=0", "--script-console=1", f"--script-run={script}"]
                clients.append(launch(command, role))
                time.sleep(0.5)
            deadline = time.monotonic() + 75
            while any(client.poll() is None for client in clients):
                if server.poll() is not None:
                    raise RuntimeError("Native server stopped during capture")
                for role in roles:
                    path = args.output / (role + ".ndjson")
                    if path.exists():
                        # A writer can be between write and newline while we poll.
                        records = [json.loads(line) for line in path.read_text().split("\n")[:-1] if line]
                        if any(record.get("success") is False for record in records):
                            raise RuntimeError(f"Desktop role failed: {role}; inspect its bounded evidence/log")
                if time.monotonic() > deadline:
                    raise RuntimeError("Native desktop capture timed out")
                time.sleep(0.1)
            if any(client.returncode != 0 for client in clients):
                raise RuntimeError("Native desktop client exited unsuccessfully")
            placed_ids = set()
            observations = {}
            finals = []
            def one(records, event):
                found = [r for r in records if r.get("event") == event]
                if len(found) != 1:
                    raise RuntimeError(f"Expected exactly one {event}")
                placed_ids.add(found[0]["container_id"])
                return found[0]
            def contents(stacks):
                result = Counter()
                for item in stacks:
                    result[(item["item"], item["condition"], item["charge"], item["soul"])] += item["count"]
                return result
            for role in roles:
                complete, records = harness._completion(args.output / (role + ".ndjson"), role, "native inventory")
                if not complete["player_identity_stable"] or not complete["saw_peer"]:
                    raise RuntimeError(f"Client identity/peer evidence missing: {role}")
                observations[role] = records
                final = one(records, "native_gui_resumed" if args.phase == "transfer" else "native_gui_restart_continuation")
                if final["container"]:
                    raise RuntimeError("Chest was not empty at completion")
                if args.phase == "transfer":
                    emptied = one(records, "native_gui_emptied")
                    if final["player"] != emptied["player"] or complete["resumes"] != 1:
                        raise RuntimeError("Reconnect changed inventory identities")
                finals.append(final["player"])
            if args.phase == "transfer":
                initial = one(observations[roles[0]], "native_gui_initial")
                taken = one(observations[roles[0]], "native_gui_first_take_observed")
                late = one(observations[roles[1]], "native_gui_late_join")
                if not initial["container"] or taken["container"] != late["container"]:
                    raise RuntimeError("Late join did not see the committed GUI take")
                if contents(initial["player"] + late["player"] + initial["container"]) != contents(finals[0] + finals[1]):
                    raise RuntimeError("GUI transfer lost or duplicated loot")
                put = one(observations[roles[0]], "native_gui_put_observed")
                if contents(put["container"]) != contents(initial["container"]):
                    raise RuntimeError("GUI put did not restore original loot")
            else:
                previous = json.loads((args.output.parent / "transfer" / "summary.json").read_text())
                if before != previous["save_after"]:
                    raise RuntimeError("Restart did not use transfer's exact durable file")
                recovered = [one(observations[role], "native_gui_recovered") for role in roles]
                if any(r["container"] for r in recovered) or [r["player"] for r in recovered] != previous["final_players"]:
                    raise RuntimeError("Restart refilled loot or changed saved item identities")
                if contents(recovered[0]["player"] + recovered[1]["player"]) != contents(finals[0] + finals[1]):
                    raise RuntimeError("Restart continuation lost or duplicated loot")
            if len(placed_ids) != 1 or not next(iter(placed_ids)) & (1 << 63):
                raise RuntimeError("Clients did not converge on one engine-derived placed reference")
            time.sleep(0.5)
        finally:
            # Terminating this server is intentional crash/recovery evidence: accepted
            # native changes must already be durable without an orderly shutdown save.
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=5)
    summary = {"phase": args.phase, "roles": roles, "success": True,
               "placed_container_id": next(iter(placed_ids)), "cell": args.cell,
               "manifest": settings["content_manifest_id"], "save_before": before,
               "save_after": hashlib.sha256(save.read_bytes()).hexdigest(),
               "server_stop": "terminated after client completion; no shutdown save",
               "final_players": finals,
               "initial_container": initial["container"] if args.phase == "transfer" else []}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"PASS native desktop {args.phase}: two GUI clients; evidence {args.output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("transfer", "restart"))
    parser.add_argument("--cell", default="Imperial Prison Ship")
    for option in ("runtime", "pack", "output", "server", "openmw", "resources"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--credentials", type=Path, nargs=2, required=True)
    parser.add_argument("--data", type=Path, action="append", required=True)
    parser.add_argument("--content", action="append", default=[])
    parser.add_argument("--fallback-archive", action="append", default=[])
    options = parser.parse_args()
    for option in ("runtime", "pack", "output", "server", "openmw", "resources"):
        setattr(options, option, getattr(options, option).resolve())
    options.credentials = [path.resolve() for path in options.credentials]
    capture(options)
