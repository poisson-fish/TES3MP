# TES3MP actor content V1

The dedicated server requires one bounded, manifest-scoped actor artifact. It
is configuration content, not persistent world state.

## Format

The file is ASCII text, at most 2 MiB. Blank lines are allowed after the header.
Tokens are separated by ASCII whitespace.

```text
TES3MP_ACTORS_V1
manifest <64-lowercase-hex-digits>
actor <actor-id> <entity-id> <prototype-id> interior <cell-space-id> <x> <y> <z> <rot-x> <rot-y> <rot-z> <idle|travel|wander> [<waypoint-x> <waypoint-y> <waypoint-z> ...]
actor <actor-id> <entity-id> <prototype-id> exterior <worldspace-id> <grid-x> <grid-y> <x> <y> <z> <rot-x> <rot-y> <rot-z> <idle|travel|wander> [<waypoint-x> <waypoint-y> <waypoint-z> ...]
```

Exactly one manifest declaration is required. Actor, entity, prototype, cell,
and worldspace identifiers are nonzero typed integers. Positions and waypoint
coordinates are signed 32-bit fixed-point values; orientations are unsigned
32-bit turn values. Actor IDs and entity IDs are unique. Exterior grid values
are signed 32-bit integers.

The artifact contains at most 4,096 actors and at most 248 actors in one exact
cell. `idle` has no waypoints. `travel` and `wander` have 1–32 waypoint triples.
Actors appear in ascending actor-ID order.

## Composition and presentation

Startup requires the artifact manifest to equal the configured content
manifest. Every actor spawn and waypoint must name a configured exact cell and
must be occupiable according to the configured collision artifact. Actor entity
IDs are reserved before player identity allocation so the two namespaces remain
disjoint.

Clients map prototype IDs locally with repeated
`tes3mp-content-actor-prototype-map <id>=<NPC-or-creature-record>` options. The
server artifact never carries local record names or paths. A negotiated actor
client needs a complete local mapping for every presented prototype.

Missing, oversized, malformed, duplicate, mismatched, unknown-cell, or
collision-invalid content stops server composition before listen. There is no
partial catalog acceptance.

## Exclusions

V1 does not define combat, death, deletion, inventory, equipment, dialogue,
scripts, persistence, pathfinding, dynamic collision, client simulation, or
authority delegation. Actor movement is server-owned idle/travel/wander at the
fixed tick; exact cells without an active player remain frozen.
