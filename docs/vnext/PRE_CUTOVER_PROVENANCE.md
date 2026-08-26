# Pre-cutover repository provenance

Capture date: 2026-08-25

Capture time: 2026-08-25T17:50:41-06:00

Status: provenance capture published; Phase 0 exit gate approved 2026-08-25

## Purpose and scope

This document records the repository state observed immediately before Slice 0.6
work began. It is the provenance input required by Phase 0 and ADR-0001; the
capture did not itself authorize the real baseline cutover. The project owner
subsequently granted that authorization at the Phase 0 exit-gate review.

The recorded `vnext` commit is the clean, published capture base. This provenance
artifact and its initial implementation-plan update were committed and published
as `1dc1d5bd00519efa5b92a917c46f9407e9e28257`, with tree
`994268dd4e8917a8337d1045aea57efa6728c0ef`.

The plan-status follow-up that records this result necessarily advances the
documentation-only branch tip. Because a commit cannot contain its own object
ID, the real cutover evidence must record the then-final ID from `git rev-parse
vnext` after all Phase 0 documentation is published. ADR-0001 requires local
`vnext`, `origin/vnext`, and that recorded final ID to match before preparation.

## Repository and tool identity

| Item | Captured value |
|---|---|
| Repository worktree | `C:/Users/udidi/Documents/Code/Morrowind/TES3MP` |
| Git version | `2.52.0.windows.1` |
| Checked-out branch | `vnext` |
| Capture-base commit | `beb2f86cdf7d810f8eebf0207e266fb62c6e8fda` |
| Capture-base tree | `12589c7888becb82e0139e9919abc9f09888d228` |
| Capture-base parent | `02d4cfdb7d51a00cad4f18cf57c93cc1bd720e81` |
| Legacy archive commit | `49be5b6405d6ab427e06ed350cf76c715a1f3bdd` |
| Legacy archive tree | `e13344f9223cbc2cfc8cd887ba81c8c9b2cf316f` |
| Merge base of capture base and archive | `49be5b6405d6ab427e06ed350cf76c715a1f3bdd` |
| Pinned OpenMW 0.51 commit | `f4bec41444214a7903bebd178389ca22ca13f646` |

The capture-base commit metadata was:

```text
commit=beb2f86cdf7d810f8eebf0207e266fb62c6e8fda
tree=12589c7888becb82e0139e9919abc9f09888d228
parents=02d4cfdb7d51a00cad4f18cf57c93cc1bd720e81
author=twin <twin@twin.com>
author-date=2026-08-25T17:31:12-06:00
committer=twin <twin@twin.com>
commit-date=2026-08-25T17:31:12-06:00
subject=docs: approve vNext cutover and platform policies
```

## Branch and remote refs

The only configured remote at capture time was:

```text
remote.origin.url https://github.com/poisson-fish/TES3MP.git
remote.origin.fetch +refs/heads/*:refs/remotes/origin/*
```

No `openmw-upstream` remote was configured at capture time. The Phase 0 exit
gate was subsequently approved on 2026-08-25, and Phase 1, Slice 1.1 added the
remote under that authorization.

Relevant local and remote-tracking refs were:

| Ref | Object ID | Kind / relationship |
|---|---|---|
| `refs/heads/vnext` | `beb2f86cdf7d810f8eebf0207e266fb62c6e8fda` | local branch, upstream `origin/vnext` |
| `refs/remotes/origin/vnext` | `beb2f86cdf7d810f8eebf0207e266fb62c6e8fda` | remote-tracking branch |
| `refs/remotes/origin/HEAD` | `beb2f86cdf7d810f8eebf0207e266fb62c6e8fda` | symbolic default points to `origin/vnext` |
| `refs/remotes/origin/0.8.1` | `49be5b6405d6ab427e06ed350cf76c715a1f3bdd` | legacy archive branch |

A direct `git ls-remote` query confirmed the shared refs:

```text
49be5b6405d6ab427e06ed350cf76c715a1f3bdd refs/heads/0.8.1
beb2f86cdf7d810f8eebf0207e266fb62c6e8fda refs/heads/vnext
```

The first-parent vNext commits after the archive branch point were:

```text
7b46b95aa0ec0bd2778c81124e41d7de51eb2e27 docs: add vNext recovery and migration plan
86cfa5ab316b6ae1a917da52d2d9617d26c08154 docs: make vNext a clean-break rearchitecture
1628c978e996799f50aa31af9bcf932746bdd0c1 docs: add detailed vNext implementation plan
5c7dff688eb859313afa5c28d80cc1a7ce59d1b8 ensure human in the loop on important decisions
02d4cfdb7d51a00cad4f18cf57c93cc1bd720e81 docs: advance vNext archive preparation
beb2f86cdf7d810f8eebf0207e266fb62c6e8fda docs: approve vNext cutover and platform policies
```

## Tags

The permanent archive tag was present locally and on `origin`:

| Item | Object ID |
|---|---|
| Annotated tag `tes3mp-0.8.1-archive` | `1f3bc4c651573a60b4326b5d4703b6fad4b7fccf` |
| Peeled commit | `49be5b6405d6ab427e06ed350cf76c715a1f3bdd` |

`git cat-file -t tes3mp-0.8.1-archive` returned `tag`. The tag metadata was
`2026-08-25T13:49:05-06:00 | twin | Archive TES3MP 0.8.1 source before vNext
cutover`. The direct remote query returned both the tag object and the same
peeled commit.

