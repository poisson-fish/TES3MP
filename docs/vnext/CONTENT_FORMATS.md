# TES3MP vNext server content formats

These formats seed bounded, manifest-scoped server state. They are configuration
inputs, not persistence. Source parsers and their tests are authoritative.

All formats are whitespace-separated ASCII-compatible text with a required
header and exactly one manifest record. Missing, oversized, malformed,
duplicate, unknown-cell, collision-invalid, or manifest-mismatched input fails
server startup; partial catalogs are never accepted.

## Baked content packs V2

[`bake_tes3mp_content.py`](../../scripts/bake_tes3mp_content.py) resolves one or
more OpenMW configuration layers in increasing priority order. It searches the
configured `data` and `data-local` directories using OpenMW's later-directory
priority, then hashes the ordered exact bytes of every `content` file. V2
accepts TES3 ESM/ESP content only.

Every configured plugin must appear exactly once. The baker reads each TES3
header's bounded `MAST` declarations and requires every direct master to appear
earlier in the resolved content order. Missing, duplicate, path-valued, or
misordered masters reject before pack publication. `pack.json` records the
validated direct-master list beside each content file's name, size, and SHA-256;
pack verification checks that recorded graph.
V1 packs, which did not carry this dependency metadata, are rejected without
migration; rebaking creates a V2 identity under a distinct hash domain.

The baker normalizes and hashes the server catalogs and client mappings,
validates winning TES3 records and stable identities, and requires mappings to
cover every presented identity. Cross-catalog mismatches reject publication.

Output is written under `packs/<manifest-id>/` with `server.cfg`, `openmw.cfg`,
the rebound catalogs, and a digest-bearing `pack.json`. Existing packs are
immutable. A successful bake atomically updates `CURRENT`; failure leaves the
previous pointer unchanged. The generated server config expects mutable
`join-password.txt` and `players.txt` in the output root, outside the immutable
pack. The tool never copies or hashes those files.

`--derived-pack-recipe` replaces authored collision, actor, inventory, and
combat catalogs from a bounded `TES3MP_DERIVED_VANILLA_V1` selection of winning
records, identities, transforms, starting equipment, collision solids, and a
combat seed. Missing, deleted, ambiguous, malformed, unsupported, inconsistent,
or out-of-bound input rejects before publication. Recipe collision boxes are
bounded root-occlusion input, not arbitrary geometry extraction.

### Modpack compatibility boundary

The resolved loadout is one compatibility unit. Added, removed, reordered, or
byte-different gameplay plugins reject admission. Catalogs and mappings always
describe winning records after overrides and deletions.

V2 hashes only configured TES3 `content` files and generated TES3MP artifacts.
It does not yet enumerate or hash `fallback-archive` entries, loose resources,
or other resource paths. Therefore it cannot yet claim complete production
compatibility for a modpack whose models, collision, or other external assets
can affect canonical behavior. Future resource identity must bind those inputs;
an override may remain unbound only after it is explicitly classified as
presentation-only.

## Script packages V2 and executable modules V1

Configured by optional `script_package_file`. The ASCII file is limited to
2 MiB, 64 packages, 16,384 variables, 256 variables per package, 4 KiB per
string, and 1 MiB of strings total.

```text
TES3MP_SCRIPT_PACKAGES_V2
manifest <64-hex-digits>
package <package-id> <package-version> <load-order> <api-version> <abi-version> <module-filename> <module-sha256> <entrypoint> <execution-budget>
variable <package-id> <variable-id> boolean <true|false>
variable <package-id> <variable-id> integer <signed-64-bit-value>
variable <package-id> <variable-id> float <finite-double>
variable <package-id> <variable-id> string_hex <lowercase-hex-bytes|->
```

Startup sorts declarations canonically. IDs are nonzero, package IDs are
unique, variables reference a package, API is 5, and ABI is 1. Module leaf
names, SHA-256 values, entrypoints, and budgets (1–4,096) are bounded. Ordered
package versions/API and the typed initial-value catalog bind V2 persistence,
so incompatible state rejects. `-` is an empty string. V1 catalogs reject. The
baker validates, copies, and rebinds modules; their bytes enter pack identity.

