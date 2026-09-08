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
- **`feature/*`** (e.g. `feature/second-race-entry`) — self-contained features branched off
  `master`, intended for a pull request against `melinsoftware/meos`. Merged into `local`, but
  kept clean of unrelated `local`-only changes.

## Remotes

- `origin` — this fork (`RalfUhlig/meos`), push target for all own branches.
- `upstream` — the original project (`melinsoftware/meos`), fetch-only, for pulling in new
  changes.

The `upstream` remote is restricted to only track `master` (not `develop`, etc.):

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
