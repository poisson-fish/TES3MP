# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). The test-only inventory command joins
  owned intent, protected preparation, encoded file commit, owned success,
  fresh decode and detached restore/save. Saves now retain exact script-service
  registration metadata alongside the accepted revision and generation counter.
  Production durability and live atomic transfer remain unproven.
- **Next action:** prepare detached restart `LocalScripts` storage from decoded
  version-3 metadata, restored inventory nodes and the detached restart registry
  candidate, with explicit fresh service/other-store bindings. Validate exact
  association, identity, base/configured state, membership and lifetimes before
  staging; preserve shared/distinct services, registration order and cursors.
  Prove registered versus unregistered configured items and dormant entries survive
  fresh fixture reconstruction, with malformed/stale binding rejection, allocation
  cleanup and healthy retries preserving inputs, fixtures and existing output.
  Publish only an owned detached candidate. Keep installation, command resumption
  and execution deferred. This supplies the service state needed for M2's eventual
  end-to-end authoritative inventory operation.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slice

[Serialization](../../apps/tes3mp-server/native/transfer_rehearsal.cpp) captures
owned script metadata only after complete protected transfer validation. Source is
service 0; destination shares it or uses service 1, including empty services.
Registration instance IDs and script IDs remain ordered; cursor equal to size
means end. Configured locals never imply registration. Bounded other-store
identity/base/configured-state bindings cover registrations outside the two
serialized inventories; they do not reconstruct that store's full state.

The [test codec](../../apps/tes3mp-server/native/transfer_save_codec.cpp) explicitly
uses version **3** and rejects earlier versions. It checks framing, membership,
unique bindings/registrations, service association, supplied content, identity
collisions/counter bounds and cursors. Each inventory/other binding collection is
bounded to 1,024 items and registrations to 3,072 total. Byte preflight uses fixed
bounded storage and already supplied content IDs before engine allocation; it does
not intern untrusted names. Decode publishes only the canonical validated save.
Detached restore/save carries metadata as owned values without building services.

[Focused coverage](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
checks shared/distinct services, empty services, registration order, begin/middle/end
cursors, configured unregistered items and registered dormant items. All 48 cases
encode before destroying the original fixture and decode afterward. Malformed
owned/wire metadata and individual allocation failures preserve caller values and
storage, fixtures and independent state; cleanup and healthy retries are checked.

Saved revision/generation values remain verbatim. Protected ownership/storage/
lifetime/iterator guards, persistence-before-install, preallocated command success
and sticky fail-closed uncertainty remain intact. No notifications or script
instructions are dispatched. All restart composition remains test-target-only.

## Fresh verification

Windows MSVC 14.51 (`scripts/setup_msvc_env.ps1 -PreferLatest`), RelWithDebInfo,
`build/vnext-product`, 2026-09-15; final build and filters individually, exit **0**:

- `tes3mp_native_loadout_tests` build.
- `inventory-transfer-script-metadata`: **48** cases, **4,704** malformed
  rejections, **46,234** injected allocation failures; fresh decode after fixture
  destruction in every case.
- `inventory-transfer-codec`: **48** cases, **3,669** malformed rejections,
  **18,637** codec and **33,956** commit allocation failures.
- `inventory-transfer-restore`: **48** cases, **226** malformed rejections,
  **3,040** allocation failures, **48** incomplete pairs.
- `inventory-transfer-restart-registry`: **48** cases, **3,981** malformed/stale
  rejections, **1,432** allocation failures; validation/publication allocate **0**.
- `inventory-transfer-command`: **48** cases, **2,208** safe rejections,
  **96** stale/repeated inputs, **384** fail-closed outcomes, **1,595** allocation
  failures and **2** result allocation failures; installation/retirement/publication
  allocate **0**. Tracked blocks after cleanup are **0** in every filter.

Logs are `build/logs/native-script-metadata-` plus `build`, `focused`, `codec`,
`restore`, `restart-registry` or `command`, then `.log`. An initial build exit **2**
(tag argument type) and focused-test exit **1** (new fixture decoration) were fixed;
those checks passed on retry before continuing. Documentation budget and local-link
checks individually passed, exit **0** (`docs-budget.log`, `docs-links.log` under
that prefix). No complete suites, expensive gates or upstream baseline tests ran.

## Remaining limits and inherited evidence

Script-service reconstruction, installation, command resumption, selections,
production callers, notifications, script execution and durable request
deduplication remain deferred. Restart registry preparation still installs nothing;
future service reconstruction must bind metadata to exact fresh other-store state.
Future resumption must reject revision/ID exhaustion rather than wrap saved values.
The format retains the existing single supplied script/declaration set and non-gold
MISC scope.

File evidence is synthetic single-writer Windows I/O, not crash/power-loss or
production durability. POSIX/32-bit bounds, equipment, gold/other types, Lua/custom
state, content deletion and postponed physics remain outside this slice. Allocation
tracking excludes direct C allocation, other threads and private external allocators.
M1 real-content/enchantment evidence was not rerun; TR remains unverified. Networking
and the migration base are unchanged. Broad headless dependencies and baseline
provenance debt remain.
