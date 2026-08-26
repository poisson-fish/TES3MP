# GameNetworkingSockets selection proof

This directory contains disposable evidence for the restricted transport profile
accepted by [ADR-0005](../../adr/ADR-0005-transport-security-authentication-resumption.md).
It is not the production Phase 6 transport wrapper, and none of its proof-only
types or budgets are a production interface.

The exact dependency lock is
[`scripts/vnext_gamenetworkingsockets_proof.json`](../../../../scripts/vnext_gamenetworkingsockets_proof.json).
It pins GameNetworkingSockets 1.6.0, OpenSSL 3.5.8 LTS, Protobuf C++ 6.33.4,
Abseil 20250512.1, and the Protobuf-bundled `utf8_range` license. The runner
downloads only the locked HTTPS archives, verifies every archive and license
hash before building, and uses no dependency-manager resolution or network
fetch during CMake configuration. It also pins the vulnerability-review sources
and requires GameNetworkingSockets protobuf C++ to be regenerated only during
the proof with the locked `protoc`.

The profile builds static libraries, uses OpenSSL, and disables Steam services,
P2P, relay, ICE, WebRTC, examples, upstream tests, tools, shared libraries, and
unencrypted production operation. On Windows it uses the static MSVC runtime
and OpenSSL's portable `no-asm` configuration.

## Run locally

Run:

```text
python scripts/run_vnext_gamenetworkingsockets_proof.py
```

The Windows job requires the Visual Studio 2022 x64 developer environment,
CMake, Ninja, `nmake`, and native Strawberry Perl. Linux and macOS require a C++
compiler, CMake, Ninja, Make, and Perl. Tool paths can be supplied through
`VNEXT_CMAKE`, `VNEXT_NINJA`, `VNEXT_CTEST`, `VNEXT_MAKE`, and `VNEXT_PERL`.
The Linux Clang job also passes `--sanitize` for ASan and UBSan.

The runner retains exact archive, source commit, license, build-profile,
toolchain, platform, budget, and verbose scenario evidence under
`build/vnext-gamenetworkingsockets-proof/evidence/`. Credential and resume-token
canaries must not occur in retained evidence.

## Proven behavior

The proof exercises a real direct-IP client/server connection through a bounded
UDP capture proxy and verifies that encryption is active and plaintext requests
fail closed. It checks post-encryption authentication ordering, open and
password-protected joins, rate limiting and redaction, password and resume-token
wire canaries, single-use resume rotation under contention, expiry/context/
generation rejection, teardown generations, bounded reliable/latest-wins
queues, reliable ordering under faults, and separate lanes that let a new
unreliable sample pass a delayed reliable fragment. The real transport harness
also verifies slow-reader receive byte/message caps, excessive per-packet
segments, fail-closed maximum-message handling, bounded concurrent handshake and
disconnect floods, bounded callback draining, recovery after dropped unreliable
work, and the documented close behavior that discards unread data and
invalidates the connection handle.

The capture proves resistance to passive observation for the exercised
canaries. It makes no endpoint-authentication claim; ADR-0005 explicitly accepts
active server impersonation for the first milestone.

All approved scenarios now pass locally on Windows MSVC 2022. Slice 2.3 stays
**In Progress** until the complete hosted matrix and retained artifacts have
passed consistency review and the owner accepts the completion evidence.
Android ARM64 is a separate feasibility result, recorded in
[ANDROID_ARM64.md](ANDROID_ARM64.md).
