# Inventory content V1

The dedicated server may load a bounded, manifest-bound inventory artifact with
`inventory_content_file`. When the setting is absent, inventory replication is
not advertised. A configured file must load completely before the listener can
start.

The file is UTF-8-compatible ASCII, one whitespace-separated record per line:

```text
TES3MP_INVENTORY_V1
manifest <64-lowercase-hex-content-manifest-id>
prototype <id> <category> <weight> <value> <max-condition> <max-charge> <slot-mask> <stackable-0-or-1> <key-id-or-none>
container <id> <interior space-id | exterior worldspace-id grid-x grid-y> <x> <y> <z> <capacity-weight>
container_item <container-id> <stack-id> <prototype-id> <count> <condition> <charge> <soul-actor-prototype-id-or-none>
ground_item <stack-id> <prototype-id> <count> <condition> <charge> <soul-actor-prototype-id-or-none> <interior space-id | exterior worldspace-id grid-x grid-y> <x> <y> <z>
```

Item categories are the closed numeric range 0–11 declared by `ItemCategory`.
The equipment slot mask uses the 19 canonical `EquipmentSlot` bits. A zero
container capacity means unlimited weight, matching the canonical inventory
model.

Loading fails closed for missing or oversized files, malformed or duplicate
records, manifest mismatch, invalid prototypes/stacks, unknown container
references, capacity violations, or a cell whose complete reliable inventory
view cannot fit the bounded outbound queue. Container and ground positions must
also lie in declared collision cells before production composition succeeds.

The artifact seeds server-owned canonical state only. It does not grant clients
authority to create item prototypes, select stack identities, or mutate counts.

OpenMW clients bind those opaque IDs locally with repeatable
`--tes3mp-content-item-prototype-map <id>=<item-record>` and
`--tes3mp-content-container-map <id>=<ref-num-index>[:<content-file>]`
options. Mappings must be injective and complete for every presented record;
missing or ambiguous mappings fail the multiplayer session closed.
