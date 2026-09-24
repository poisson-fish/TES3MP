# TES3MP vNext

TES3MP vNext targets cooperative multiplayer modded OpenMW: explore content such
as Tamriel Rebuilt, fight and quest together, and share actors, objects, time,
weather, and persistent world consequences while retaining OpenMW graphics.

The project is moving from a separate bounded gameplay implementation to an
OpenMW-backed authoritative server. Existing networking, sessions, replication,
and client integration are the migration base. A bounded native server runtime
now owns inventory, doors, one NPC's travel and physical weapon combat, including
durable death and corpse loot. Live graphical two-client combat and broad mod/quest
compatibility remain unfinished. See the current status and next slice below.

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
