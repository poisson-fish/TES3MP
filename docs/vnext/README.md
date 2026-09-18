# Multiplayer modded OpenMW

Build cooperative multiplayer OpenMW for compatible modpacks, including Tamriel
Rebuilt. Players explore and fight together and complete campaigns independently;
another's actions or absence must not permanently block their opportunities.
Shared NPC respawn and personal progression define the target, not implemented
support. Clients retain OpenMW graphics and compatible visual mods.

## Chosen route

Reuse and refactor OpenMW content, mechanics, world and scripting on an authoritative
dedicated server. Separate simulation from presentation and make player context explicit.
Do not create independent gameplay formulas, manual catalogs or a quest language.

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

Networking remains engine-independent. [CURRENT.md](CURRENT.md) names native runtime
coverage, evidence, limitations and next action. Migrate one subsystem through
small playable slices, preserving one canonical writer.

## Product boundaries

- Shared world, independent progression; no synchronized attendance or quest stages.
  [Progression design](DECISIONS.md#cooperative-progression-design) proposes shared
  NPC lives and personal story state. Temporary unavailability is acceptable;
  private campaigns are not the default; quest adaptations cannot be required. M5 owns
  contextual scripts, deferred dependencies and scoped permanent changes.
  Generic item replenishment and access remain unresolved; retries cannot duplicate rewards.
- Match gameplay plugins, ordering, scripts and relevant resources. Purely visual
  texture/shader/settings differences may remain client-local.
- Target supported OpenMW APIs. MWSE/native-engine-only support and multiplayer
  script compatibility require evidence.
- Prove two desktop clients first. Preserve portability and VR interfaces; VR hardware,
  standalone headsets, public-server scale and administration are later concerns.

## Start and resume

Read [CURRENT.md](CURRENT.md), [DEVELOPMENT.md](DEVELOPMENT.md), this overview and
the active milestone in [PLAN.md](PLAN.md). Consult [DECISIONS.md](DECISIONS.md)
for architecture and authority.

Keep OpenMW 0.51.0 baseline `f4bec41444214a7903bebd178389ca22ca13f646`.
[BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json),
[OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json) and existing `proofs/`
serve dependency tooling, not planning.

Exactly five active documents share 5,000 words. Replace stale prose; add no
diaries or duplicate roadmaps. Code and tests establish implementation; documents
establish intended behavior.
