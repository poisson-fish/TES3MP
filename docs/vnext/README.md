# Multiplayer modded OpenMW

Build cooperative multiplayer OpenMW for compatible modpacks, including Tamriel
Rebuilt. Clients retain OpenMW graphics/visual mods.

## Chosen route

Authoritative OpenMW simulation, separate presentation and explicit player contexts.
No independent gameplay formulas, catalogs or quest language.

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

Engine-independent networking; [CURRENT](CURRENT.md) separates synthetic/live evidence.
Migrate with one writer.

M4 closed, 2026-10-06. [M5](PLAN.md#m5---complete-scripted-quests-independently-and-together)
is active: shared scripts, personal dialogue and staged filters.

## Product boundaries

- Shared world, independent progression; others cannot permanently consume opportunities.
  The [M5 contract](DECISIONS.md#m5-implementation-contract) adopts personal consequences,
  local story instances and bounded recovery copies through proven acquisition routes.
  Later parties coordinate compatible stories with individual rewards, never merged
  histories. Private campaigns/quest edits are not prerequisites; compatibility needs evidence.
- Match gameplay plugins, ordering, scripts and resources. Visual-only
  differences may remain client-local.
- OpenMW APIs are the target; MWSE/native-engine-only compatibility needs evidence.
- Retain accepted desktop behavior, portability and VR interfaces. Headsets, scale
  and administration follow later.

## Start and resume

Read [CURRENT](CURRENT.md), [DEVELOPMENT](DEVELOPMENT.md) and active [PLAN](PLAN.md);
[DECISIONS](DECISIONS.md) owns architecture/authority.

Keep OpenMW 0.51.0 baseline `f4bec41444214a7903bebd178389ca22ca13f646`.
[BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json),
[OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json) and existing `proofs/`
serve dependency tooling, not planning.
