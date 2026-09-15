# Branch and sync workflow (personal reference only)

This repo is a fork of [melinsoftware/meos](https://github.com/melinsoftware/meos). This file
describes how the fork is organized and kept in sync with the original project. It's a personal
reference and not part of anything intended to go back upstream.

## Branches

- **`master`** — a pure mirror of `upstream/master`. No own commits, only fast-forward merges
  from `upstream`. Serves as the clean starting point for bugfix branches meant to be
  contributed back to the original project.
- **`local`** — the version actually in use day to day. Contains `master`, the bugfixes and all
  personal features (plus this file). Developed on an ongoing basis.
- **`bugfix/*`** (e.g. `bugfix/si-card-readout`) — fixes of upstream behaviour, branched off
  `master` and kept ready for a pull request against `melinsoftware/meos`: nothing that adds
  capability, and one concern per commit so single fixes can be left out. Merged into `local`.
- **`feature/*`** (e.g. `feature/additional-race`) — personal features, not intended for
  upstream. Branched off `local` (or off the feature they build on) and merged back into
  `local`.
- **`linux`** — integration branch of the native Linux port (plan and status:
  `plans/linux-port.md`). Currently based on `master`, i.e. upstream plus the port without
  `local`-only changes. Merges `master` after every upstream release.
- **`port/*`** (e.g. `port/stufe-0-build`) — work branches of the Linux port, branched off `linux`
  and merged back into it.

The port's work branches are called `port/*`, not `linux/*`: Git cannot have a branch `linux` and
branches below `linux/` at the same time, because `linux` would have to be both a file and a
directory under `refs/heads`.

### Branches that were given up

Removed branches are kept as annotated `archive/*` tags, so their commits stay reachable.

On 2026-09-15 `local` was rebuilt from `master`. Features used to branch off `master` for
possible upstream PRs while also building on each other in `local`, and the same conflicts had
to be resolved again and again. The rebuilt `local` merges `feature/course-family-results`,
`feature/additional-race` (which carries both bugfix branches) and `feature/duplicate-runner`,
and no longer contains the abandoned second race entry.

| Tag | Was |
|---|---|
| `archive/local-2026-09-15` | `local` before the rebuild |
| `archive/duplicate-runner-2026-09-15` | `feature/duplicate-runner` and `localdev` before the rebuild |
| `archive/second-race-entry` | `feature/second-race-entry`, superseded by `feature/additional-race` |
| `archive/cleanup-remove-second-race-entry` | its removal, obsolete with the rebuild |

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
| 2026-09-15 | MeOS 5.0 Update 2 and 3 (`11dbad7`) | The port started from MeOS 5.0 Update 1. No conflicts: the drop touches `localizer.cpp`, `MeosSQL.cpp` and `TabRunner.cpp`, which the port also edits, but in other places; no source files added or removed. Merged into `port/qt-backend` as well; Debug and Release build and pass `ctest` on both branches. `LocalizerInternal::tl()` no longer checks `implBase` for null, so programs without `meos.cpp` must load a language first (the workbench's `app_frame.cpp` does). |

## Keeping bugfix branches current for upstream PRs

To bring a bugfix branch up to date with a newer `master` (without pulling in `local`-only
changes):

```bash
git checkout bugfix/my-fix
git merge master
```

## New personal changes

Features are branched off `local` and merged back into it; small changes go directly on
`local`. Never on `master`, and never on a `bugfix/*` branch. To bring an open feature branch up
to date, merge `local` into it.
