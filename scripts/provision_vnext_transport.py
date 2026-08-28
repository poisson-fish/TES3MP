#!/usr/bin/env python3
"""Provision the exact verified Phase 6 transport dependency inputs."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
from typing import Any

import run_vnext_cares_proof as cares
import run_vnext_gamenetworkingsockets_proof as gns


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build" / "vnext-transport-dependencies" / "manifest.json"


class ProvisionError(RuntimeError):
    pass


def _require_file(path: pathlib.Path, description: str) -> pathlib.Path:
    if not path.is_file():
        raise ProvisionError(f"missing verified {description}: {path}")
    return path


def _require_directory(path: pathlib.Path, description: str) -> pathlib.Path:
    if not path.is_dir():
        raise ProvisionError(f"missing verified {description}: {path}")
    return path


def _run_proofs() -> None:
    subprocess.run([sys.executable, str(ROOT / "scripts/run_vnext_gamenetworkingsockets_proof.py")], check=True)
    subprocess.run([sys.executable, str(ROOT / "scripts/run_vnext_cares_proof.py")], check=True)


def _verify_license(source: pathlib.Path, value: dict[str, Any], description: str) -> None:
    license_path = _require_file(source / value["path"], f"{description} license")
    actual = gns.sha256_file(license_path)
    if actual != value["sha256"]:
        raise ProvisionError(
            f"{description} license SHA-256 mismatch: expected {value['sha256']}, got {actual}"
        )


def build_manifest() -> dict[str, Any]:
    gns_lock = gns.load_lock()
    cares_lock = cares.load_lock()
    gns_sources = {
        name: _require_directory(
            gns.SOURCE_DIR / dependency["source_directory"], f"{name} source"
        )
        for name, dependency in gns_lock["dependencies"].items()
    }
    cares_source = _require_directory(
        cares.SOURCE_DIR / cares_lock["dependency"]["source_directory"], "c-ares source"
    )
    for name, source in gns_sources.items():
        _verify_license(source, gns_lock["dependencies"][name]["license"], name)
    _verify_license(cares_source, cares_lock["dependency"]["license"], "c-ares")

    protobuf_config = _require_file(
        gns.PROTOBUF_INSTALL_DIR / "lib/cmake/protobuf/protobuf-config.cmake",
        "Protobuf package configuration",
    )
    _require_file(gns.OPENSSL_INSTALL_DIR / "include/openssl/ssl.h", "OpenSSL headers")
    gns_evidence = _require_file(gns.EVIDENCE_PATH, "GameNetworkingSockets evidence")
    cares_evidence = _require_file(cares.EVIDENCE_PATH, "c-ares evidence")

    return {
        "schema_version": 1,
        "production_profile": "tes3mp-phase6-slice6.1-static-caller-pumped",
        "locks": {
            "gamenetworkingsockets": {
                "path": gns.LOCK_PATH.relative_to(ROOT).as_posix(),
                "sha256": gns.sha256_file(gns.LOCK_PATH),
            },
            "cares": {
                "path": cares.LOCK_PATH.relative_to(ROOT).as_posix(),
                "sha256": cares.sha256_file(cares.LOCK_PATH),
            },
        },
        "sources": {
            **{name: source.resolve().as_posix() for name, source in gns_sources.items()},
            "cares": cares_source.resolve().as_posix(),
        },
        "installs": {
            "openssl": gns.OPENSSL_INSTALL_DIR.resolve().as_posix(),
            "protobuf": protobuf_config.parents[3].resolve().as_posix(),
        },
        "evidence": {
            "gamenetworkingsockets": {
                "path": gns_evidence.resolve().as_posix(),
                "sha256": gns.sha256_file(gns_evidence),
            },
            "cares": {
                "path": cares_evidence.resolve().as_posix(),
                "sha256": cares.sha256_file(cares_evidence),
            },
        },
        "build_profile": {
            "gamenetworkingsockets": gns_lock["build_profile"],
            "cares": cares_lock["build_profile"],
        },
        "production_claim": "exact verified static inputs for the private Slice 6.1 adapter only",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refresh", action="store_true", help="rerun both exact dependency proofs first")
    parser.add_argument("--output", type=pathlib.Path, default=OUTPUT)
    arguments = parser.parse_args()
    try:
        if arguments.refresh:
            _run_proofs()
        manifest = build_manifest()
        output = arguments.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"Verified transport dependency manifest: {output}")
        return 0
    except (ProvisionError, gns.ProofError, cares.ProofError, OSError, subprocess.CalledProcessError) as error:
        print(f"transport dependency provisioning failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
