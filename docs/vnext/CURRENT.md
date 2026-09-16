# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Plain equipment now has a bounded,
  actor/runtime/content-bound in-memory byte codec with strict preflight and
  detached round trips. This remains synthetic staging, not live mutation,
  equipment file persistence or production integration.
- **Next action:** add a test-target-only equipment file adapter around
  [equipment_codec](../../apps/tes3mp-server/native/equipment_codec.hpp), reusing
  the [transfer file sink](../../apps/tes3mp-server/native/transfer_file_sink.hpp)
  durability discipline with equipment-specific bounds and trusted bindings.
  Prove bounded read/decode/detached restart, unchanged prior bytes/outputs on
  pre-replacement failure and fail-closed uncertain replacement; keep live
  installation and effect execution deferred. Preserve transfer version 4 and
  fixed owner/service roles.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, concrete scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** 8850e745c298c6de629ef9a5a26bbdddf6aa56e3 preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

[PlainEquipmentValues](../../apps/openmw/mwworld/plainequipment.hpp) exposes its
existing semantic validator to the codec. Equipment format 1 is separate from
transfer version 4. It binds an explicit runtime string, content digest and
trusted actor identity; supplied, already interned TES3 reference IDs prevent
untrusted name interning. These synthetic bindings are not authentication or a
production loadout fingerprint.

Encoding uses stock ObjectState/CellRef/animation serializers plus lossless
fields for stock clamps and omissions. It preserves all supported values,
clothing identities, signed counts, dormant membership, shirt slot, selection,
and exact proposed generation counters, including rollover and representable
exhaustion. Byte growth is checked before allocation. Limits are 80 MiB total,
2 MiB per object, 65 objects, 256 animations per object and 4096-byte text.

Decode first scans views and fixed arrays without allocating input storage.
It checks the complete envelope, field order/types/lengths, nested text and
animation bounds, supplied content, identities, counts, shirt and selection.
Only then does stock ESMReader construct owned values. Shared semantic validation
and canonical re-encoding reject unsupported values, redundant fields and
inconsistent stock/lossless data before nonthrowing output swap. No borrowed
pointers or iterators are serialized or published.

[Two-actor tests](../../apps/tes3mp-server/native/equipment_tests.cpp) prove
encode/decode/detached-restore/re-export/re-encode consistency for split/no-split,
restack/incompatible targets, signed and dormant nodes, 65-node output and exact
counters. Maximum strings/animations, empty inventory, distinct clothing bases,
and source destruction/input mutation exercise bounds and owned lifetimes.
Every truncated prefix and focused malformed/oversized/binding rejection
preserve prior output storage/value, both live actors and captured effect intents.
Individual C++ allocation failures cover encoding, decoding, combined detached
restoration/re-export, semantic rejection and canonical-byte rejection, with
leak-free cleanup and deterministic retries.

Prepared equipment still runs shared stock mechanics in protected actor-local
storage, revalidates witnessed live state and captures gameplay effect intents.
Detached restoration installs no owner, registry, listeners or script services.
Caller content must outlive restored storage. No gameplay effect is suppressed
or executed by this codec.

## Fresh verification

Windows MSVC, scripts/setup_msvc_env.ps1 -PreferLatest, RelWithDebInfo,
build/vnext-product, 2026-09-16. Target tes3mp_native_loadout_tests and each
individual filter below exited **0**. Logs use build/logs/native-equipment-.

| Filter suffix | Evidence / log suffix |
|---|---|
| codec-encode | 2 deterministic owned encodes; codec-encode.log |
| codec | 16 byte/detached round trips; codec.log |
| codec-guards | 3,350 rejection cases; codec-guards.log |
| codec-bounds | 7 bounds/lifetime cases; codec-bounds.log |
| codec-allocations | 2,175 injected failures; codec-allocations.log |

Full filters start with inventory-equipment-. Final build: codec-build.log.
An initial build exited 2 for a test helper's fourCC argument type; allocation
checks exited 1 because MSVC preflight rejection made zero observed C++
allocations. Both test issues were fixed and their checks rerun successfully.
Documentation budget and local links ran individually, exit **0**:
build/logs/docs-budget.log and docs-links.log.

## Limits and inherited evidence

Equipment file persistence, live installation, production integration, scripts,
enchantments, other slots/types and durable request/notification deduplication
remain deferred. No equipment success is durably acknowledged or published.
Runtime scenes/caches are not saved values; postponed physics is rejected.
Access remains serialized and actor-local.

Transfer trusted caller matching, fixed owner/service roles, version-4 selections,
exact saved counters, persistence-before-install and owned publication are unchanged.
Prior transfer and equipment preparation/value checks are inherited, not rerun.
Allocation tracking excludes direct C allocation, other threads and private external
allocators. No full suites, expensive gates or upstream baseline tests ran.
M1/TR evidence, independent networking, migration base, broad headless dependencies
and baseline provenance debt are unchanged.
