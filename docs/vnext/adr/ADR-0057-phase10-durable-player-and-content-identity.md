# ADR-0057: Phase 10 durable player and content identity

Status: **Implemented**

Date: 2026-09-05

Decision owner: project owner

## Decision

Implement the approved A/A foundation:

1. After ordinary password authentication, a fresh join receives a random,
   opaque 32-byte player credential. The client persists the secret; the server
   persists only its SHA-256 digest with server-owned `PlayerId`, `EntityId`,
   `AppearanceId`, and content-manifest identity. A later credential-authenticated
   join reattaches those stable player/entity IDs. Routing principals and bounded
   resume tokens remain separate and process-local.
2. Both peers send an exact nonzero 32-byte content-manifest identity during
   handshake. A mismatch rejects before authentication. Canonical protocol and
   state use manifest-scoped opaque numeric IDs for the initial interior cell,
   exterior worldspace, and appearance; OpenMW record names remain local adapter
   mappings.

The initial registry is bounded to 256 records and replaced atomically on disk.
IDs are monotonic and never reassigned after a committed identity. Expiration
still removes the visible canonical avatar; durable credential reattachment,
including after server restart, recreates it at the configured spawn with the
same player/entity identity.

## Compatibility and security

- The protocol profile advances from 1.0 to 1.1, and canonical checksum encoding
  advances to version 2 because manifest and appearance identity change the wire
  and canonical meanings.
- Malformed, duplicate, exhausted, wrong-manifest, or wrong-credential state
  fails closed without consuming canonical identity.
- A server may issue a player credential only for a fresh password join. Clients
  reject an unexpected credential during resume or credential reattachment so a
  server response cannot silently replace their durable secret.
- The desktop client writes the credential by atomic replacement. POSIX files
  are restricted to owner access; Windows relies on the containing directory ACL.
- The registry is an identity foundation, not character/world persistence. It
  does not persist transform, inventory, stats, scripting, moderation, or account
  data, and it does not add character creation or appearance selection.

## Consequences

- Password knowledge alone can create a new bounded player identity, while the
  dedicated credential selects an existing identity only after password success.
- Exact manifest agreement prevents peers with different content mappings from
  entering admission or resume paths.
- The one configured appearance preserves current visible-avatar behavior while
  making appearance identity explicit for later character systems.
- Registry format migration, credential recovery/rotation, account linking,
  multiple manifests, and general persistence require later decisions.

## Verification

Protocol, authentication, join, lifecycle, persistence, canonical checksum,
adapter, and configuration contracts cover mismatch rejection, credential
round trips, restart-stable reattachment, atomic failure, and appearance mapping.
Networking-enabled server/headless builds, the Phase 7 lifecycle integration
flow, and the full OpenMW desktop executable build pass.