Module artifacts are ASCII files limited to 64 KiB, 16 callbacks per module,
64 instructions per callback, and the package's declared execution budget per
callback invocation. The initial deterministic instruction set is deliberately
narrow:

```text
TES3MP_SCRIPT_MODULE_V1
abi 1
api 5
entry <entrypoint>
callback <callback-order> <command_finalized|session_joined|spatial_state_changed|session_lifecycle|dialogue_choice_committed>
increment_integer <declared-integer-variable-id> <nonzero-signed-delta>
global_equals <global-id> <short|long|float> <value>
quest_stage_equals <quest-id> <stage>
journal_entry_absent <journal-entry-id>
dialogue_choice_is <dialogue-choice-id>
faction_rank_at_least <faction-id> <rank>
reputation_at_least <faction-id> <signed-32-bit-value>
set_quest_stage <quest-id> <stage>
add_journal_entry <quest-id> <journal-entry-id>
set_faction_rank <faction-id> <rank>
set_reputation <faction-id> <signed-32-bit-value>
consume <positive-budget-units>
end
```

Predicates read a copied immutable world snapshot; a false predicate emits
nothing. Actions derive expected revisions from that snapshot and queue typed next-tick commands.
Predicates must precede actions in a callback.
Startup validates every variable, global type, quest stage, journal entry, and
quest-entry relationship against the manifest catalogs. Each predicate and
action costs one budget unit; `consume` costs its declared units. Invalid input,
overflow, or exhaustion terminates execution and publishes no callback output.
Modules use canonical package/load order, after durable state is restored.

Broad mod support still requires a bounded extractor for cells, actors,
objects, inventories, collision, combat, and mappings from load-order winners.
The derived recipe covers only a small vanilla subset. Client-side mod scripts
may present confirmed state or submit typed intent, but cannot commit canonical
state; unsupported scripted behavior must be rejected or explicitly inert until
the deterministic server-scripting boundary exists.

## Characters V2

Configured by `character_content_file`. Omission disables authoritative
character creation. Maximum size is 2 MiB. The catalog contains at most 256
races, classes, and birthsigns, 1,024 appearances per race, 64 starting spells
per race/sign, and 64 starting items.

```text
TES3MP_CHARACTERS_V2
manifest <64-lowercase-hex-digits>
spawn <cell> <x> <y> <z> <rot-x> <rot-y> <rot-z>
completion_spawn <cell> <x> <y> <z> <rot-x> <rot-y> <rot-z>
race <id> <8-female-attributes> <8-male-attributes> <27-skill-bonuses> <spell-count> [<spell-id> ...]
appearance <race-id> <head-id> <hair-id> <female-0-or-male-1>
class <id> <combat-0-or-magic-1-or-stealth-2> <2-favored-attributes> <5-minor-skills> <5-major-skills>
birthsign <id> <spell-count> [<spell-id> ...]
starting_item <prototype-id> <count> <equipment-slot-or--1>
```

`<cell>` uses the same `interior <space-id>` or `exterior <worldspace-id>
<grid-x> <grid-y>` form as the other catalogs. Both safe-point transforms must
be in the exact manifest and occupiable in the collision catalog. `spawn` is
the pre-chargen checkpoint; `completion_spawn` is committed only after stock
chargen has actually exited. Coordinates are signed fixed-point values with
1,024 canonical quanta per OpenMW world unit.
Attributes and skills use the fixed OpenMW order (8 attributes, 27 skills);
skill indexes and favored attribute indexes are range-checked and distinct
where the class rules require it. Equipment slots use the canonical 0–18
range; `-1` means carried only.

Record IDs are manifest-scoped unsigned 64-bit identities derived from the
case-folded ASCII OpenMW record ID with FNV-1a. They are opaque on the wire;
the server never accepts an unchecked record string from a client. The shipped
vanilla profile was extracted from the installed `Morrowind.esm` identified by
its manifest hash. Its authoritative intro transform comes from the stock
`CharGen` startup script: Imperial Prison Ship at `(61, -135, 24)`, Z rotation
340 degrees, encoded as `(62464, -138240, 24576)` canonical position quanta and
`4056358002` in `Turn32`. The completion transform comes from the stock
`CharGenDoorExitCaptain` exit destination outside Seyda Neen. V1 character
catalogs are intentionally rejected rather than assigned an inferred safe
point. See
[`character_content.cpp`](../../apps/tes3mp-server/character_content.cpp) and
[`character_profile.cpp`](../../components/tes3mp/server_core/character_profile.cpp).

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

