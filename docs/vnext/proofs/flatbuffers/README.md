# ADR-0004 FlatBuffers selection proof

This directory is disposable selection evidence for the restricted FlatBuffers
profile approved in ADR-0004. It is not the Phase 4 production codec, protocol
schema, or a reusable runtime wrapper.

The proof:

- downloads the exact source archive and checks its SHA-256 before extraction;
- builds `flatc` from the pinned source and verifies its reported version;
- checks explicit schema IDs and compatible v1-to-v2 evolution;
- regenerates C++ and rejects drift from the committed generated headers;
- compiles generated accessors, builders, and the header-only verifier without
  OpenMW or transport dependencies;
- tests exact size-prefix and root-identifier handling, every truncation, corrupt
  offsets, excess depth/table work, unknown unions, collection/string bounds,
  UTF-8, finite/range numeric checks, no partial output, and owned lifetimes;
- generates deterministic valid and malformed seed corpora; and
- optionally runs the exact proof decoder under Clang libFuzzer, ASan, and UBSan.

Run from an environment containing Python 3, CMake, Ninja, and the selected C++
compiler:

```sh
python scripts/run_vnext_flatbuffers_proof.py
```

On Windows this must be an x64 Visual Studio 2022 developer environment. If
CMake or Ninja are not on `PATH`, set `VNEXT_CMAKE` and `VNEXT_NINJA` to their
executables. The Clang 18 Linux CI path also runs:

```sh
python3 scripts/run_vnext_flatbuffers_proof.py --fuzz-seconds 30
```

`--update-generated` is a deliberate maintenance operation for an approved
schema or pin change. A normal proof run never changes tracked source.

Retained evidence is written beneath `build/vnext-flatbuffers-proof/evidence/`.
The exact dependency, archive, license, generator arguments, excluded surfaces,
platform matrix, and update policy are in
[`vnext_flatbuffers_proof.json`](../../../../scripts/vnext_flatbuffers_proof.json).
