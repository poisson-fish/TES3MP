#!/usr/bin/env python3
"""Exercise native inventory through two real OpenMW GUI clients, without screen control.

Consumes an operator-prepared native runtime (3/5 shirts, mapped prison-ship
barrel 90, prototype 70). Run transfer, then restart against the SAME runtime.
Does not seed/reset identities, credentials, or the canonical save. Automation
must be enabled in the client build. Game assets and runtime files stay external.
"""

from __future__ import annotations

import argparse
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
            # Reverse connection order covers established-player insertion order.
            for index in (1, 0):
                role = roles[index]
                evidence = args.output / (role + ".ndjson")
                (args.output / (role + "-user")).mkdir()
                command = harness.build_client_command(args, args.pack, port, password,
                                                       role, evidence, set(roles), "native inventory")
                command = [value for value in command if not value.startswith(
                    ("--tes3mp-player-credential-file=", "--new-game="))]
                command += [f"--tes3mp-player-credential-file={args.credentials[index]}",
                            "--new-game=0", "--script-console=1", f"--script-run={script}",
                            "--tes3mp-content-item-prototype-map=70=common_shirt_01",
                            "--tes3mp-content-container-map=90=299164:1"]
                clients.append(launch(command, role))
                time.sleep(0.5)
            deadline = time.monotonic() + 45
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
            for role in roles:
                complete, records = harness._completion(args.output / (role + ".ndjson"), role, "native inventory")
                required = {"native_gui_initial", "native_gui_put_observed", "native_gui_take_observed",
                            "native_gui_resumed"} if args.phase == "transfer" else {
                                "native_gui_recovered", "native_gui_take_observed", "native_gui_restart_continuation"}
                if not required.issubset(record.get("event") for record in records):
                    raise RuntimeError(f"GUI evidence incomplete: {role}")
                if not complete["player_identity_stable"] or not complete["saw_peer"]:
                    raise RuntimeError(f"Client identity/peer evidence missing: {role}")
                first = role == roles[0]
                if args.phase == "transfer":
                    expected = {"native_gui_initial": (3 if first else 5, 0),
                                "native_gui_put_observed": (1 if first else 5, 2),
                                "native_gui_take_observed": (1 if first else 6, 1),
                                "native_gui_resumed": (1 if first else 6, 1)}
                    required_intent = "native_put_intent_captured" if first else "native_take_intent_captured"
                    if sum(record.get("event") == required_intent for record in records) != 1 or complete["resumes"] != 1:
                        raise RuntimeError(f"Single GUI intent/resume evidence missing: {role}")
                else:
                    expected = {"native_gui_recovered": (1 if first else 6, 1),
                                "native_gui_take_observed": (1 if first else 7, 0),
                                "native_gui_restart_continuation": (1 if first else 7, 0)}
                for event, counts in expected.items():
                    observed = [record for record in records if record.get("event") == event]
                    if len(observed) != 1 or (observed[0]["player_shirts"], observed[0]["container_shirts"]) != counts:
                        raise RuntimeError(f"GUI count evidence mismatch: {role}/{event}")
            time.sleep(0.5)
        finally:
            # Terminating this server is intentional crash/recovery evidence: accepted
            # native changes must already be durable without an orderly shutdown save.
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=5)
    summary = {"phase": args.phase, "roles": roles, "success": True,
               "manifest": settings["content_manifest_id"], "save_before": before,
               "save_after": hashlib.sha256(save.read_bytes()).hexdigest(),
               "server_stop": "terminated after client completion; no shutdown save"}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"PASS native desktop {args.phase}: two GUI clients; evidence {args.output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("transfer", "restart"))
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
