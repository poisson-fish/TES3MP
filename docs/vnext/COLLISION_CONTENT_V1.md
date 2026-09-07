# Collision content V1

The dedicated server requires `collision_content_file` to name a bounded static
collision artifact. The artifact is server-only and bound to the configured
content manifest before the listener starts.

## Format

The file is UTF-8-compatible ASCII, at most 2 MiB, with this exact header and
record vocabulary:

```text
TES3MP_COLLISION_V1
manifest <64-lower-or-upper-hex-digits>
cell interior <cell-space-id>
cell exterior <worldspace-id> <grid-x> <grid-y>
solid interior <cell-space-id> <min-x> <min-y> <min-z> <max-x> <max-y> <max-z>
solid exterior <worldspace-id> <grid-x> <grid-y> <min-x> <min-y> <min-z> <max-x> <max-y> <max-z>
```

Blank lines are allowed after the header. The manifest record appears exactly
once. Every cell in the configured manifest appears exactly once as a `cell`
record, and no other cell is accepted. A file may contain at most 32,768 unique
`solid` records. Coordinates are signed integers in
`[-2147483647, 2147483647]`; each minimum must be strictly below its maximum.

## Semantics

Solids are pre-inflated forbidden volumes for the canonical player root, not
raw render or physics meshes. The deterministic provider sweeps the attempted
fixed-tick root segment against solids in its current exact cell. Contact keeps
the current position and installs zero velocity; a clear segment accepts the
kernel's attempted position and velocity. Explicit cells with no solids are
unobstructed, which preserves the existing fixture paths.

Missing, oversized, malformed, manifest-mismatched, or incomplete content stops
server composition before listen, as does a configured spawn inside a solid. A
runtime request with the wrong manifest, an undeclared cell, an already
out-of-domain root, or a position/velocity mismatch returns collision
unavailable and cannot mutate canonical state. An attempted step out of the
coordinate domain meets an implicit hard boundary and stops safely.

This first provider does not add cell traversal, dynamic bodies, gravity,
slopes, capsule dimensions, world objects, or client collision authority.
Content tooling may later bake engine geometry into these blocked-root volumes
without changing the movement-kernel query boundary.