## World V3

The required `world_content_file` is at most 4 MiB. It declares one clock, up to
65,536 typed globals, and manifest-scoped quest, journal, faction, rank, and dialogue-choice catalogs.

```text
TES3MP_WORLD_V3
manifest <64-lowercase-hex-digits>
time <day> <month> <year> <milliseconds-since-midnight> <time-scale-thousandths>
global <nonzero-id> <short-or-long-or-float> <value>
quest <nonzero-quest-id> <initial-stage> <strictly-increasing-stage>...
journal <nonzero-entry-id> <quest-id> <declared-stage>
faction <nonzero-faction-id> <strictly-increasing-rank>...
dialogue_choice <nonzero-choice-id> unrestricted
dialogue_choice <nonzero-choice-id> <required-faction-id> <minimum-rank> <minimum-reputation>
```

Days are 1–30, months 0–11, milliseconds 0–86,399,999, and time scale is in
thousandths from 0 through 1,000,000. Advancement uses integer milliseconds and
a carried remainder over a fixed 30-day, 12-month calendar. `short` is signed
16-bit, `long` signed 32-bit, and `float` finite. Global IDs are unique and
declaration order is canonical: restart requires exactly the same IDs, order,
and types before installation. A quest declares 1–1,024 stages including its
initial stage; the catalog is bounded to 4,096 quests, 32,768 total stages, and
16,384 journal entries; each names a declared quest stage. Restart requires the
exact catalog. Factions have 1–256 ranks; choices are either unrestricted or
require membership at a minimum declared rank and reputation. Canonical
membership and reputation have independent per-player revisions. Clients map
quest IDs with `tes3mp-content-quest-map=<id>=<journal-record>`. See
[`world_content.cpp`](../../apps/tes3mp-server/world_content.cpp).

## Combat V6

Configured optionally by `combat_content_file`; combat also requires actor,
inventory, collision, and historical-contact composition. The catalog contains
one deterministic seed; exactly one settings record, progression profile, and
player template; one combat state per actor; an optional complete actor-attack
set; and zero or more melee weapon or armor profiles keyed by inventory
prototype ID. V6 also carries the bounded direct-magic subset used during
melee contact: on-strike enchantments, equipped constant defenses and elemental
shields, and actor-carried common or blight diseases.

