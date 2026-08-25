# ADR-0001: Baseline-cutover Git mechanics

Status: **Accepted**

Date opened: 2026-08-25

Date approved: 2026-08-25

Decision owner: project owner

Needed by: Phase 1

## Decision summary

The project owner approved Option 1 on 2026-08-25. The cutover will use a
two-parent, exact-tree commit:

- first parent: the final committed and published pre-cutover `vnext` commit;
- second parent: OpenMW `openmw-0.51.0` at
  `f4bec41444214a7903bebd178389ca22ca13f646`; and
- commit tree: the exact OpenMW 0.51 tree with only `docs/vnext/**` overlaid from
  the first parent.

The commit will be prepared and verified on a disposable branch/worktree, then
fast-forwarded onto `vnext`. Shared history will never be force-pushed. The
approved production-form disposable rehearsal required by Slice 0.5 passed. The
approval and successful rehearsal do not authorize the real cutover. The real
cutover remains behind Slice 0.6 and the Phase 0 exit-gate review.

## Why this decision is needed now

The current `vnext` branch still contains the TES3MP 0.8.1-era source tree. The
accepted direction requires the active product to become a clean OpenMW 0.51
baseline rather than a port or textual merge of legacy engine and multiplayer
changes. Git history must make both provenance and the deliberate clean break
auditable without rewriting the already shared `vnext` history.

The decision affects every later review: whether upstream history is reachable,
how an auditor enumerates vNext-owned files, how legacy code is proven absent,
and how the team recovers if a cutover is published with a material defect.

## Fixed inputs and invariants

- Legacy archive commit:
  `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`.
- Permanent annotated archive tag: `tes3mp-0.8.1-archive`, tag object
  `1f3bc4c651573a60b4326b5d4703b6fad4b7fccf`, published on `origin` and peeling
  to the legacy archive commit.
- OpenMW baseline: `openmw-0.51.0` at
  `f4bec41444214a7903bebd178389ca22ca13f646` from
  `https://gitlab.com/OpenMW/openmw.git`.
- The existing lightweight `tes3mp-0.8.1` tag remains untouched.
- The final pre-cutover commit must contain all accepted vNext documentation and
  the Slice 0.6 provenance capture, be clean, and be published on `origin/vnext`.
- No force-push, rebase of published commits, or tag movement is permitted.
- The cutover tree preserves only `docs/vnext/**` from TES3MP. Every other
  tracked path is supplied by the pinned OpenMW tree.

## Scenarios the mechanics must cover

1. A reviewer asks whether a post-cutover file came from OpenMW 0.51 or vNext.
   Comparing the cutover with its OpenMW parent shows only `docs/vnext/**`.
2. A legacy-only path has a name that does not produce a merge conflict. Exact
   tree construction removes it because absence in OpenMW is authoritative.
3. The rehearsal or verification fails halfway through. The active `vnext`
   branch remains unchanged because preparation happens in a separate worktree
   and branch.
4. Another commit reaches `origin/vnext` during preparation. The final
   fast-forward/push precondition fails; the cutover is rebuilt from the new
   final pre-cutover commit rather than overwriting shared history.
5. A material issue appears after publication. The team fixes forward where
   possible. An emergency rollback, if explicitly approved, is a new commit with
   the exact recorded pre-cutover tree, never history rewriting.
6. A later OpenMW update needs provenance. Both the original vNext lineage and
   the exact OpenMW baseline are reachable ancestors of the cutover commit.

## Option 1: two-parent exact-tree cutover (recommended)

Create a merge-shaped commit with the pre-cutover `vnext` commit as first parent
and the pinned OpenMW tag as second parent. Build the index from the second
parent's tree and overlay only `docs/vnext/**` from the first parent before
committing.

Advantages:

- preserves the published vNext lineage without a force-push;
- makes the exact upstream baseline an ancestor rather than only a text record;
- makes the difference from the upstream parent small and mechanically
  enumerable;
- avoids conflict-driven or rename-detection-driven tree construction; and
- supports an ordinary fast-forward publication.

Costs and risks:

- the cutover is an intentionally unusual merge commit and must be documented;
- first-parent diffs are necessarily a large whole-tree replacement;
- future merge-base behavior sees both historical lines; and
- the exact command sequence needs rehearsal because index/tree replacement
  during an in-progress merge is less familiar than a textual merge.

## Option 2: single-parent tree-replacement commit

Create one commit whose only parent is the final pre-cutover `vnext` commit but
whose tree is OpenMW 0.51 plus `docs/vnext/**`.