The earlier plan and ADR notes describe an existing lightweight
`tes3mp-0.8.1` tag at `68954091c54d0596037c4fb54d2812313b7582a1`. At this
capture, `git for-each-ref` and `git ls-remote --tags origin` found no such local
or remote tag; the only tag advertised by `origin` was
`tes3mp-0.8.1-archive`. No tag was created, moved, deleted, or reused during this
capture. The Phase 0 exit-gate review should acknowledge this observed change;
the required permanent archive tag itself remains intact.

## Submodule state

The legacy tree declared one submodule:

```text
[submodule "extern/breakpad"]
    path = extern/breakpad
    url = https://chromium.googlesource.com/breakpad/breakpad
```

The superproject gitlink pinned `extern/breakpad` to
`e6d1c032baa222d8a8dc87813e9067199ec0266d`. `git submodule status` returned the
same ID with a leading `-`, meaning the submodule was not initialized in this
worktree. This is captured legacy state only; ADR-0001 excludes `.gitmodules`
and the gitlink from the cutover tree because they are not in the pinned OpenMW
tree.

## Active-tree classification

The capture-base commit contained 3,178 tracked files and was still the legacy
TES3MP-era active tree. Each cutover exclusion probe was present:

```text
.gitmodules
apps/browser
apps/master
apps/openmw-mp
components/openmw-mp
files/tes3mp
tes3mp-changelog.md
tes3mp-credits.md
appveyor.yml
.travis.yml
```

This confirms Phase 1 has not started and that the real cutover must remove these
paths by constructing the exact OpenMW tree, not by treating the current tree as
an implementation base.

The five vNext documentation blobs at the capture base were:

```text
b1c963aa722e6662f57a194057a368b94806d3c3 docs/vnext/IMPLEMENTATION_PLAN.md
0d3415c2fc4ffdc50cd717cb5505a23e68b9d3a8 docs/vnext/LEGACY_GAMEPLAY_FEATURE_INVENTORY.md
b7e32c1bb8f8add2541001fb6765576ab9b22f7f docs/vnext/README.md
099df30130b021bee4ddf9cc1cfe430998bc364f docs/vnext/adr/ADR-0001-baseline-cutover-git-mechanics.md
1bf9b77eab803f07019ed3b692c53d8282b34da5 docs/vnext/adr/ADR-0002-platform-toolchain-policy.md
```

This provenance document becomes the sixth preserved `docs/vnext/**` file when
the Slice 0.6 change is committed.

## Cleanliness, remote, and integrity evidence

Before any Slice 0.6 file was created:

- `git status --porcelain=v1 --untracked-files=all` produced no output;
- `git status --short --branch` returned `## vnext...origin/vnext`;
- `git rev-parse vnext` and `git rev-parse origin/vnext` both returned
  `beb2f86cdf7d810f8eebf0207e266fb62c6e8fda`;
- `git worktree list --porcelain` listed only this worktree at that commit;
- `git diff --check` produced no output; and
- `git fsck --no-dangling` completed successfully with no output.

A read-only query of the official OpenMW repository returned:

```text
f4bec41444214a7903bebd178389ca22ca13f646 refs/tags/openmw-0.51.0
```

This verifies the planned baseline object at capture time without adding a
remote or fetching it into the active repository.

## Published-capture preflight

After commit `1dc1d5bd00519efa5b92a917c46f9407e9e28257` was published,
the preflight recorded:

- `HEAD`, local `vnext`, `origin/vnext`, and the direct `origin` branch query all
  resolved to `1dc1d5bd00519efa5b92a917c46f9407e9e28257`;
- `HEAD^{tree}` resolved to `994268dd4e8917a8337d1045aea57efa6728c0ef`;
- `git status --porcelain=v1 --untracked-files=all` produced no output;
- the archive tag remained an annotated tag and both local and remote peel checks
  resolved to `49be5b6405d6ab427e06ed350cf76c715a1f3bdd`;
- `git submodule status` still reported the uninitialized Breakpad gitlink at
  `e6d1c032baa222d8a8dc87813e9067199ec0266d`;
- `git diff --check` produced no output; and
- `git fsck --no-dangling` completed successfully with no output.

Before running any ADR-0001 preparation command, repeat these checks against the
final Phase 0 documentation tip:

```powershell
git status --porcelain=v1 --untracked-files=all
git rev-parse vnext
git rev-parse origin/vnext
git rev-parse 'vnext^{tree}'
git ls-remote --heads origin refs/heads/vnext
git cat-file -t tes3mp-0.8.1-archive
git rev-parse 'tes3mp-0.8.1-archive^{}'
git ls-remote --tags origin refs/tags/tes3mp-0.8.1-archive 'refs/tags/tes3mp-0.8.1-archive^{}'
git submodule status
git diff --check
git fsck --no-dangling
```

The final preflight fails closed unless the worktree is clean, local and remote
`vnext` match the final documentation commit, the archive tag remains annotated
and peels to the approved legacy commit, and the integrity checks pass. The
project owner explicitly approved the Phase 0 exit gate and authorized Phase 1,
including the real cutover, on 2026-08-25. The cutover must still fail closed
unless these checks pass again against the final published documentation commit.
