# Current state and next action

## Handoff

M3 in [PLAN.md](PLAN.md); direction and constraints: [README.md](README.md),
[DECISIONS.md](DECISIONS.md). **Next:** exercise v9 doors with two graphical clients:
contact, reversal, latency, reconnect and restart. Desktop wiring is built;
graphical contact/presentation remains unverified.

## Ordinary doors

The [host](../../apps/tes3mp-server/native/inventory_host.hpp) accepts
**native-inventory-9**: insert `door "ORIGIN_PLUGIN" REFERENCE_INDEX` between
`interior` and `cell`. One winning ordinary placement joins content identity.
V9 requires a new campaign and native-door-capable clients; v8 remains unchanged.

[Session format 5](../../apps/tes3mp-server/native/equipment_session.hpp) saves stock
DoorState with inventories/world membership. Recovery installs all owners atomically. The [runtime](../../apps/tes3mp-server/native/door_runtime.cpp)
commits stock activation/reversal and 30 Hz motion before installation/replication.
Preparations reject stale inventory commits. One native mutation
per tick remains; inventory/activation defers that tick's motion. Uncertain writes close service.

Baselines carry angle, direction, motion ID and obstruction. Clients install exact
angles without local motion/avoidance and probe only their own player with detached
geometry. Authenticated, sequenced reports bind session/generation and motion.
Any applicable block stalls progress; reports expire after ten observed ticks
(~333 ms), never persist, and cannot block reversal/reconnect. Collision is trusted;
latency may clip before a stall, without a response barrier or rewind. NPC contacts
and server physics are deferred. Scripted, teleporting, locked/keyed or trapped
doors and configured Lua services remain unsupported.

## Inventory

Two players, 32 shared inventories, 64 world items, 19 equipment slots, corpse loot,
Take All and pickup/drop share one interior/image. Recovery never reloads loot.
User-accepted placement uses stock rays/bounds; model bytes bind saves. V7 retains player-position drops.

## Verification

Windows MSVC RelWithDebInfo, `build/vnext-product`; logs in `build/logs`.

| Check | Evidence | Log |
|---|---|---|
| door-service | atomic failures, report ordering/expiry/reversal/disconnect/restart, two wire clients; authenticated late join, spoof rejection, canonical commits | door-service.log |
| door-session-host | Morrowind base/generated placements, activation/motion, inventory continuation/restart, binding rejection, v8 compatibility | door-live-host.log |
| builds | native tests / openmw / tes3mp_server | door-live-build.log / door-live-client-build.log / door-live-server-build.log |
| docs | budget/links | docs-budget.log / docs-links.log |

Contacts are synthetic; desktop collision has build evidence only. Inherited:
`door-persistence-codec.log`, `door-persistence-session.log`, `door-persistence-v8.log`;
previous inventory/pickup presentation remains user-confirmed.

## Limits

Item-placement rays retain authored door transforms. Dynamic cells, animation,
scripts/Lua, AI/combat, locks/traps, merchants/respawn, chargen/rewards, persistent
time/RNG, general campaign recovery, published mods and Tamriel Rebuilt remain
unfinished/unproven. Movement/camera authority remains inherited.
