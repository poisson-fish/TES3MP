#!/usr/bin/env python3
import argparse
import json
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from run_phase7_join_demo import bounded_rss, resident_bytes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True)
    args = parser.parse_args()
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="tes3mp-phase7-soak-") as temporary:
        root = Path(temporary)
        password = root / "join-password"
        config = root / "server.cfg"
        password.write_text("phase7-soak-secret\n", encoding="utf-8")
        config.write_text(
            f"bind_address=127.0.0.1\nport={port}\ntick_interval_ms=16\n"
            f"disconnect_grace_ms=3000\njoin_password_file={password.as_posix()}\n",
            encoding="utf-8")
        server = subprocess.Popen([str(args.server), str(config)], text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        clients = []
        try:
            time.sleep(0.5)
            clients = [subprocess.Popen(
                [str(args.client), "127.0.0.1", str(port), str(password), "60000", mode],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                for mode in ("soak-one", "soak-two")]
            samples = {"server": [], "client_one": [], "client_two": []}
            deadline = time.monotonic() + 80
            while any(client.poll() is None for client in clients):
                if time.monotonic() >= deadline:
                    raise RuntimeError("60-second soak timed out")
                for name, process in zip(("server", "client_one", "client_two"),
                                         (server, *clients)):
                    if process.poll() is None:
                        samples[name].append(resident_bytes(process.pid))
                time.sleep(1)
            outputs = [client.communicate(timeout=5) for client in clients]
            if any(client.returncode != 0 for client in clients):
                raise RuntimeError(f"soak clients failed: outputs={outputs!r}")
            events = []
            for output in outputs:
                lines = [json.loads(line) for line in output[0].splitlines() if line]
                event = next((line for line in lines
                              if line.get("event") == "soak_flow_complete"), None)
                if not event or event.get("duration_seconds") != 60:
                    raise RuntimeError(f"soak evidence missing: output={output!r}")
                events.append(event)
            if events[0]["entries"] != events[1]["entries"]:
                raise RuntimeError(f"soak canonical views diverged: events={events!r}")
            memory = {name: bounded_rss(values) for name, values in samples.items()}
        finally:
            for client in clients:
                if client.poll() is None:
                    client.terminate()
                    client.communicate(timeout=5)
            server.terminate()
            server_output = server.communicate(timeout=5)
        server_events = [json.loads(line) for line in server_output[0].splitlines()
                         if line.startswith("{")]
        queue = next((line for line in server_events
                      if line.get("event") == "phase7_queue_drain"), None)
        if not queue or any(queue.get(field) != 0 for field in (
                "final_reliable_messages", "final_reliable_bytes",
                "final_latest_messages", "final_latest_bytes")):
            raise RuntimeError(f"queue drain evidence missing or nonzero: {server_output!r}")
        for field, cap in {
                "reliable_high_water_messages": 256,
                "reliable_high_water_bytes": 4 * 1024 * 1024,
                "latest_high_water_messages": 1,
                "latest_high_water_bytes": 65536}.items():
            if not 0 < queue.get(field, 0) <= cap:
                raise RuntimeError(f"queue bound failed: field={field} evidence={queue!r}")
        print(json.dumps({"event": "phase7_soak_passed", "duration_seconds": 60,
                          "matching_canonical_views": True, "memory": memory,
                          "queue_drain": queue}, separators=(",", ":")))
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
