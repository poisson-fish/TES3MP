# TES3MP vNext

TES3MP vNext is a clean-break authoritative multiplayer implementation for
Morrowind on OpenMW 0.51. It is under active development and is not yet a
playable replacement for TES3MP 0.8.x.

The repository currently contains a dedicated server, a scripted headless
client, a reusable client session, and an OpenMW desktop/PC-VR adapter path.
Authentication, player identity, exact-cell interest, movement, actors,
interactive objects, inventory, equipment, containers, disconnect/resume, and
resynchronization have bounded implementations. Combat and the remaining game
systems are not implemented yet.

## Start here

- [Overview](docs/vnext/README.md)
- [Current implementation and unfinished work](docs/vnext/CURRENT.md)
- [Development and verification workflow](docs/vnext/DEVELOPMENT.md)
- [Durable engineering decisions](docs/vnext/DECISIONS.md)
- [Server content formats](docs/vnext/CONTENT_FORMATS.md)

The source baseline is OpenMW 0.51.0 at
`f4bec41444214a7903bebd178389ca22ca13f646`. Intentional differences are recorded
in [`docs/vnext/BASELINE_PROVENANCE.json`](docs/vnext/BASELINE_PROVENANCE.json).
TES3MP vNext is GPLv3; see [LICENSE](LICENSE).
