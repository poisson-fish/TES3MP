# c-ares selection proof

This directory contains disposable dependency-selection evidence authorized by
[ADR-0032](../../adr/ADR-0032-phase6-transport-adapter-and-lifecycle-boundary.md).
It is not the Phase 6 resolver or transport adapter, and none of its fixture
types are a production interface.

The exact lock is
[`scripts/vnext_cares_proof.json`](../../../../scripts/vnext_cares_proof.json).
The runner downloads the locked c-ares 1.34.8 release archive, verifies the
archive and MIT-license hashes before building, and configures only the static
library. Tools, upstream tests, installation, shared libraries, c-ares event
threads, and the c-ares query cache are outside the selected profile.

The proof uses an in-process loopback DNS fixture and the public
`ares_getaddrinfo`, socket-state callback, `ares_process_fds`, `ares_cancel`,
and `ares_destroy` APIs. It covers IPv4-only, IPv6-only, dual-stack, NXDOMAIN,
no-data, timeout, malformed-response, cancellation, destruction, duplicate,
and more-than-eight-answer cases without depending on public DNS. It also
demonstrates a separate numeric-address fast path, port propagation, owned
result copying/deduplication, and the approved eight-result bound. It makes no
DNSSEC, encrypted-DNS, application-cache, connection-ordering, or production
integration claim.

Run locally from the repository root:

```text
python scripts/run_vnext_cares_proof.py
```

Linux Clang 18 CI additionally passes `--sanitize` for ASan+UBSan. The manually
dispatched workflow retains the exact lock, license, toolchain, scenario output,
and machine-readable evidence for all five supported desktop compiler jobs.