This retains linear first-parent history and avoids merge semantics. However,
OpenMW history is not reachable from the active line, provenance depends on
documentation and object IDs alone, and an auditor cannot use the upstream
parent relationship to distinguish baseline content from vNext-owned content.

## Option 3: new OpenMW-rooted branch with replayed documentation

Create a new branch at OpenMW 0.51 and replay the vNext documentation commits on
top. This provides the cleanest OpenMW-first history, but adopting it under the
existing `vnext` name would require replacing published branch history. Using a
new permanent branch name would abandon the accepted active line and require a
separate migration policy. Either consequence conflicts with the current
no-force-push and continuity requirements.

## Rejected mechanism: ordinary textual merge

An ordinary recursive/ort merge followed by conflict resolution is not a
cutover mechanism. Paths that do not conflict can survive accidentally, rename
detection can influence results, and reviewers must audit a massive merge
instead of verifying one exact baseline tree. The `ours` merge strategy may be
used only to establish the two parent records before replacing the index with
the exact upstream tree; its generated tree is never accepted as the cutover
tree.

## Approved production-form procedure

The real cutover substitutes the final recorded pre-cutover commit and a newly
created disposable worktree path. It must use the rehearsed sequence below; a
changed baseline, preserved path set, parent structure, or tree-construction
mechanism reopens this ADR.

### Preconditions

1. Slices 0.1 through 0.6 are implemented and the Phase 0 exit gate is approved.
2. `vnext`, `origin/vnext`, and the intended pre-cutover commit resolve to the
   same object ID; the main worktree is clean.
3. `openmw-upstream` has the exact official URL, and the fetched tag resolves to
   `f4bec41444214a7903bebd178389ca22ca13f646`.
4. The permanent archive tag still exists locally and remotely, remains an
   annotated tag, and peels to `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`.
5. The preparation branch name and a newly created disposable worktree path are
   explicit and do not collide with existing branches or worktrees.

### Prepare without changing `vnext`

1. Record `cutover_pre`, `cutover_upstream`, and their tree IDs in the rehearsal
   or production evidence.
2. Create a new preparation branch at `cutover_pre` in a disposable worktree.
3. Run a no-commit `ours` merge of `cutover_upstream` only to establish the
   desired two-parent merge state.
4. Replace the index and worktree with `cutover_upstream` using `git read-tree
   --reset -u`.
5. Restore `docs/vnext/**` into the index and worktree from `cutover_pre`.
6. Inspect the staged tree and create one cutover commit whose message records
   the baseline, preserved path set, and verification contract.

The production-form rehearsal must validate the porcelain merge/read-tree/
restore sequence above in a disposable clone or worktree. The earlier plumbing
spike demonstrates only the proposed graph and tree shape; it is not completion
evidence for this procedure.

The approved command sequence for tree construction is:

```sh
cutover_pre=$(git rev-parse vnext)
cutover_upstream=$(git rev-parse openmw-0.51.0)
test "$cutover_upstream" = f4bec41444214a7903bebd178389ca22ca13f646

cutover_root=$(mktemp -d /tmp/tes3mp-vnext-cutover.XXXXXX)
cutover_worktree="$cutover_root/worktree"
git worktree add -b prepare/openmw-0.51-cutover \
    "$cutover_worktree" "$cutover_pre"

git -C "$cutover_worktree" merge --no-commit -s ours "$cutover_upstream"
git -C "$cutover_worktree" read-tree --reset -u "$cutover_upstream"
git -C "$cutover_worktree" restore --source="$cutover_pre" \
    --staged --worktree -- docs/vnext
git -C "$cutover_worktree" commit \
    -m 'Cut over to OpenMW 0.51 baseline' \
    -m 'Baseline: f4bec41444214a7903bebd178389ca22ca13f646' \
    -m 'Preserved overlay: docs/vnext/**'
cutover_commit=$(git -C "$cutover_worktree" rev-parse HEAD)
```

Variable values and all verification output must be captured in the real
cutover implementation note before publication. The preparation branch name
must be confirmed unused before the command sequence begins.

### Verify before publication

The rehearsal and real cutover both fail closed unless all checks pass:

- the commit has exactly two parents in the approved order;
- both parents are ancestors of the cutover commit;
- `git diff` from the OpenMW parent lists only `docs/vnext/**`;
- `git diff` from the pre-cutover parent under `docs/vnext/**` is empty;
- the cutover tree lacks `.gitmodules`, legacy multiplayer/server/browser paths,
  RakNet/CrabNet integration, CoreScripts material, and TES3MP-only root files;
