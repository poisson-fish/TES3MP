# Current state and next action

## Handoff

- **Direction:** OpenMW-backed authoritative cooperative multiplayer; see
  [README.md](README.md) and [DECISIONS.md](DECISIONS.md).
- **Milestone:** M2 in [PLAN.md](PLAN.md). Opposing-direction contention between
  two authorized callers now survives failed delivery, resynchronization and
  fresh version-4 continuation in the installed transfer fixture. Production
  durability/live atomic transfer remain unproven.
- **Next action:** begin the plain equipment seam at
  [InventoryStore::equip/unequipSlot](../../apps/openmw/mwworld/inventorystore.cpp)
  and their [unstack/restack dependencies](../../apps/openmw/mwworld/containerstore.cpp).
  Extract explicit owner/player/service context through shared stock mechanics
  and prove one non-enchanted clothing slot in two isolated actor inventories,
  including split/restack and separated equipment notifications. Establish
  preparation/rejection isolation before any installation claim. Equipment
  persistence and production integration remain deferred; preserve transfer version 4.
- **Session scope:** complete 2–3 closely related bounded slices sequentially;
  one slice at a time limits concurrent scope, not slices per session. After
  review and commit, summarize results/commit and provide a ready-to-paste next
  session prompt with workspace, reading, scope, verification and commit
  requirements. Carry these session-scope and final-response instructions forward.
- **Checkpoint:** `8850e745c298c6de629ef9a5a26bbdddf6aa56e3` preserves the working
  gameplay implementation before the engine-backed pivot.

## Implemented M2 slices

The [focused tests](../../apps/tes3mp-server/native/transfer_rehearsal_tests.cpp)
now construct two independently authorized caller/command pairs before arrival,
with different live source IDs, opposite directions and unequal quantities.
Separate synthetic version-4 seeds set their signed counts to magnitudes 3/5;
partial requests use 1/2 and full requests use 3/5. Other saved values, identities,
selections, service roles and exact counters are retained. Independent stock
contexts establish both candidates' viability and initiator-sensitive `OnPCAdd`.
Both arrival orders produce one persisted/installed winner. The stale loser
rejects before preparation or I/O, retaining independent output storage/value,
owner views and canonical state, including with diagnostic allocation failure armed.

Winner delivery covers all twelve notification failure boundaries and both
owner-view allocation failures. The consumed batch cannot replay. Independent
views preserve their confirmed updates and recover from authoritative snapshots,
including after fixture destruction and fresh installation. Both snapshot
allocation failures preserve prior storage/value. The loser newly issues its
original item, direction and quantity at the recovered revision, with a separately
supplied trusted caller and fresh output. Its result survives another fresh restart.
No recovery replays committed gameplay; restart emits no notifications and script
execution counts remain zero.

Safe file failures and exhaustive representative allocation injection preserve
both pending intents before contention and the committed winner during loser
continuation. Eight uncertain persistence boundaries block both callers through
old/new sinks and block snapshots without allocation or I/O. Discarding the
fixture and validating coherent prior/new bytes permits the other still-valid
intent through a fresh composition; the old sink remains closed. Uncertainty
never produces success or installation, unlike failed delivery after commitment.

The command, fixture, codec and view-consumer implementations required no changes.
Server authority, trusted caller matching, fixed save-envelope initiator and
owner/service roles, version-4 selections and exact saved counters remain intact.
Ownership, storage, lifetime, registry and iterator guards remain mandatory.
Owned success/notification data prepares before persistence; installation,
retirement and publication allocate nothing. No borrowed pointers or iterators
enter saves or published values.

## Fresh verification

Windows MSVC through `scripts/setup_msvc_env.ps1 -PreferLatest`, RelWithDebInfo,
`build/vnext-product`, 2026-09-15. Target `tes3mp_native_loadout_tests` and each
filter below exited **0**. Log prefix: `build/logs/native-opposing-`.

| Filter | Opposing winners / stale checks / allocation failures before contention + reissue | Log suffix |
|---|---|---|
| `inventory-transfer-return-restart` | 192 / 576 / 5,503 + 5,701 | `return-restart.log` |
| `inventory-transfer-restart-command` | 192 / 576 / 5,526 + 5,724 | `forward-restart.log` |
| `inventory-transfer-return-selections` | 128 / 384 / 2,815 + 2,980 | `return-selections.log` |
| `inventory-transfer-selections` | 128 / 384 / 2,811 + 2,976 | `forward-selections.log` |
| `inventory-transfer-command` | shared continuation-helper regression | `command.log` |

Opposing evidence totals 640 winners/reissued losers, 320 winner restarts, 3,200
safe file failures, 2,560 uncertain outcomes/fresh continuations, 584 failed winner
deliveries and 80 owner-view allocation failures. Matrices cover partial/full
removal, stacking, signed counts, scripts, shared/distinct services and selections.
The forward-restart matrix covers unset/transferred/dormant selections; return
and selection matrices additionally cover separate retained live selections.
Its initially overbroad coverage assertion was corrected and the failed filter
rerun successfully. Existing same-item contention and command coverage also pass.
Build: `reviewed-build.log`. Documentation budget and local-link checks ran
individually, exit **0**: `docs-budget.log`, `docs-links.log`.

## Remaining limits and inherited evidence

Commands, authorization, snapshots and delivery remain test-only, serialized and
ephemeral. Production integration, script execution and durable request/notification
deduplication remain deferred. One script/declaration set and non-gold MISC are
supported; views contain IDs/counts/selections. Borrowed inputs require serialized
lifetimes. Other-store metadata validates identity/base/configuration, not complete
saved values. File tests use synthetic single-writer Windows I/O, not crash/power-loss
or production durability. Allocation tracking excludes direct C allocation,
other threads and private external allocators.

Unchanged codec/restore/registry/script/installation matrices remain inherited.
No complete suites, expensive gates or upstream baseline tests ran. Equipment,
gold/other types, Lua/custom state, content deletion, postponed physics and
POSIX/32-bit bounds remain outside this slice. M1 real-content/enchantment evidence
was not rerun; TR remains unverified. Networking/migration base are unchanged.
Broad headless dependencies and baseline provenance debt remain.
