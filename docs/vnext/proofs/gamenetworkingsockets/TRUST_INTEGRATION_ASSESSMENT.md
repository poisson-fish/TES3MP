# GameNetworkingSockets v1.6.0 endpoint-trust integration assessment

Status: **Fail — owner decision required**

Date: 2026-08-26

## Scope

This is fail-fast research evidence for ADR-0005 acceptance test 12. It asks
whether the exact selected GameNetworkingSockets release can receive
per-deployment endpoint trust through its supported public API without Steam, a
universal signing secret, silent trust-on-first-use, or an invasive dependency
patch.

It is not a production wrapper, dependency build proof, or authorization to use
an internal upstream API.

## Source examined

- GameNetworkingSockets tag: `v1.6.0`
- Commit: `2cb93a06350bb065db53abdb0d87cf297e0bfd34`
- Public certificate API:
  [`include/steam/isteamnetworkingsockets.h`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/include/steam/isteamnetworkingsockets.h)
- Private socket implementation:
  [`src/steamnetworkingsockets/clientlib/csteamnetworkingsockets.h`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/src/steamnetworkingsockets/clientlib/csteamnetworkingsockets.h)
- Private certificate store:
  [`src/steamnetworkingsockets/steamnetworkingsockets_certstore.h`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/src/steamnetworkingsockets/steamnetworkingsockets_certstore.h)
  and
  [`steamnetworkingsockets_certstore.cpp`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/src/steamnetworkingsockets/steamnetworkingsockets_certstore.cpp)

The files were retrieved from the tagged GitHub repository through its contents
API and inspected locally on 2026-08-26.

## Findings

1. `ISteamNetworkingSockets` publicly exposes `GetCertificateRequest()` and
   `SetCertificate()`. These provision the local endpoint certificate; they do
   not configure which remote roots or certificates are trusted.
2. The concrete class has additional private/internal certificate and private-key
   machinery. It is not a supported adapter contract.
3. `CertStore_Reset()`, `CertStore_AddCertFromBase64()`, and
   `CertStore_AddKeyRevocation()` exist only in the private
   `SteamNetworkingSocketsLib` certificate-store header.
4. Unless `STEAMNETWORKINGSOCKETS_ALLOW_DYNAMIC_SELFSIGNED_CERTS` is compiled,
   the private store defines `STEAMNETWORKINGSOCKETS_HARDCODED_ROOT_CA_KEY` and
   trusts that single compile-time root.
5. The private add function explicitly reports that another self-signed root
   cannot be added when the hardcoded root configuration is active.
6. No public `v1.6.0` API was found to add, remove, freeze, or revoke a configured
   remote trust anchor at runtime.

## Security and operational consequence

A stock public-API integration cannot implement ADR-0005's approved configured
per-deployment trust policy. The remaining mechanisms would materially change
the approved architecture:

- compile all clients with one vNext root and operate a CA that issues every
  community server certificate;
- call private certificate-store symbols and bind vNext to unsupported internals;
- add and maintain an upstreamable public trust-store patch; or
- choose a transport with a supported endpoint-trust interface.

Disabling authentication, accepting an arbitrary self-signed certificate, or
using silent trust-on-first-use would fail ADR-0003's hostile-Internet/on-path
scenario and is not an eligible workaround.

## Gate result

ADR-0005 proposed acceptance test 12 fails for stock GameNetworkingSockets
`v1.6.0`. Per the accepted decision's own review trigger, ADR-0005 is reopened
before exact dependency locks, cross-platform builds, or channel/backpressure
proof code is added.

## Decision options now requiring owner review

### Option A1: narrow upstreamable trust-anchor API patch (recommended)

Add a small initialization-only public API that loads explicit self-signed trust
anchors and revocations into a fresh store, validates them, then freezes trust
before any listener or connection exists. Carry the patch only behind the owned
adapter, submit it upstream, and fail Phase 6 if the maintained patch cannot stay
small and reviewable.

This preserves the approved transport and avoids central infrastructure, but it
makes vNext temporarily responsible for a security-sensitive dependency patch
and its cross-platform/update proof.

### Option A2: operate a vNext certificate authority

Compile one vNext public root into clients and require operators to obtain
short-lived server certificates from a vNext service. This uses the upstream
model without a source patch, but creates a central online/offline issuance,
revocation, availability, privacy, abuse, and key-custody product that is not in
the current plan. It is not recommended.

### Option B: reopen transport selection

Reject GameNetworkingSockets and evaluate a maintained transport with supported
runtime endpoint trust. MsQuic remains technically attractive but lacks official
macOS support at the reviewed release, so this option requires additional
candidate/proof research before another recommendation. It delays Phase 2 but
avoids a security-critical dependency patch.

## Recommendation

Approve Option A1 only if the owner accepts a narrow, upstream-first dependency
patch as part of the selected-library proof and patch policy. The proof must
demonstrate explicit trust loading, validation, freeze-before-use, revocation,
wrong/expired/malformed root rejection, repeated initialization behavior, and no
access to broader private library state.

If owning that patch is unacceptable, choose Option B. Option A2 is not
recommended for an open community dedicated-server project.