- all files outside `docs/vnext/**` have the exact modes and blob/tree IDs from
  the OpenMW parent;
- `git fsck`, the Phase 1 baseline checker, documentation link checks, and
  `git diff --check` pass as applicable; and
- the preparation worktree is clean after the commit.

Build and upstream-test proof belongs to Phase 1 after the real cutover. Slice
0.5 proves Git mechanics and resulting tree identity, not platform compilation.

### Publish without rewriting history

1. Recheck that local and remote `vnext` still equal `cutover_pre`.
2. Fast-forward the local `vnext` branch to the verified preparation commit.
3. Push normally to `origin/vnext`; a non-fast-forward rejection stops the
   operation and requires rebuilding/revalidating against the new branch tip.
4. Fetch and verify that local `vnext`, `origin/vnext`, and the approved cutover
   commit resolve identically.

No `--force`, `--force-with-lease`, history rewrite, or moved tag is permitted.

## Recovery and rollback

- Before the preparation commit, abort the in-progress merge and discard the
  disposable worktree/branch; `vnext` is unchanged.
- After the preparation commit but before fast-forwarding `vnext`, discard or
  retain the preparation branch for diagnosis; `vnext` is unchanged.
- After a local fast-forward but before publication, do not rewrite reflexively.
  The verified commit may remain local while the cause is investigated.
- After publication, prefer fixes on top of the OpenMW baseline. If the owner
  explicitly approves emergency rollback, create and publish a new commit whose
  tree exactly equals the recorded `cutover_pre` tree and whose parent is the
  published cutover line. Verify the resulting tree and ancestry before pushing.

An emergency rollback restores the legacy active tree and therefore pauses
Phase 1; it does not undo, delete, or conceal the cutover commit.

## Disposable graph/tree spike evidence

On 2026-08-25, before owner approval, a disposable local clone was used to test
the graph and tree construction without changing any shared ref:

- pre-cutover sample: `02d4cfdb7d51a00cad4f18cf57c93cc1bd720e81`;
- OpenMW parent: `f4bec41444214a7903bebd178389ca22ca13f646`;
- common ancestor: `7be09078b4342dffd4bba47f3ef9cad413eafba3`;
- synthetic cutover tree: `466c1fd64121433ae62eb3d46feb310e59b16b81`;
- synthetic two-parent commit:
  `9c4dc996ad3c66e08b6e3e61b257dc8501d1f087`; and
- synthetic rollback commit:
  `68c5e52e746090ec68bf556e9458276816e1f450`.

The cutover differed from OpenMW only by the three `docs/vnext` files committed
at that sample point, preserved those files byte-for-byte, retained both parents
as ancestors, produced no match for the checked legacy multiplayer paths, and
allowed a new child commit with the exact pre-cutover tree. These object IDs are
disposable evidence and must not be used as the real cutover IDs.

## Production-form rehearsal evidence

After owner approval on 2026-08-25, the approved porcelain sequence was run in
disposable clone
`/tmp/tes3mp-vnext-cutover-production-rehearsal.UYIOWR/repo`. The rehearsal
created a disposable committed input containing every current vNext document,
then performed the merge/read-tree/restore procedure in a separate worktree.
It did not update any ref or file in the active repository.

Recorded objects:

- disposable pre-cutover commit:
  `45fa537004aa19bef4b35b9c556351f8327bf75a`;
- OpenMW parent: `f4bec41444214a7903bebd178389ca22ca13f646`;
- cutover commit: `e042db240fe2f109c47bedf0640e4724b7f6ea63`;
- cutover tree: `85d3390ca15a169e04e88d746b1a1d67de4c7a1b`;
- cutover parent order: disposable pre-cutover commit first, OpenMW commit
  second; and
- history-preserving rollback commit:
  `addf2c63ff64af3a12f09f71863ff1973f8601ac`.

Verification passed for:

- exact two-parent order and ancestry;
- no difference from OpenMW outside `docs/vnext/**`;
- no difference from the disposable pre-cutover commit inside `docs/vnext/**`;
- the only five upstream differences being `README.md`,
  `IMPLEMENTATION_PLAN.md`, `LEGACY_GAMEPLAY_FEATURE_INVENTORY.md`, ADR-0001,
  and ADR-0002 under `docs/vnext/`;
- absence of `.gitmodules`, legacy browser/master/server/multiplayer paths,
  TES3MP configuration and root documentation, and path components named
  RakNet, CrabNet, or CoreScripts;
- clean cutover and abort worktrees, `git diff --check`, and `git fsck
  --no-dangling`;
