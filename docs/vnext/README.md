# Multiplayer modded OpenMW

Cooperative multiplayer OpenMW for compatible modpacks, including Tamriel
Rebuilt, retaining client graphics/visual mods.

## Chosen route

Authoritative OpenMW simulation, separate presentation and explicit player contexts.
No independent gameplay formulas, catalogs or quest language.

Authenticated intent enters server OpenMW; coherent commits persist world/player
state and replicate to client prediction/UI/graphics/audio.

Engine-independent networking; one migration writer. [CURRENT](CURRENT.md) owns evidence.
M4 closed, 2026-10-06; [M5](PLAN.md#m5---complete-scripted-quests-independently-and-together) is active.

## Product boundaries

- Shared world, independent progression; others cannot permanently consume opportunities.
  The [M5 contract](DECISIONS.md#m5-implementation-contract) owns personal consequences,
  instances, proven acquisition/recovery and later parties with individual rewards.
  Histories never merge; private campaigns/quest edits are not prerequisites.
- Match gameplay plugins, ordering, scripts and resources. Visual-only
  differences may remain client-local.
- OpenMW APIs are the target; MWSE/native-engine-only compatibility needs evidence.
- Retain desktop behavior, portability and VR interfaces; headsets/scale/administration follow later.

## Start and resume

Keep OpenMW 0.51.0 baseline `f4bec41444214a7903bebd178389ca22ca13f646`.
[BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json),
[OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json) and existing `proofs/`
serve dependency tooling, not planning.
