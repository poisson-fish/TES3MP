# GameNetworkingSockets v1.6.0 endpoint-trust integration assessment

Status: **Resolved by approved scope change — no patch required**

Date: 2026-08-26

## Scope

This retained fail-fast research evidence asked whether GameNetworkingSockets
`v1.6.0` could receive per-deployment endpoint trust through its supported public
API without Steam, a universal signing secret, or an invasive dependency patch.

The assessment originally reopened ADR-0005. The owner subsequently approved a
simpler community-server security profile that uses the library's automatic
basic encryption without authenticated endpoint identity. The missing trust
integration is therefore no longer a selection gate, and no upstream or local
source patch is authorized.

## Source examined

- GameNetworkingSockets tag: `v1.6.0`
- Commit: `2cb93a06350bb065db53abdb0d87cf297e0bfd34`
- Public certificate API:
  [`include/steam/isteamnetworkingsockets.h`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/include/steam/isteamnetworkingsockets.h)
- Private socket implementation:
  [`src/steamnetworkingsockets/clientlib/csteamnetworkingsockets.h`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/src/steamnetworkingsockets/clientlib/csteamnetworkingsockets.h)
- Private certificate store:
  [`steamnetworkingsockets_certstore.h`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/src/steamnetworkingsockets/steamnetworkingsockets_certstore.h)
  and
  [`steamnetworkingsockets_certstore.cpp`](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/src/steamnetworkingsockets/steamnetworkingsockets_certstore.cpp)

The exact tagged files were inspected on 2026-08-26.

## Findings

1. The public `GetCertificateRequest()` and `SetCertificate()` functions
   provision a local endpoint certificate; they do not configure arbitrary
   remote trust roots.
2. The runtime certificate-store operations needed by the original proposal are
   private implementation APIs.
3. The normal non-Steam model instead selects a hardcoded root at compile time.
4. No supported public `v1.6.0` API was found to add, remove, freeze, or revoke a
   per-server remote trust anchor at runtime.
5. The public API documentation states that connections receive basic encryption
   by default but remain vulnerable to man-in-the-middle attacks without
   certificates or another out-of-band shared-secret mechanism.

## Original gate result

Stock GameNetworkingSockets `v1.6.0` could not satisfy the original configured
per-server endpoint-trust acceptance test. The technically possible alternatives
were:

- maintain an upstream-oriented trust-store API patch;
- operate a vNext certificate authority and compile its root into clients; or
- select another transport with a supported endpoint-verification API.

Each alternative added maintenance or operator infrastructure beyond the first
community-server milestone.

## Approved disposition

On 2026-08-26 the owner approved amending ADR-0005 to use the supported
unauthenticated direct-IP mode with automatic encryption. The decision explicitly
accepts active endpoint impersonation risk for the first milestone while keeping
encryption against passive observation, application-level join authentication,
secret redaction, rate limits, bounded inputs, and single-use resume tokens.

Consequently:

- the proposed trust-anchor API patch is rejected;
- a vNext certificate authority is rejected;
- operators will not manage transport certificates, trust anchors, pins, or
  revocation lists;
- no private GameNetworkingSockets certificate-store API may be called; and
- the dependency proof must verify encryption is active and unencrypted
  production operation fails, but it must not claim authenticated server
  identity.

The accepted boundary and review triggers are authoritative in
[ADR-0005](../../adr/ADR-0005-transport-security-authentication-resumption.md).
