"""Run requested two-desktop combat families sequentially, stopping on the first failure."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def run(build, cases_file, output):
    if cases_file.stat().st_size > 65536:
        raise ValueError("Combat acceptance cases exceed input budget")
    cases = json.loads(cases_file.read_text())
    if not isinstance(cases, list) or not 1 <= len(cases) <= 16:
        raise ValueError("Combat acceptance requires 1..16 bounded cases")
    for case in cases:
        arguments = case.get("arguments", [])
        if not isinstance(arguments, list) or len(arguments) > 16 or any(
                not isinstance(arg, str) or len(arg) > 256
                or arg.split("=", 1)[0] in ("--build", "--content-config", "--output")
                for arg in arguments):
            raise ValueError("Invalid acceptance arguments or overridden capture provenance")
    output.mkdir(parents=True, exist_ok=False)
    script = Path(__file__).with_name("run_native_navigation_capture.py")
    runtime = {name: hashlib.sha256((build / name).read_bytes()).hexdigest()
               for name in ("openmw.exe", "tes3mp_server.exe")}
    results = []
    for case in cases:
        name = case["name"]
        if not name or len(name) > 64 or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-" for c in name):
            raise ValueError("Invalid acceptance case name")
        capture = output / name
        if "existing" in case:
            capture = Path(case["existing"]).resolve()
        else:
            config = Path(case["config"]).resolve()
            with (output / (name + ".log")).open("w", encoding="utf-8") as log:
                result = subprocess.run([sys.executable, str(script), "--build", str(build.resolve()),
                    "--content-config", str(config), "--output", str(capture), *case["arguments"]],
                    stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f"Combat acceptance failed: {name}; inspect {output / (name + '.log')}")
        report = json.loads((capture / "result.json").read_text())
        if not report.get("success"):
            raise RuntimeError(f"Combat acceptance did not confirm success: {name}")
        current = {name: hashlib.sha256((build / name).read_bytes()).hexdigest() for name in runtime}
        if current != runtime or (report.get("runtime") is not None and report["runtime"] != runtime) \
                or ("existing" in case and report.get("runtime") != runtime):
            raise RuntimeError(f"Combat acceptance runtime changed or is unverified: {name}")
        report["runtime"] = runtime
        (capture / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        if case.get("presentation"):
            from verify_actor_presentation import verify
            frames = verify(capture, **case["presentation"])
            (capture / "presentation-validation.json").write_text(json.dumps(frames, indent=2) + "\n")
        results.append(dict(name=name, capture=str(capture), success=True))
        print(f"PASS {name}", flush=True)
    (output / "result.json").write_text(json.dumps(dict(success=True, runtime=runtime, families=results), indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.build, args.cases, args.output.resolve())
