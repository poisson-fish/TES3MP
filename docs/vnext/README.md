# Multiplayer modded OpenMW

The product is cooperative multiplayer OpenMW: load a compatible modpack such
as Tamriel Rebuilt, explore together, fight the same actors, complete quests
together, and share objects, time, weather, and persistent world consequences.
Clients retain OpenMW graphics and compatible visual mods. This is the target,
not a claim that the current implementation already supports it.

## Chosen route

Build an OpenMW-backed authoritative dedicated server. Reuse and refactor
OpenMW's content loading, world, inventory, mechanics, and scripting behavior.
Separate simulation from presentation and give gameplay an explicit player
context. Do not grow a second implementation of Morrowind behind manually
selected item, spell, actor, or quest catalogs.

```text
Shared gameplay loadout
          |
OpenMW content and gameplay runtime <--- authenticated player intent
          |                              through TES3MP sessions/transport
Server-owned world + player state
          |
Coherent persistence and committed replication
          |
OpenMW clients: input, prediction, UI, graphics, audio
```

Networking stays engine-independent. An app-local OpenMW runtime now owns bounded
plain-shirt transfer and equipment commands, coherent pair installation and recovery; the native loadout
probe and synthetic tests share it. Production server integration is still
unfinished. The existing server
and desktop adapter remain the working migration base; each subsystem changes
authority exactly once. Broad compatibility is the destination, reached through
small playable slices rather than an engine-wide rewrite before the first test.

## Product boundaries

- One authoritative shared world; individual characters, inventories, and stats.
- Start with a cooperative party sharing quest progress and world consequences.
  Dialogue and player-specific script operations need an explicit initiator;
  rewards and unique world items must not duplicate when clients retry.
- Match gameplay plugins, ordering, scripts, and gameplay-relevant resources.
  Purely visual texture/shader/settings differences may remain client-local.
- Target mods supported by the chosen OpenMW version. MWSE/native-engine-only
  behavior is not automatically supported. OpenMW mod compatibility alone does
  not prove multiplayer script compatibility.
- Develop the first two-client proof on desktop using the existing toolchain.
  Preserve portability and existing VR interfaces, but VR hardware, standalone
  headsets, large public servers, and administration are not prerequisites for
  proving the gameplay route.

## Start and resume

1. [CURRENT.md](CURRENT.md): actual state, active milestone, next concrete action.
2. [DEVELOPMENT.md](DEVELOPMENT.md): session, migration, and verification rules.
3. [PLAN.md](PLAN.md): ordered outcomes and acceptance criteria; read the active
   milestone, not an invented expansion of the entire roadmap.
4. [DECISIONS.md](DECISIONS.md): durable architecture and cooperative semantics.

A fresh implementation request can be: "Follow AGENTS.md. Implement the next
unfinished slice of the active milestone in docs/vnext/CURRENT.md using PLAN.md.
Verify it narrowly and replace the handoff with the next concrete action."

The code baseline remains OpenMW 0.51.0 at
`f4bec41444214a7903bebd178389ca22ca13f646`; this pivot does not upgrade it.
[BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json) and
[OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json) are tooling inputs.
The existing `proofs/` sources remain because dependency tooling consumes them;
they are not an active roadmap or a place to add implementation diaries.

There are exactly five active documents and a 5,000-word combined ceiling.
Replace stale material rather than accumulating history. Source and executable
tests define implemented behavior; these documents define the intended change.
