# TES3MP vNext agent guide

This repository contains an OpenMW baseline plus an in-progress clean-break
TES3MP implementation. Keep working context small.

## Read order

1. Read `docs/vnext/CURRENT.md` for the implemented surface and unfinished work.
2. Read `docs/vnext/DEVELOPMENT.md` before changing code or running gates.
3. Read `docs/vnext/DECISIONS.md` only when a change touches architecture,
   authority, compatibility, security, persistence, or platform boundaries.
4. Read `docs/vnext/CONTENT_FORMATS.md` only when changing server content input.
5. Inspect the owning source and tests. Code and executable tests outrank prose.

Do not search Git history or reconstruct retired phase documents unless the
current code, tests, and active documents leave a specific question unanswered.

## Working rules

- Keep the server authoritative for canonical gameplay state.
- Keep `components/tes3mp` independent of OpenMW, rendering, OpenXR, operating
  system, and public transport-library types.
- Bound and validate external input before allocation or mutation; preserve
  atomic failure.
- Implement one useful milestone at a time with proportionate tests.
- Update `CURRENT.md` only when implemented behavior, limitations, the active
  milestone, or verified evidence changes.
- Add a compact entry to `DECISIONS.md` only for a consequential decision that
  is not already clear from code and tests.
- Do not create phase diaries, discovery reports, command transcripts, duplicate
  trackers, or speculative implementation plans.
- Keep the five Markdown files directly under `docs/vnext` below 10,000 words
  combined. Git history is the development archive.
