# Development and fresh-session workflow

## Working loop

1. Read [CURRENT.md](CURRENT.md), this file, [README.md](README.md), active
   [PLAN.md](PLAN.md), and [DECISIONS.md](DECISIONS.md) for boundary/persistence changes.
2. Inspect owning code/tests and `git status`; they establish behavior. Search
   history/retired documents only for specific unanswered questions.
3. Advance bounded connected behavior, preserving the migration base and one
   canonical writer. Do not create another plan.
   For effects, finish a behavior family using shared OpenMW handlers and reusable,
   parameterized checks. Session size follows dependencies, not an effect-ID quota.
4. Run the smallest affected check individually; stop, fix and rerun on failure.
5. Review the diff. Retire superseded code only after replacement/failure checks.
   Replace CURRENT's handoff for changed behavior, limitations or evidence;
   record durable decisions. Commit only when authorized; report only run checks.

Use ignored `build/` for measurements/logs. Never track game/mod assets, credentials,
tokens, user saves or machine-local content paths. Git is the development archive.

## Dependency and migration rules

Keep `components/tes3mp` independently buildable behind owned interfaces; no OpenMW,
rendering, OpenXR, OS or public transport-library types. Preserve boundary checks.

App-local `apps/tes3mp-server` runtime targets may depend on OpenMW. Expose owned
commands/IDs/results/snapshots/persistence; declare CMake dependencies explicitly.
Broad probe links do not establish production headless boundaries.

Refactor owning engine code to separate context/presentation; share logic with
single-player. Null UI/listeners must preserve gameplay. Plain items do not prove
scripts/constants.

Migration paths cannot share live mutation. After replacement/failure checks,
remove obsolete implementations, build entries, schemas, configuration, hooks and
assertions; redirect useful tests. Do not polish retired gameplay before advancing.

Update touched proof/JSON tooling inputs. Repair whole-baseline provenance and
compare baseline patches only when a specific check requires it.

## Narrow verification

No complete suites, expensive gates or baseline tests by default; broad runs need
requested scope. Log to `build/logs`; inspect exit codes and short failure tails.
Source-contract failures report compact mismatches, never source files.

For documentation, run these methods separately, stopping at the first failure:

```powershell
New-Item -ItemType Directory -Force -Path build/logs | Out-Null
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_document_set_stays_small_and_unambiguous *> build/logs/docs-budget.log
# Inspect $LASTEXITCODE before the next command.
python -m unittest scripts.tests.test_vnext_documentation.VnextDocumentationTests.test_active_local_links_resolve *> build/logs/docs-links.log
```

For C++, inspect the owning target/framework, build that target in an existing
tree, then run one executable/filter. Add only required missing dependencies;
never aggregate `_tests_run` targets. Record changed evidence in CURRENT.

`build_windows.bat -Target server`, `client` and `desktop-evidence` initialize MSVC
and log to `build/logs`; inspect build cost first. Builds do not prove gameplay.
Wrappers `protocol`, `server-logic`, `adapter-tests`, `contracts`, `standalone` run
groups; `baseline`, `checks`, `full`, unfiltered unittest discovery and default
standalone presets are not narrow defaults. Linux/macOS presets remain in
root CMakePresets.json.

Captures prove only exercised paths. Record bounded logs, loadout, outcomes and
failures; distinguish synthetic, real-loadout and live evidence. Old captures
cannot prove new runtimes. Treat mod data as external input.

## Documentation contract

Exactly five active Markdown files live directly under `docs/vnext`:

| File | Responsibility |
|---|---|
| README.md | Stable product vision and navigation |
| CURRENT.md | Implementation, limitations, evidence, next action; at most 850 words |
| PLAN.md | Sole ordered roadmap and exit/removal criteria; at most 1,500 words |
| DEVELOPMENT.md | Session, dependency, migration and verification workflow |
| DECISIONS.md | Durable constraints and cooperative design, with proposals labeled |

Combined ceiling: 5,000 words; existing dependency proofs are a fixed exception.
No nested plans, diaries, reports, transcripts or duplicate trackers. Replace stale
prose, preserve links, distinguish inherited/new evidence. Design changes alone
do not change CURRENT's implementation status or next action.
