# TES3MP vNext

TES3MP vNext targets cooperative multiplayer modded OpenMW: explore content such
as Tamriel Rebuilt, fight and quest together, and share actors, objects, time,
weather, and persistent world consequences while retaining OpenMW graphics.

An OpenMW-backed authoritative server owns bounded shared inventory, doors,
NPC simulation, melee/ranged/magic combat, effects, death, corpse loot and recovery.
M4 is closed following completed acceptance and owner sign-off on 2026-10-06,
including V79 player physics cutover and combined two-client/NPC combat acceptance.
M5 is the active milestone: authoritative shared scripting and independent quest
progression. Implementation is unstarted; broad mod/quest compatibility remains
a target. See the current status and next slice below.

## Start here

- [Overview](docs/vnext/README.md)
- [Current implementation and unfinished work](docs/vnext/CURRENT.md)
- [Development and verification workflow](docs/vnext/DEVELOPMENT.md)
- [Durable engineering decisions](docs/vnext/DECISIONS.md)
- [Implementation milestones](docs/vnext/PLAN.md)

The source baseline is OpenMW 0.51.0 at
`f4bec41444214a7903bebd178389ca22ca13f646`. Intentional differences are recorded
in [`docs/vnext/BASELINE_PROVENANCE.json`](docs/vnext/BASELINE_PROVENANCE.json).
TES3MP vNext is GPLv3; see [LICENSE](LICENSE).
