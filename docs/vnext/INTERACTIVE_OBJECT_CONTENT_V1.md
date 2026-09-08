# Interactive object content V1

The dedicated server can load a bounded, manifest-scoped interactive-object
catalog before it starts listening. Set the optional server configuration key:

```text
interactive_object_content_file = objects.txt
```

When the key is omitted, interactive-object replication is disabled and the
server does not advertise `interactiveObjectReplicationCapability()`. When the
key is present, any unavailable, oversized, malformed, or manifest-incompatible
artifact fails startup instead of silently running with a partial catalog.

## Format

The artifact is UTF-8-compatible ASCII tokens separated by spaces or tabs.
Blank lines are allowed; comments and quoted values are not. The maximum file
size is 2 MiB. The first line and one manifest declaration are required:

```text
TES3MP_INTERACTIVE_OBJECTS_V1
manifest 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

Each remaining line declares one object:

```text
object <id> <standard|teleport> <cell> <px> <py> <pz> <ox> <oy> <oz> <lock-level> <key-id|none> <trap-id|none> [<destination-cell> <dpx> <dpy> <dpz> <dox> <doy> <doz>]
```

A cell is either `interior <space-id>` or
`exterior <space-id> <grid-x> <grid-y>`. Positions are signed fixed-point
canonical coordinates in the inclusive 32-bit range. Orientations are unsigned
32-bit turn values. IDs are nonzero unsigned 64-bit values.

Standard doors must not include a destination. Teleport doors must include one.
A positive lock level initializes the object as locked; a key ID may only be
declared for a locked object. A trap ID initializes the trap as armed; `none`
initializes it as disarmed.

Example:

```text
TES3MP_INTERACTIVE_OBJECTS_V1
manifest 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
object 1 standard interior 7 100 20 30 0 0 0 25 900 none
object 2 teleport interior 7 200 20 30 0 0 0 0 none 44 exterior 8 0 0 500 20 30 0 0 0
```

The loader accepts at most 16,384 objects globally and 512 objects in one exact
cell. Object IDs must be unique. Source and destination cells must exist in the
configured content manifest, and every embedded transform must identify the
same cell as its declaration.

## Authority boundary

Clients submit only interaction intent. The canonical server validates the
object, cell, reach, tick, and expected revision before changing state. Network
`UnlockWithKey` commands are rejected until Phase 15 provides authoritative
inventory ownership; an empty or client-claimed key set is never treated as
proof of possession. The engine-independent reducer already accepts an explicit
server-verified key set for that later integration.
