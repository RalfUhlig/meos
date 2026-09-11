# Branch and sync workflow (personal reference only)

This repo is a fork of [melinsoftware/meos](https://github.com/melinsoftware/meos). This file
describes how the fork is organized and kept in sync with the original project. It's a personal
reference and not part of anything intended to go back upstream.

## Branches

- **`master`** — a pure mirror of `upstream/master`. No own commits, only fast-forward merges
  from `upstream`. Serves as the clean starting point for feature branches meant to be
  contributed back to the original project.
- **`local`** — the version actually in use day to day. Contains `master` plus all personal
  changes (including this file). Developed on an ongoing basis.
- **`localdev`** — branched off `local`; carries further personal changes that are not (yet) in
  `local`.
- **`feature/*`** (e.g. `feature/second-race-entry`) — self-contained features branched off
  `master`, intended for a pull request against `melinsoftware/meos`. Merged into `local`, but
  kept clean of unrelated `local`-only changes.
- **`linux`** — integration branch of the native Linux port (plan and status:
  `plans/linux-port.md`). Currently based on `master`, i.e. upstream plus the port without
  `local`-only changes. Merges `master` after every upstream release.
- **`port/*`** (e.g. `port/stufe-0-build`) — work branches of the Linux port, branched off `linux`
  and merged back into it.

The port's work branches are called `port/*`, not `linux/*`: Git cannot have a branch `linux` and
branches below `linux/` at the same time, because `linux` would have to be both a file and a
directory under `refs/heads`.

### Open: how `linux` and `local` relate

It is not decided yet whether a Linux version for daily use should contain the personal changes
from `local`. Either `linux` stays on `master` and `local` additionally merges `linux` (the port
stays separable from personal changes), or `linux` is based on `local` (personal features are
available on Linux right away, but port and personal changes can no longer be separated cleanly).

## Remotes

- `origin` — this fork (`RalfUhlig/meos`), push target for all own branches.
- `upstream` — the original project (`melinsoftware/meos`), fetch-only, for pulling in new
  changes.

Only `master` is needed from `upstream`. To stop fetching its other branches (`develop`, etc.):

```bash
git remote set-branches upstream master
```

## Syncing with the original project

```bash
# Fetch new changes from upstream
git fetch upstream

# Bring master up to the latest upstream state (fast-forward only)
git checkout master
git merge --ff-only upstream/master
git push origin master

# Update the personal version
git checkout local
git merge master
```

If `git merge --ff-only upstream/master` fails, `master` accidentally picked up its own
commits — that shouldn't happen. In that case, reset `master` to `upstream/master` rather than
dropping the fast-forward requirement.

### Linux port after an upstream release

Upstream publishes squashed release drops, so a single merge can touch many files.

```bash
git checkout linux
git merge master
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
```

- Merge, don't rebase: conflicts are resolved once, and the history shows which drop came in.
- If the CMake configure step fails with "Source lists of CMake and MeOS.vcxproj differ",
  upstream added or removed source files. Assign them to one of the `MEOS_*_SOURCES` lists in
  `code/CMakeLists.txt`.
- New compile errors on Linux usually come from MSVC-only constructs in the new upstream code;
  the portability rules are listed in `CLAUDE.md`.
- Afterwards merge `linux` into the open `port/*` branches.
- Record noteworthy conflicts and their resolution in the sync log below.

## Linux port sync log

| Date | Upstream drop | Conflicts and resolution |
|---|---|---|
| — | — | No upstream drop merged into `linux` yet; the port started from MeOS 5.0 Update 1. |

## Keeping feature branches current for upstream PRs

To bring a feature branch up to date with a newer `master` (without pulling in `local`-only
changes):

```bash
git checkout feature/my-feature
git merge master
```

## New personal changes

Changes that are *not* meant for an upstream PR are committed directly on `local`, or in their
own branches merged into `local` — not on `master`.