```text
TES3MP_COMBAT_V6
manifest <64-lowercase-hex-digits>
seed <unsigned-64-bit>
settings <12-finite-OpenMW-melee-values> <fatigue-base> <fatigue-multiplier> <fatigue-return-base> <fatigue-return-multiplier> <endurance-fatigue-multiplier> <difficulty-multiplier> <block-left-angle> <block-right-angle> <swing-block-multiplier> <swing-block-base> <block-still-bonus> <block-minimum-chance> <block-maximum-chance> <fatigue-block-base> <fatigue-block-multiplier> <weapon-fatigue-block-multiplier> <base-armor-skill> <unarmored-base-1> <unarmored-base-2> <armor-minimum-damage-multiplier> <unarmed-creature-wears-armor-0-or-1> <redistribute-missing-shield-hit-0-or-1>
magic_settings <elemental-shield-multiplier> <disease-transfer-percent>
player_magic <willpower> <destruction> <fire-resist> <shock-resist> <frost-resist> <poison-resist> <common-disease-resist> <blight-disease-resist> <fire-shield> <shock-shield> <frost-shield>
progression <misc-factor> <minor-factor> <major-factor> <specialization-factor> <block-specialization> <block-use-gain> <short-blade-specialization> <short-blade-use-gain> <long-blade-specialization> <long-blade-use-gain> <blunt-specialization> <blunt-use-gain> <axe-specialization> <axe-use-gain> <spear-specialization> <spear-use-gain> <hand-to-hand-specialization> <hand-to-hand-use-gain> <light-armor-specialization> <light-armor-use-gain> <medium-armor-specialization> <medium-armor-use-gain> <heavy-armor-specialization> <heavy-armor-use-gain> <unarmored-specialization> <unarmored-use-gain>
player <agility> <luck> <strength> <fatigue-term> <fortify-attack> <blind> <short-blade> <long-blade> <blunt> <axe> <spear> <hand-to-hand> <fatigue> <endurance> <block> <intelligence> <magicka> <health-recovery-per-second> <magicka-recovery-per-second> <light-armor> <medium-armor> <heavy-armor> <unarmored> <maximum-weight> <werewolf-0-or-1>
actor <actor-id> <health> <fatigue> <evasion> <chameleon> <invisibility> <normal-resistance> <normal-weakness> <knocked-down-0-or-1> <paralyzed-0-or-1> <unaware-0-or-1> <dead-0-or-1> <creature-0-or-1>
actor_attack <actor-id> <agility> <luck> <strength> <fatigue-term> <combat-skill> <fatigue> <chop-min> <chop-max> <slash-min> <slash-max> <thrust-min> <thrust-max> <reach> <endurance>
actor_magic <actor-id> <willpower> <destruction> <fire-resist> <shock-resist> <frost-resist> <poison-resist> <common-disease-resist> <blight-disease-resist> <fire-shield> <shock-shield> <frost-shield>
weapon <prototype-id> <skill> <chop-min> <chop-max> <slash-min> <slash-max> <thrust-min> <thrust-max> <weight> <reach> <normal-weapon-0-or-1>
armor <prototype-id> <light-0-medium-1-heavy-2> <base-armor>
enchantment <weapon-prototype-id> <charge-cost> <effect-count> [<self-or-other> <fire-or-shock-or-frost-or-poison-or-health-or-fatigue> <minimum> <maximum>]...
equipment_magic <prototype-id> <zero> <zero> <fire-resist> <shock-resist> <frost-resist> <poison-resist> <common-disease-resist> <blight-disease-resist> <fire-shield> <shock-shield> <frost-shield>
disease <actor-id> <spell-record-id> <common-or-blight> <effect-count> [<other> <fire-or-shock-or-frost-or-poison-or-health-or-fatigue> <minimum> <maximum>]...
```

The actor set must exactly match Actors V1. Every weapon must be a conditioned,
right-hand-compatible inventory weapon. If any `actor_attack` declaration is
present, exactly one is required for every actor; all-zero damage declares an
unarmed attack. Attack reach is expressed in stock OpenMW distance units.
Initial health and fatigue are also their canonical maxima. The five fatigue
settings following the core melee values reproduce OpenMW's fatigue term and
active restoration formulas;
restoration is integrated by elapsed authoritative server ticks and clamps at
the canonical maximum. The remaining values reproduce difficulty and blocking.
`combat_difficulty` selects one bounded server-wide value from -100 through 100;
clients never contribute it to an outcome. Every conditioned inventory armor
prototype requires one armor profile. Equipped armor supplies the stock
skill-, condition-, and slot-weighted rating; a server PRNG selects the struck
slot for wear and defensive-skill advancement after an unsuccessful block.
The two armor flags reproduce the stock creature-wear and shield-hit
redistribution settings. Progression specializations use combat 0, magic 1, and stealth
2. Use gains and the four class factors are baked from the selected loadout;
the server combines them with the confirmed character class and current skill.
Health and magicka recovery rates are derived from the stock rest formulas at
the default 30x time scale and apply only to active, living players without a
live same-cell aggressor; integrating those rates with canonical time and full
rest semantics remains future work. Values are finite and range checked;
cross-catalog failure is atomic. See
[`combat_content.cpp`](../../apps/tes3mp-server/combat_content.cpp).

Direct-magic sources contain one through eight instantaneous effects. An
on-strike source requires a conditioned carried-right weapon and sufficient
canonical charge; charge, wear, melee damage, magic damage, retaliation, death,
and revisions commit together. Equipped magic may supply only resistances and
the three elemental shields. Disease entries are unique per actor, transfer by
the server PRNG and configured resistance-aware chance, and apply at most once
per player. General casting, durations, area effects, summons, attribute/skill
effects, dispelling, and scripted effects are outside V6 and fail content
validation instead of being approximated.
