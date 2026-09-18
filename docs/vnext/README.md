# Multiplayer modded OpenMW

The product is cooperative multiplayer OpenMW: load a compatible modpack such
as Tamriel Rebuilt, explore and fight together, and complete the game independently.
Other players must not permanently close a character's quest paths. Preserve
personal progression in a shared world, with NPC respawn restoring later access.
Clients retain OpenMW graphics and compatible visual mods. This is the target,
not a claim of implemented support.

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

Networking stays engine-independent. The native runtime currently owns bounded
inventory, equipment, container/corpse and world-item operations in one durable
image. [CURRENT.md](CURRENT.md) names verified behavior, limitations and the next
action. Each subsystem changes authority once, through small playable slices.

## Product boundaries

- One authoritative shared world; individual characters and progression.
  Cooperation must not require synchronized quest stages or attendance.
- [Cooperative progression design](DECISIONS.md#cooperative-progression-design)
  proposes shared NPC lives/respawn and personal story state through engine services.
  Quest-specific adaptations cannot be required. Temporary NPC unavailability is
  acceptable; personal worlds are not the default. M5 plans contextual scripts,
  deferred unavailable dependencies and scoped permanent changes; item access
  still needs generic replenishment/transfer rules.
  Rewards must not duplicate when clients retry.
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

The code baseline remains OpenMW 0.51.0 at
`f4bec41444214a7903bebd178389ca22ca13f646`; this pivot does not upgrade it.
[BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json) and
[OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json) are tooling inputs.
The existing `proofs/` sources remain because dependency tooling consumes them;
they are not an active roadmap or a place to add implementation diaries.

There are exactly five active documents and a 5,000-word combined ceiling.
Replace stale material rather than accumulating history. Source and executable
tests define implemented behavior; these documents define the intended change.
