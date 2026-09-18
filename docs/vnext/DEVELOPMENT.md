# Development and fresh-session workflow

## Working loop

1. Read [CURRENT.md](CURRENT.md), then this file, [README.md](README.md) and the
   active milestone in [PLAN.md](PLAN.md). Read [DECISIONS.md](DECISIONS.md) for
   authority, architecture, compatibility, persistence or scripting changes.
2. Inspect owning source/tests and `git status`. Code/tests establish implemented
   behavior; product decisions establish the target. Do not reconstruct retired
   roadmaps or search history without a specific unanswered question.
3. Implement one observable slice. Preserve the working migration base and one
   canonical writer. Resolve routine details without another implementation plan.
4. Run the smallest affected check individually, stopping on failure. Fix its
   cause and rerun before proceeding. Do not broaden testing just to end a session.
5. Review the diff and retire superseded callers/code only after replacement
   behavior and failure tests work. Replace CURRENT's handoff when behavior,
   limitations, milestone or evidence changes. Update decisions for durable
   rule changes. Commit only when authorized; never claim unrun verification.

Use ignored `build/` for measurements/logs. Never track game/mod assets, credentials,
tokens, user saves or machine-local content paths. Git is the development archive.

## Dependency and migration rules

`components/tes3mp` remains independently buildable, using owned protocol/session/
transport/coordination interfaces: no OpenMW, rendering, OpenXR, OS or public
third-party transport types. Preserve explicit boundary checks.

A separately named app-local runtime target under `apps/tes3mp-server` may depend
on OpenMW. Expose owned commands, IDs, results, snapshots and persistence operations
at its integration boundary. Declare CMake dependencies explicitly. A broad probe
link does not prove an acceptable production headless dependency graph.

Refactor actual engine behavior, separating player/service context and presentation
at the owning source. Stock single-player and multiplayer share extracted logic.
Null UI/listener stubs must not remove gameplay; plain-item probes do not prove
scripts or constant enchantments.

Old/new paths may coexist for migration/testing but cannot mutate the same live
subsystem. After replacement callers and failure tests work, remove obsolete
implementations, build entries, schemas, configuration, hooks and architectural
assertions. Redirect useful behavioral tests. Do not fix every retired gameplay
discrepancy before advancing the native runtime.

Existing proof sources and JSON registries remain tooling inputs. Update touched
entries; repair whole-baseline provenance only when a specific check requires it.
Baseline/patch comparisons are permitted for that purpose, not routine discovery.

## Narrow verification

No complete suites, expensive full gates or upstream baseline tests by default.
Broad release/platform runs require a requested scope. Source-contract failures
report compact mismatches, never entire source files. Redirect output to
`build/logs`; inspect the exit code and a short failure tail before proceeding.

For documentation, run these methods separately, stopping at the first failure:

```powershell
New-Item -ItemType Directory -Force -Path build/logs | Out-Null
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_document_set_stays_small_and_unambiguous *> build/logs/docs-budget.log
# Inspect $LASTEXITCODE before the next command.
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_local_links_resolve *> build/logs/docs-links.log
```

For C++, inspect the owning CMake target/test framework. Build the individual target
in a suitable existing tree, then run one executable/filter. Establish only missing
dependencies needed for that slice. Never substitute aggregate `_tests_run` targets.
Name the target/filter, log and result in CURRENT when evidence changes.

`build_windows.bat -Target server`, `client` and `desktop-evidence` initialize MSVC
and log to `build/logs`; inspect build cost first. Builds do not prove gameplay.
Wrappers `protocol`, `server-logic`, `adapter-tests`, `contracts`, `standalone` run
groups; `baseline`, `checks`, `full`, unfiltered unittest discovery and default
standalone presets are not narrow defaults. Linux/macOS presets remain in
root CMakePresets.json.

Use captures only for exercised paths. Old recipe-backed desktop captures cannot
prove a new runtime. Evidence needs bounded logs, loadout identity, outcomes and
failure conditions. Label synthetic fixtures, real-loadout tests and live clients
accurately; real mod data is external input.

## Documentation contract

Exactly five active Markdown files live directly under `docs/vnext`:

| File | Responsibility |
|---|---|
| README.md | Stable product vision and navigation |
| CURRENT.md | Implementation, limitations, evidence, next action; at most 850 words |
| PLAN.md | Sole ordered roadmap and exit/removal criteria; at most 1,500 words |
| DEVELOPMENT.md | Session, dependency, migration and verification workflow |
| DECISIONS.md | Durable constraints and cooperative design, with proposals labeled |

Combined ceiling: 5,000 words, enforced by the documentation guard. Existing
dependency-proof Markdown is a fixed exception. No new nested planning files,
diaries, discovery reports, transcripts or duplicate trackers may bypass the limit.
Replace superseded prose, preserve local links, and distinguish inherited evidence
from new verification. Design-only changes do not change CURRENT's implementation
status or active next action.
