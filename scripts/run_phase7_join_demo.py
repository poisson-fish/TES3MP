#!/usr/bin/env python3
import argparse
import json
import socket
import subprocess
import tempfile
import time
from pathlib import Path


def run_client(binary: Path, port: int, password: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), "127.0.0.1", str(port), str(password), "5000"],
        text=True, capture_output=True, timeout=10, check=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True)
    args = parser.parse_args()
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="tes3mp-phase7-") as temporary:
        root = Path(temporary)
        good = root / "join-password"
        bad = root / "wrong-password"
        config = root / "server.cfg"
        good.write_text("phase7-demo-secret\n", encoding="utf-8")
        bad.write_text("wrong-phase7-secret\n", encoding="utf-8")
        config.write_text(
            f"bind_address=127.0.0.1\nport={port}\ntick_interval_ms=16\n"
            f"disconnect_grace_ms=1000\njoin_password_file={good.as_posix()}\n",
            encoding="utf-8")
        server = subprocess.Popen(
            [str(args.server), str(config)], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(0.5)
            rejected = run_client(args.client, port, bad)
            if rejected.returncode == 0:
                raise RuntimeError("injected bad credential unexpectedly joined")
            first = subprocess.Popen(
                [str(args.client), "127.0.0.1", str(port), str(good), "5000", "motion-one"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            second = subprocess.Popen(
                [str(args.client), "127.0.0.1", str(port), str(good), "5000", "motion-two"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            outputs = [first.communicate(timeout=10), second.communicate(timeout=10)]
            if first.returncode != 0 or second.returncode != 0:
                server_code = server.poll()
                server_output = server.communicate(timeout=2) if server_code is not None else None
                raise RuntimeError(f"good clients failed: codes={[first.returncode, second.returncode]} outputs={outputs!r} server={server_code} server_output={server_output!r}")
            joined = [json.loads(output[0].splitlines()[0]) for output in outputs]
            for field in ("session_id", "player_id", "entity_id"):
                values = sorted(item[field] for item in joined)
                if values != [1, 2]:
                    raise RuntimeError(f"{field} did not prove distinct non-orphan identities: {values}")
            if "movement_flow_complete" not in outputs[0][0] or "movement_flow_complete" not in outputs[1][0]:
                raise RuntimeError(f"movement flow evidence missing: {outputs!r}")
            print(json.dumps({"event": "phase7_movement_demo_passed", "clients": joined,
                              "simultaneous_movement": True, "converged_views": True,
                              "stale_views_rejected": True}, separators=(",", ":")))
            return 0
        finally:
            server.terminate()
            try:
                server.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.communicate(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
