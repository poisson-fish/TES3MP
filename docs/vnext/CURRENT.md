# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M3 in [PLAN.md](PLAN.md). Whole-interior shared inventories,
  content-backed carried starting inventories, and ordinary equipment commands
  across all 19 slots are implemented.
- **Next action:** extract stock starting-loadout auto-equipment selection with
  explicit actor stats/content, then wire it into fresh character bootstrap.
  Preserve stock selection and effects; do not silently equip enchanted/scripted
  items without the required services. Recovery must retain saved equipment.

## Implemented behavior

Authenticated clients can equip/unequip ordinary **clothing, armor and weapons**
in all 19 stock slots, including distinct rings and stacked ammunition/thrown
weapons. Stock slot mappings are shared with OpenMW Class implementations;
clothing/armor eligibility receives explicit actor/content/inventory context.
Beast body-part restrictions and broken armor/weapon checks remain stock.
The bounded actors have no attack simulation. Lights/tools, item activation,
general enchantment effects and executing scripts remain unsupported.

Equipment uses detached engine staging, the same coherent canonical transaction
as transfers, and durability before installation/publication. Every accepted
equipment command advances the inventory revision even without a split. Stale,
foreign, duplicate-slot and invalid-type requests cannot publish partial state;
uncertain writes close the service until recovery. Transfers reject equipped
source items and preserve every unrelated slot. Both private inventory and public
equipment baselines project all slots through the existing wire protocol.

Full slot images use equipment format **7**, with an explicit plain/NPC/scripted
payload mode. Legacy shirt-only formats 1/5/6 retain their exact encodings and
decode into the slot table; canonical format 6 and descriptor bindings are
unchanged. Recovery preflights slot lengths, membership, type, counts and duplicate
identities before installing any owner. Staging/restoration now also preserve
activation flags that stock insertion clears during ordinary item copying.
The bounded scripted/constant Fortify Luck shirt diagnostic remains supported;
it is not general server script execution.

The [native host](../../apps/tes3mp-server/native/inventory_host.hpp) accepts
**native-inventory-5** with `actors "NPC_BASE" "NPC_BASE"`. Each established
player receives the winning NPC's fixed/leveled carried inventory, including an
empty loadout. Owners bind before loot; `loot LEVEL SEED` consumes one stream in
player-role order, then placement order. Scripts, invalid records and oversized
inventories reject startup. Starting items remain **unequipped until commanded**;
chargen rewards and imported saves are not implemented.

V4/v5 discover all enabled winning container placements in one configured
interior, including overrides/deletions, ordered by stable identity. Bootstrap
admits 1–32 shared inventories and retains them for the host lifetime. Unsupported
scripts, locks/traps or oversized cells reject explicitly. Per-command source
ownership, cell and reach checks remain enforced. V3/v4 retain seeded shirts and
their original bindings; descriptor changes require migration or a new campaign.

All stores share one registry/counter and coherent image. Recovery constructs
empty stores and installs saved contents without fixed/leveled loot or refill.
All 12 TES3 inventory record types retain instance fields, dormant identities,
condition/light time, charge and souls. Organic/capacity checks use engine rules.

## Verification

Windows MSVC `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`; individual native-test and server targets build.
Logs are under `build/logs`.

| Filter/check | Evidence | Log |
|---|---|---|
| equipment-slots | all 19 slots, two rings, ammo stacks, restrictions, failed/uncertain durability, allocation-free installation/publication, transfer preservation, malformed recovery, exact restart and synthetic reconnect | equipment-slots.log |
| equipment-canonical | canonical command/image transaction, retries/contention, file recovery and continuation | equipment-canonical.log |
| player-inventory-host | real Morrowind.esm plus generated NPC override: starting shirt/pants equip, dagger equip/unequip/transfer/recipient equip, restart/late join, nine containers, content mismatch rejection | equipment-host.log |
| inventory-equipment-scripted | preserved skip/local/constant-effect semantics and actor isolation | equipment-scripted.log |
| inventory-equipment-codec-guards | 3,350 malformed-byte/binding rejections | equipment-codec-guards.log |
| inventory-equipment-connected | legacy transfer/equipment/restart compatibility | equipment-slots-legacy.log |
| build/documentation | individual targets; budget and local links | equipment-slots-build.log / equipment-server-build.log / docs-budget.log / docs-links.log |

These new checks use synthetic clients/transport, not desktop presentation or
arbitrary published mods. Inherited v3 chest GUI evidence remains under
`build/native-chest-final`; no new GUI/TR proof or complete/baseline suite ran.

## Limits

One interior, two configured players, 32 containers, 64 source nodes per store,
bounded loot expansion and a 1 MiB canonical image remain limits. NPC/creature/
companion simulation, world-item lifecycle, automatic starting equipment,
chargen, scripts/Lua/custom state, locks/traps, merchant/restock/respawn behavior,
cell scheduling and persistent time/RNG, authoritative movement/combat and
general recovery remain unfinished. Ownership and fractional wear persist but
are not fully projected. Player actions cannot create dynamic base records.
