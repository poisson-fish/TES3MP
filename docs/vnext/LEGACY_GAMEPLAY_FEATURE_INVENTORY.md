# TES3MP 0.8.x legacy gameplay feature inventory

Document type: historical reference inventory

Observed baseline: `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`

## Purpose and limits

This inventory groups the user-visible multiplayer behavior present or claimed
by the archived TES3MP 0.8.x line. It exists so vNext gameplay reviews can find
historical cases that may otherwise be overlooked.

This is not a compatibility matrix, a requirements list, or a porting plan.
Nothing here approves legacy protocol layouts, packet semantics, server APIs,
authority choices, state scope, persistence behavior, or engine modifications.
Each vNext domain still requires the ADR/GDR review, implementation slice, and
tests named in the implementation plan. The archived implementation is evidence
about past player-facing behavior only.

## Inventory

| Domain | Historical player-visible behavior to remember during vNext review |
|---|---|
| Connection and discovery | A server browser exposed advertised servers and server information. Clients performed a version/handshake check, could be rejected for data-file mismatch or a missing handshake response, and joined password-protected or open servers. |
| Player lifecycle and communication | Players connected and disconnected, completed character generation, appeared to other players, exchanged chat, received death messages, and could be kicked or address-banned by server logic. |
| Player movement and presence | Other players' position, rotation, movement, momentum, jump/stance behavior, animation flags, explicit animations, sounds, speech, cell changes, and map markers were presented to nearby clients. Interior and exterior travel were supported. |
| Character identity and progression | Character base information, race/class/birthsign and shapeshifting, level progress, attributes, skills, health, magicka, fatigue, bounty, reputation, disposition, factions, jail state, rest, selected spell, marked location, and quick keys could be synchronized or restored by server behavior. |
| Spells and ongoing effects | Spellbooks, power cooldowns, active spell effects, item-based casting, spell casting, effect caster identity, and effect duration/stacking data were represented for players; active effects were also represented for actors. |
| Actors, creatures, and companions | NPC and creature creation/list membership, cell changes, positions, stats, equipment, speech, attacks, casting, animations, deaths and killers, summons, followers, and AI packages were synchronized. A client could be assigned simulation authority for actors in a region/cell. |
| Combat and death | Unarmed, melee, bow, crossbow, thrown-weapon, and spell attacks were presented across clients. Projectile origins/speeds, cast-on-strike use, knockdown state, hits on actors or objects, dynamic stats, death animations, killer attribution, resurrection, and player ally relationships were represented. |
| Inventory, equipment, and trade | Player inventory and equipment, stack counts, item condition/charge, enchantment charge, trapped souls, item use, pickup/drop sounds, vendor trades, merchant inventory/gold restocking, and container changes were synchronized. Container updates used a server-approval flow in later 0.8.x releases. |
| Placed objects and activation | Players could observe object activation, enable/disable state, spawn, placement, movement, rotation, scale, deletion, attachment, sounds, animation, lock state, trap state, restocking, and script-driven changes. Object actions could originate in gameplay, the console, or client-side scripts. |
| Doors, travel, and cells | Door open/closed state and destinations were synchronized. Servers could override destinations, including redirecting travel toward alternate interiors, reset interior or exterior cells, and control collision on selected objects. |
| Dialogue and narrative progression | Dialogue choices and learned topics, journal entries with timestamps, faction rank/reputation/expulsion, read skill books, and world kill counts used by quests could be synchronized or saved by server behavior. |
| Shared world presentation | Servers could control game time/date, weather, explored world-map state, music/video playback, selected game settings, cell resets, collision overrides, and destination overrides. |
| Dynamic content and client scripts | Servers could create or override several record types, including actors, items, spells, enchantments, cells, scripts, containers, doors, activators, statics, sounds, and record-based settings. Selected client-script local variables and rule-selected globals could be synchronized. |
| Server-driven UI and controls | Server scripts could show message boxes, password/input dialogs, and list boxes; alter map visibility and quick keys; trigger console commands, music, or video; and enable or disable selected gameplay inputs/settings. |
| Server customization and state retention | A server-side Lua API exposed lifecycle and gameplay callbacks plus functions for reading received changes and sending state. The project described gameplay synchronization and state saving/loading as customizable through external CoreScripts. |
| Operations and moderation | Server configuration covered hostname, port, player cap, password, plugin/data-file requirements, logging, and gameplay settings. Scripts could inspect connection information, change advertised rules, stop the server, kick players, and manage address bans. |
| VR branch behavior | The separate 0.8.1 VR branch carried attack/cast projectile origins and could override VR-related settings. This establishes a historical PC VR use case, not a reusable VR architecture or a standalone Quest implementation. |

## Known limitations and ambiguous precedents

- The archived README calls out AI problems as a remaining gameplay issue.
- Client-side script-variable synchronization depended on server-managed
  whitelists/rules to avoid excessive traffic.
- Many durable gameplay rules lived in the separately maintained CoreScripts
  project, so this repository alone does not fully define persistence, conflict,
  reset, or ownership behavior.
- The existence of a synchronized value does not establish whether its legacy
  canonical scope was global, per-player, group-scoped, cell-scoped, or merely a
  forwarded presentation update.
- Legacy actor authority, object approval, container approval, and time control
  are scenarios to revisit, not accepted vNext authority designs.
- Packet names and script functions sometimes expose implementation mechanisms
  rather than a stable player-facing contract. They are deliberately not copied
  into this inventory as vNext tasks.

## Repository evidence reviewed

- `README.md` for the archived release's overall support and known limitations.
- `tes3mp-changelog.md` for user-visible additions from 0.0.1 through 0.8.1.
- `components/openmw-mp/NetworkMessages.hpp` for the compiled synchronization
  domains present at the archived branch point.
- `apps/openmw-mp/processors/` and `apps/openmw/mwmp/processors/` for server and
  client handling coverage.
- `apps/openmw-mp/Script/ScriptFunctions.hpp` and
  `apps/openmw-mp/Script/Functions/` for the server customization surface.
- `files/tes3mp/tes3mp-server-default.cfg` for the legacy operational and content
  enforcement surface.

External wiki pages and the separate CoreScripts and OpenMW-VR repositories were
not used. Any behavior defined only there remains outside this repository-backed
inventory and must be researched when its vNext decision slice begins.
