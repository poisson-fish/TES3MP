# FlatBuffers Android ARM64 feasibility assessment

Assessment date: 2026-08-26

The approved FlatBuffers profile does not require `flatc` to execute on Android.
The pinned host tool generates ordinary C++ headers, and the selected runtime
surface is header-only generated accessors, builders, and the verifier. It has
no operating-system, socket, renderer, OpenXR, JNI, or OpenMW dependency.

The proof builds those headers as C++20 on the supported desktop compilers. The
FlatBuffers `v25.12.19` CMake project itself requires only C++11 for the selected
surface, while Android support files and Android CI definitions are present in
the pinned source. No Quest or Android production job is added before Phase 23,
as required by ADR-0002.

The Android ARM64 feasibility conclusion is **supported in principle, not yet a
release gate**. Phase 23 must still cross-compile the then-current pinned runtime
and generated vNext schemas with its selected NDK. Failure there reopens
ADR-0004 before any target-specific workaround may leak into protocol types.

Evidence sources:

- [`ADR-0002`](../../adr/ADR-0002-platform-toolchain-policy.md)
- [`ADR-0004`](../../adr/ADR-0004-protocol-schema-codec-evolution-policy.md)
- [`vnext_flatbuffers_proof.json`](../../../../scripts/vnext_flatbuffers_proof.json)
- [FlatBuffers Android directory at the pin](https://github.com/google/flatbuffers/tree/v25.12.19/android)
- [FlatBuffers CMake configuration at the pin](https://github.com/google/flatbuffers/blob/v25.12.19/CMakeLists.txt)
