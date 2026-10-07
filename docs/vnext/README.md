# Multiplayer modded OpenMW

Build cooperative multiplayer OpenMW for compatible modpacks, including Tamriel
Rebuilt. Players explore/fight together and complete campaigns independently;
others cannot permanently block opportunities. Clients retain OpenMW graphics/visual mods.

## Chosen route

Refactor OpenMW content/mechanics/world/scripting on an authoritative dedicated
server. Separate simulation/presentation; make player context explicit. No
independent gameplay formulas, manual catalogs or quest language.

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

Networking remains engine-independent. [CURRENT.md](CURRENT.md) names the next
action and separates synthetic/live evidence; migrate with one writer.

M4 closed with owner sign-off, 2026-10-06; [CURRENT](CURRENT.md) retains evidence.
[M5](PLAN.md#m5---complete-scripted-quests-independently-and-together) is active.
The initial shared-script seam is implemented; host scheduling and desktop proof remain pending.

## Product boundaries

- Shared world, independent progression; others cannot permanently consume opportunities.
  The [M5 contract](DECISIONS.md#m5-implementation-contract) adopts personal consequences,
  local story instances and bounded recovery copies through proven acquisition routes.
  Its later party addition coordinates compatible stories with individual rewards;
  joining never merges histories. Private campaigns are not the default; quest edits
  cannot be required. Implementation and API compatibility still require evidence.
- Match gameplay plugins, ordering, scripts and resources. Visual-only
  differences may remain client-local.
- Target supported OpenMW APIs. MWSE/native-engine-only support and multiplayer
  script compatibility require evidence.
- Retain accepted two-desktop behavior. Preserve portability and VR interfaces;
  headset support, public scale and administration follow later.

## Start and resume

Read [CURRENT.md](CURRENT.md), [DEVELOPMENT.md](DEVELOPMENT.md), this overview and
the active [PLAN.md](PLAN.md) milestone. Consult [DECISIONS.md](DECISIONS.md)
for authority and architecture.

Keep OpenMW 0.51.0 baseline `f4bec41444214a7903bebd178389ca22ca13f646`.
[BASELINE_PROVENANCE.json](BASELINE_PROVENANCE.json),
[OPENMW_PATCH_REGISTRY.json](OPENMW_PATCH_REGISTRY.json) and existing `proofs/`
serve dependency tooling, not planning.

Code and tests establish implementation; documents establish intended behavior.
