#!/usr/bin/env python3
import argparse
import ctypes
import json
import statistics
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def run_client(binary: Path, port: int, password: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), "127.0.0.1", str(port), str(password), "5000"],
        text=True, capture_output=True, timeout=10, check=False)


def resident_bytes(pid: int) -> int:
    if sys.platform == "win32":
        class ProcessMemoryCounters(ctypes.Structure):
            _fields_ = [
                ("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong),
                ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t),
            ]
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        ctypes.windll.kernel32.OpenProcess.restype = ctypes.c_void_p
        ctypes.windll.kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        ctypes.windll.psapi.GetProcessMemoryInfo.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
        handle = ctypes.windll.kernel32.OpenProcess(0x1000 | 0x0400, False, pid)
        if not handle:
            raise RuntimeError(f"cannot inspect process memory: pid={pid}")
        try:
            if not ctypes.windll.psapi.GetProcessMemoryInfo(
                    handle, ctypes.byref(counters), counters.cb):
                raise RuntimeError(f"cannot sample process memory: pid={pid}")
            return int(counters.WorkingSetSize)
        finally:
            ctypes.windll.kernel32.CloseHandle(handle)
    if sys.platform.startswith("linux"):
        for line in Path(f"/proc/{pid}/status").read_text(encoding="ascii").splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) * 1024
        raise RuntimeError(f"resident memory unavailable: pid={pid}")
    if sys.platform == "darwin":
        measured = subprocess.run(
            ["ps", "-o", "rss=", "-p", str(pid)], text=True,
            capture_output=True, timeout=5, check=False)
        if measured.returncode == 0 and measured.stdout.strip():
            return int(measured.stdout.strip()) * 1024
    raise RuntimeError(f"resident memory sampling unsupported: platform={sys.platform}")


def bounded_rss(samples: list[int]) -> dict[str, int]:
    if len(samples) < 40:
        raise RuntimeError(f"insufficient RSS samples: count={len(samples)}")
    window = 10
    reference = samples[len(samples) // 2 - window:len(samples) // 2]
    final = samples[-window:]
    reference_median = int(statistics.median(reference))
    final_median = int(statistics.median(final))
    observed_noise = max(reference) - min(reference)
    if final_median > reference_median + observed_noise:
        raise RuntimeError(
            "sustained RSS growth: "
            f"reference_median={reference_median} final_median={final_median} "
            f"observed_noise={observed_noise}")
    return {"samples": len(samples), "reference_median_bytes": reference_median,
            "final_median_bytes": final_median, "observed_noise_bytes": observed_noise,
            "peak_bytes": max(samples)}


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
            f"disconnect_grace_ms=3000\njoin_password_file={good.as_posix()}\n",
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
            time.sleep(4.2)
            observer = subprocess.Popen(
                [str(args.client), "127.0.0.1", str(port), str(good), "5000", "observer"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(0.1)
            lifecycle = subprocess.run(
                [str(args.client), "127.0.0.1", str(port), str(good), "5000", "lifecycle"],
                text=True, capture_output=True, timeout=20, check=False)
            observer_output = observer.communicate(timeout=10)
            if lifecycle.returncode != 0 or observer.returncode != 0:
                raise RuntimeError(
                    f"lifecycle clients failed: lifecycle={lifecycle!r} observer={observer_output!r}")
            lifecycle_lines = [json.loads(line) for line in lifecycle.stdout.splitlines() if line]
            lifecycle_event = next((line for line in lifecycle_lines
                                    if line.get("event") == "lifecycle_flow_complete"), None)
            if not lifecycle_event or "fixture_flow_complete" not in observer_output[0]:
                raise RuntimeError(
                    f"lifecycle evidence missing: lifecycle={lifecycle.stdout!r} observer={observer_output!r}")
            reconnect = subprocess.run(
                [str(args.client), "127.0.0.1", str(port), str(good), "5000", "reconnect"],
                text=True, capture_output=True, timeout=60, check=False)
            reconnect_lines = [json.loads(line) for line in reconnect.stdout.splitlines() if line]
            reconnect_event = next((line for line in reconnect_lines
                                    if line.get("event") == "reconnect_flow_complete"), None)
            if reconnect.returncode != 0 or not reconnect_event or reconnect_event.get("reconnect_cycles") != 32:
                raise RuntimeError(f"reconnect cycle threshold failed: {reconnect!r}")
            result = {"event": "phase7_lifecycle_demo_passed", "clients": joined,
                              "simultaneous_movement": True, "converged_views": True,
                              "stale_views_rejected": True, "hidden_during_grace": True,
                              "reconnect_cycles": reconnect_event["reconnect_cycles"],
                              "same_identity_resumed": lifecycle_event["identity_preserved"],
                              "progress_preserved": lifecycle_event["progress_preserved"],
                              "expired_resume_rejected": lifecycle_event["expired_resume_rejected"],
                              "fresh_identity_created": lifecycle_event["fresh_identity_created"]}
        finally:
            server.terminate()
            try:
                server_output = server.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server_output = server.communicate(timeout=5)
        server_lines = [json.loads(line) for line in server_output[0].splitlines()
                        if line.startswith("{")]
        queue_event = next((line for line in server_lines
                            if line.get("event") == "phase7_queue_drain"), None)
        if not queue_event:
            raise RuntimeError(f"queue drain evidence missing: server={server_output!r}")
        caps = {
            "reliable_high_water_messages": 256,
            "reliable_high_water_bytes": 4 * 1024 * 1024,
            "latest_high_water_messages": 1,
            "latest_high_water_bytes": 65536,
        }
        for field, cap in caps.items():
            if not 0 < queue_event.get(field, 0) <= cap:
                raise RuntimeError(f"queue bound failed: field={field} evidence={queue_event!r}")
        final_fields = ("final_reliable_messages", "final_reliable_bytes",
                        "final_latest_messages", "final_latest_bytes")
        if any(queue_event.get(field) != 0 for field in final_fields):
            raise RuntimeError(f"queue zero drain failed: {queue_event!r}")
        result["queue_drain"] = queue_event
        print(json.dumps(result, separators=(",", ":")))
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