- `git merge --abort` restoring the exact pre-cutover commit and tree after the
  index/worktree replacement; and
- a new child rollback commit restoring the exact pre-cutover tree while keeping
  the cutover in history.

Git warned that exhaustive rename detection was skipped while creating the
merge commit. This does not affect the result: the `ours` merge establishes only
the parent list, after which `read-tree` replaces the complete index with the
exact upstream tree and verification compares tree/blob identity rather than
rename heuristics.

The successful rehearsal used the following exact verification commands after
setting the recorded variables:

```sh
test "$(git -C "$cutover_worktree" show -s --format=%P HEAD)" \
    = "$cutover_pre $cutover_upstream"
git -C "$rehearsal_repo" merge-base --is-ancestor \
    "$cutover_pre" "$cutover_commit"
git -C "$rehearsal_repo" merge-base --is-ancestor \
    "$cutover_upstream" "$cutover_commit"
git -C "$rehearsal_repo" diff --quiet \
    "$cutover_upstream" "$cutover_commit" -- \
    . ':(exclude,top)docs/vnext/**'
git -C "$rehearsal_repo" diff --quiet \
    "$cutover_pre" "$cutover_commit" -- docs/vnext
git -C "$cutover_worktree" diff --check \
    "$cutover_upstream" "$cutover_commit"
test -z "$(git -C "$cutover_worktree" status --porcelain=v1)"
git -C "$rehearsal_repo" fsck --no-dangling
```

The legacy exclusion check used `git cat-file -e` and required failure for each
of these paths: `.gitmodules`, `apps/browser`, `apps/master`, `apps/openmw-mp`,
`components/openmw-mp`, `files/tes3mp`, `tes3mp-changelog.md`,
`tes3mp-credits.md`, `appveyor.yml`, and `.travis.yml`. It also required this
command to return no match:

```sh
git -C "$rehearsal_repo" ls-tree -r --name-only "$cutover_commit" \
    | rg '(^|/)(RakNet|CrabNet|CoreScripts)(/|$)'
```

Abort recovery ran `git merge --abort` after the merge/read-tree/restore steps,
then compared both `HEAD` and `HEAD^{tree}` with `cutover_pre` and required an
empty porcelain status. Published-state rollback was modeled by running
`git read-tree --reset -u "$cutover_pre"` on top of the cutover, committing the
result, and requiring its sole parent to equal `cutover_commit` and its tree to
equal `cutover_pre^{tree}`.

## Consequences of the approved decision

- Every active-tree difference from OpenMW at the cutover is intentional and
  limited to the recorded vNext documentation overlay.
- The repository retains both large histories. Clone and graph traversal costs
  are accepted in exchange for direct provenance.
- Reviewers must understand that the merge commit records provenance, not a
  semantic merge of the two codebases.
- The immediate first-parent diff is large by design; baseline auditing uses the
  OpenMW second parent and the Phase 1 provenance checker.
- No legacy build configuration or repository metadata survives merely because
  it was useful before the cutover.

## Failure modes and mitigations

- **Dirty or unpublished input:** preconditions fail; provenance is captured and
  committed before rehearsal or cutover.
- **Wrong upstream object:** verify the remote URL and exact commit before tree
  construction and again in the resulting parent list.
- **Accidental legacy overlay:** the allowed overlay is exactly one path prefix,
  and tree/blob identity plus explicit legacy-path checks fail closed.
- **Concurrent branch advance:** compare local and remote IDs immediately before
  fast-forward/push; rebuild rather than overwrite.
- **Interrupted worktree mutation:** the active branch remains unchanged while
  preparation occurs separately; abort or recreate the disposable worktree.
- **Misleading merge tools later:** commit message, ADR, and provenance manifest
  state that second-parent comparison is the baseline audit view.
- **Published defect:** fix forward or use the owner-approved new-commit rollback;
  never erase the event from shared history.

## Review and replacement triggers

Reopen this ADR if the OpenMW baseline hash changes, the preserved path set must
expand, the shared branch advances after final rehearsal, any verification check
finds a non-document difference from upstream, or the project proposes a branch
rename/history rewrite. A changed input requires a new rehearsal and owner
review before the real cutover.

## Owner approval

Approved by the project owner in the 2026-08-25 working session: Option 1 and
the proposed mechanics for the disposable production-form rehearsal.

The approval did not authorize the real cutover. The real cutover requires
Slice 0.6, a clean and published final pre-cutover commit, repeated verification
with the final recorded inputs, and separately recorded Phase 0 exit-gate
approval.
