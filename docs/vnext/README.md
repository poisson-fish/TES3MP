# TES3MP vNext overview

TES3MP vNext is a clean-break authoritative multiplayer implementation for
Morrowind on OpenMW 0.51. It replaces the TES3MP 0.8.x protocol, transport,
server, scripting API, saves, and engine patch set instead of porting them.

The first release target is desktop and PC-VR multiplayer on Windows, Linux,
and macOS. Standalone Quest support is conditional work after that release.
The project is not yet a playable TES3MP replacement; see [CURRENT.md](CURRENT.md)
for the precise implemented surface and remaining work.

## Architecture

```text
OpenMW / OpenMW-VR
        |
OpenMW provider and adapter
        |
reusable client session
        |
bounded versioned protocol
        |
owned transport boundary
        |
authoritative server core
        |
future scripting | persistence | operations
```

- [`components/tes3mp`](../../components/tes3mp) owns engine-independent
  protocol, transport, server-core, client-session, and test-support targets.
- [`apps/tes3mp-server`](../../apps/tes3mp-server) composes the dedicated server
  and bounded server content.
- [`apps/tes3mp-headless-client`](../../apps/tes3mp-headless-client) provides a
  scripted client for deterministic integration flows.
- [`apps/openmw/tes3mp`](../../apps/openmw/tes3mp) is the OpenMW-facing adapter
  and desktop provider implementation. Fork-specific VR tracking stays in the
  OpenMW-VR provider leaf.

The server is the only canonical gameplay writer. Clients submit semantic
intent; server reducers validate it and publish canonical results. Reliable
operations, latest-wins canonical samples, and ephemeral VR presentation data
remain separate.

## Compatibility

vNext does not support TES3MP 0.8.x wire compatibility, mixed old/new peers,
RakNet or CrabNet, the legacy CoreScripts API, legacy saves, or the old patch
set. Archived code may help identify gameplay requirements but is not an
implementation template.

The source baseline is OpenMW `openmw-0.51.0` at
`f4bec41444214a7903bebd178389ca22ca13f646`. Intentional differences and engine
patches are machine-recorded in [BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json)
and [OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json).

## Documentation

- [CURRENT.md](CURRENT.md): authoritative code inventory, limitations, and work
  still required
- [DEVELOPMENT.md](DEVELOPMENT.md): build, verification, and contribution flow
- [DECISIONS.md](DECISIONS.md): compact durable architecture and product rules
- [CONTENT_FORMATS.md](CONTENT_FORMATS.md): server content input contracts

Historical planning, review dialogue, and command transcripts live in Git
history rather than the active documentation tree.
