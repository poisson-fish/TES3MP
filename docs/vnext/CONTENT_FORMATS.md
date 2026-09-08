# TES3MP vNext server content formats

These formats seed bounded, manifest-scoped server state. They are configuration
inputs, not persistence. Source parsers and their tests are authoritative.

All formats are whitespace-separated ASCII-compatible text with a required
header and exactly one manifest record. Missing, oversized, malformed,
duplicate, unknown-cell, collision-invalid, or manifest-mismatched input fails
server startup; partial catalogs are never accepted.

## Collision V1

Configured by `collision_content_file`; required by the production server.
Maximum size is 2 MiB with at most 32,768 unique solids.

```text
TES3MP_COLLISION_V1
manifest <64-hex-digits>
cell interior <cell-space-id>
cell exterior <worldspace-id> <grid-x> <grid-y>
solid interior <cell-space-id> <min-x> <min-y> <min-z> <max-x> <max-y> <max-z>
solid exterior <worldspace-id> <grid-x> <grid-y> <min-x> <min-y> <min-z> <max-x> <max-y> <max-z>
```

Every configured exact cell appears once as `cell`; no other cell is accepted.
Coordinates are signed integers in `[-2147483647, 2147483647]` and every minimum
is below its maximum. Solids are pre-inflated forbidden volumes for canonical
root segments, not render/physics meshes. Contact retains the current position
with zero velocity. Explicit cells without solids are unobstructed.

This format does not provide gravity, slopes, capsule dimensions, dynamic
bodies, cell traversal, or client collision authority. See
[`content_collision.cpp`](../../apps/tes3mp-server/content_collision.cpp).

## Actors V1

Configured by `actor_content_file`; required by the production server. Maximum
size is 2 MiB, with at most 4,096 actors, 248 actors in one exact cell, and 32
waypoints per actor.

```text
TES3MP_ACTORS_V1
manifest <64-lowercase-hex-digits>
actor <actor-id> <entity-id> <prototype-id> interior <cell-space-id> <x> <y> <z> <rot-x> <rot-y> <rot-z> <idle|travel|wander> [<waypoint-x> <waypoint-y> <waypoint-z> ...]
actor <actor-id> <entity-id> <prototype-id> exterior <worldspace-id> <grid-x> <grid-y> <x> <y> <z> <rot-x> <rot-y> <rot-z> <idle|travel|wander> [<waypoint-x> <waypoint-y> <waypoint-z> ...]
```

IDs are nonzero; actor and entity IDs are unique. Positions/waypoints are signed
32-bit fixed-point values and orientations are unsigned 32-bit turns. `idle`
has no waypoints; `travel` and `wander` have 1–32. Records are ordered by actor
ID. All roots and waypoints must be declared and occupiable collision content.
Actor entity IDs are reserved before player allocation.

Clients map prototype IDs locally with repeated
`tes3mp-content-actor-prototype-map <id>=<record>` options. See
[`actor_content.cpp`](../../apps/tes3mp-server/actor_content.cpp).

## Interactive objects V1

Configured optionally by `interactive_object_content_file`. Omission disables
the capability. Maximum size is 2 MiB, with at most 16,384 objects and 512 in
one exact cell.

```text
TES3MP_INTERACTIVE_OBJECTS_V1
manifest <64-hex-digits>
object <id> <standard|teleport> <cell> <px> <py> <pz> <ox> <oy> <oz> <lock-level> <key-id|none> <trap-id|none> [<destination-cell> <dpx> <dpy> <dpz> <dox> <doy> <doz>]
```

`<cell>` is `interior <space-id>` or
`exterior <worldspace-id> <grid-x> <grid-y>`. IDs are nonzero unsigned 64-bit
values. Coordinates are signed fixed-point 32-bit values and rotations are
unsigned 32-bit turns. Standard doors have no destination; teleport doors have
one. A key is valid only on an initially locked object. Trap `none` starts
disarmed. Source/destination cells and transforms must belong to the manifest.

Clients submit interaction intent only. Canonical validation covers object,
cell, reach, tick, expected revision, state, and independently verified key
ownership. See [`interactive_object_content.cpp`](../../apps/tes3mp-server/interactive_object_content.cpp).

## Inventory V1

Configured optionally by `inventory_content_file`. Omission disables the
capability. Maximum size is 8 MiB. Current canonical bounds are 65,536 item
prototypes, 256 player inventories, 65,536 containers, 65,536 ground stacks,
1,024 stacks per player, and 512 stacks per container. Complete reliable views
must also fit the configured per-cell outbound bound.

```text
TES3MP_INVENTORY_V1
manifest <64-lowercase-hex-digits>
prototype <id> <category> <weight> <value> <max-condition> <max-charge> <slot-mask> <stackable-0-or-1> <key-id-or-none>
container <id> <interior space-id | exterior worldspace-id grid-x grid-y> <x> <y> <z> <capacity-weight>
container_item <container-id> <stack-id> <prototype-id> <count> <condition> <charge> <soul-actor-prototype-id-or-none>
ground_item <stack-id> <prototype-id> <count> <condition> <charge> <soul-actor-prototype-id-or-none> <interior space-id | exterior worldspace-id grid-x grid-y> <x> <y> <z>
```

Categories are the closed numeric `ItemCategory` range 0–11. Slot masks use the
19 canonical `EquipmentSlot` bits. Zero container capacity means unlimited.
Stack IDs are globally unique across initial locations. Container and ground
positions must be declared collision cells and occupiable.

OpenMW clients bind opaque IDs locally with repeatable
`--tes3mp-content-item-prototype-map <id>=<item-record>` and
`--tes3mp-content-container-map <id>=<ref-num-index>[:<content-file>]` options.
Mappings must be injective and complete for presented records. See
[`inventory_content.cpp`](../../apps/tes3mp-server/inventory_content.cpp).
