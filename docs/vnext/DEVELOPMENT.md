# Development and fresh-session workflow

## Working loop

1. Read [CURRENT.md](CURRENT.md), then this file. Use
   [README.md](README.md) for the product and the active section of
   [PLAN.md](PLAN.md) for acceptance criteria. Read [DECISIONS.md](DECISIONS.md)
   when changing authority, architecture, compatibility, persistence, or scripts.
2. Inspect the owning source, tests, and `git status`. Current code/tests define
   what exists; product decisions define what should replace it. Do not resume
   the checkpoint's old roadmap or repeat its full architecture investigation.
3. Implement one observable, reviewable slice of the active milestone. Resolve
   routine details autonomously. An unfinished milestone carries forward with
   one precise next action, not a new speculative plan.
4. Run the smallest affected check, one at a time. Stop at the first failure,
   inspect/fix the cause, then rerun that check before proceeding. Do not broaden
   testing just because a session is ending.
5. Review the diff, remove superseded code for the migrated slice, and replace
   CURRENT's status/evidence/next action. Update a decision only when its rule
   changed. Commit coherent work when the task authorizes commits; never claim
   an unrun test or a target architecture as implemented behavior.

No implementation diaries, discovery reports, transcripts, numbered session
plans, or copied test output belong in tracked documentation. Git is history.
Use ignored `build/` artifacts for measurements and logs. Never commit game/mod
assets, credentials, tokens, user saves, or machine-local content paths.

## Dependency and migration rules

`components/tes3mp` stays independently buildable: owned protocol/session/
transport/coordination interfaces, with no OpenMW, rendering, OpenXR, operating
system, or public third-party transport types. Preserve existing boundary checks.

The server application may compose a new, separately named OpenMW-dependent
runtime target outside that directory. Prefer app-local placement under
`apps/tes3mp-server` for the first probe. Its internals may use ESM and OpenMW
gameplay types; its public integration boundary exposes owned commands, IDs,
results, snapshots, and persistence operations. Add explicit CMake dependencies
with the target; do not disable independent-core checks globally. A broad probe
link is not proof that the production headless dependency graph is acceptable.

Refactor actual engine behavior instead of reproducing it in another domain.
OpenMW single-player and multiplayer should share extracted gameplay logic.
Separate player/service context and presentation effects at the operation's
owning source. Do not use null UI/listener stubs that silently remove gameplay,
or call a plain-item probe evidence for scripts and constant enchantments.

Preserve one canonical writer. Temporary old/new implementations may coexist
only behind explicit composition for migration/testing; never let both mutate
the same live subsystem. Keep old code until replacement callers and failure
tests work, then remove it in that milestone, including obsolete build entries,
schemas/configuration, hooks, and tests that assert the retired architecture.
Retain useful behavioral tests by pointing them at the replacement. Do not fix
every old gameplay discrepancy before starting M1.

Transport proof sources and JSON registries in this directory are still consumed
by tooling. Keep them and update touched entries, not their entire historical
inventory. Existing whole-baseline provenance debt is recorded in CURRENT;
repair it only for a specifically needed check. Baseline/patch comparisons are
allowed for that purpose, not as routine history reconstruction.

## Verification without runaway gates

Never run the complete test suite, expensive full gates, or upstream baseline
tests by default. Broad release/platform runs require a specifically requested
scope. A source-contract assertion must report a compact mismatch, never dump
an entire source file. Redirect command output to `build/logs`, report exit code
and a short failure tail, and stop the sequence on failure.

For the documentation guard, run the individual methods sequentially:

```powershell
New-Item -ItemType Directory -Force -Path build/logs | Out-Null
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_document_set_stays_small_and_unambiguous *> build/logs/docs-budget.log
# Inspect $LASTEXITCODE; stop if nonzero before the next command.
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_local_links_resolve *> build/logs/docs-links.log
```

For C++, inspect the owning CMake target and test framework first. Build the
individual target in an existing suitable build tree and run one executable or
filtered test. Name the target/filter/log and result in CURRENT. If configuration
or dependencies are missing, establish only those needed for the active slice.
Do not invoke aggregate `_tests_run` targets as a substitute for choosing a test.

The existing Windows wrapper initializes MSVC and logs to `build/logs`:

```text
build_windows.bat -Target server
build_windows.bat -Target client
build_windows.bat -Target desktop-evidence
```

These are builds, not automatic proof of gameplay. Inspect their cost before
running them. `protocol`, `server-logic`, `adapter-tests`, `contracts`, and
`standalone` wrappers include groups of tests; `baseline`, `checks`, `full`,
unfiltered unittest discovery, and default standalone build presets are not
the narrow default. Linux/macOS product presets remain in root CMakePresets.json.
Do not run full upstream suites to validate documentation or a small adapter edit.

Use existing capture scripts only for the path they actually exercise. Old
recipe-backed desktop captures cannot prove a new engine runtime. Add a small
reproducible capture to the owning milestone when necessary, with bounded logs,
loadout identity, observed outcomes, and failure conditions. Real mod data is
local external input; synthetic fixtures are useful but must be labeled.

## Documentation contract

Exactly five active Markdown files live directly under `docs/vnext`:

| File | Sole responsibility |
|---|---|
| README.md | Stable product vision and navigation |
| CURRENT.md | Current implementation delta, blockers, verification, next action; at most 850 words |
| PLAN.md | Ordered milestone outcomes and exit/removal criteria; at most 1,500 words |
| DEVELOPMENT.md | Session, dependency, migration, and verification workflow |
| DECISIONS.md | Compact durable constraints and cooperative semantics |

Their combined ceiling is 5,000 words, enforced by the existing documentation
guard. Existing dependency-proof Markdown is an explicit fixed exception; no new
nested planning files may bypass the limit. Keep local links valid. Replace
superseded paragraphs, avoid duplicated status, and remove completed detail from
CURRENT. Record only the newest relevant verification, distinguishing inherited
claims, source inspection, synthetic tests, real-loadout tests, and live clients.
