# TES3MP vNext agent guide

Build cooperative multiplayer modded OpenMW using an OpenMW-backed authoritative
server. Reuse engine gameplay; keep networking independent. The previous bounded
gameplay implementation is migration scaffolding. Keep fresh-session context small.

## Read order

1. Read `docs/vnext/CURRENT.md` for actual state and the next concrete action.
2. Read `docs/vnext/DEVELOPMENT.md` before changing code or running gates.
3. Read `docs/vnext/README.md` for the product and the active milestone in
   `docs/vnext/PLAN.md`. Do not expand the entire roadmap into another plan.
4. Read `docs/vnext/DECISIONS.md` when a change touches architecture,
   authority, compatibility, security, persistence, or platform boundaries.
5. Inspect the owning source and tests. Code and executable tests outrank prose.

Do not search Git history or reconstruct retired phase documents unless the
current code, tests, and active documents leave a specific question unanswered.

## Working rules

- Keep the server authoritative for canonical gameplay state.
- Keep `components/tes3mp` independent of OpenMW, rendering, OpenXR, operating
  system, and public transport-library types. A separate server-runtime target
  outside that directory may depend on OpenMW; preserve explicit boundary checks.
- Refactor OpenMW simulation/presentation and player context instead of extending
  a second gameplay implementation. Keep one canonical writer per subsystem.
- Bound and validate external input before allocation or mutation; preserve
  atomic failure.
- Implement one bounded slice of the active milestone at a time. Retire old code
  after its replacement is wired and verified; preserve the working migration base.
- Run one narrowly relevant test at a time with output in `build/logs`; stop on
  first failure. No complete suites, expensive full gates, or baseline tests by
  default. Source-contract failures must never print entire source files.
- Update `CURRENT.md` only when implemented behavior, limitations, the active
  milestone, or verified evidence changes.
- Add a compact entry to `DECISIONS.md` only for a consequential decision that
  is not already clear from code and tests.
- Do not create phase diaries, discovery reports, command transcripts, duplicate
  trackers, or additional implementation plans. PLAN.md is the single roadmap;
  replace CURRENT.md's handoff rather than appending session history.
- Keep the five active Markdown files under 5,000 words combined, CURRENT.md at
  most 850 and PLAN.md at most 1,500. Existing dependency-proof files are tooling
  inputs, not a place to bypass the budget. Git is the development archive.
