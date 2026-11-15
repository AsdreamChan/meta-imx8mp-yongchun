# ADR-0010: Shared state, downloads and the hash equivalence database live outside the build trees

## Status

Accepted (2026-11-14). Supersedes one line of
[ADR-0003](0003-build-tree-lifecycle.md): "`SSTATE_DIR` stays per-tree". The
rest of ADR-0003 — `tmp/` is disposable, copy out before deleting, reclaim the
VHDX with `fstrim` and `compact` — stands.

## Context

ADR-0003 kept `SSTATE_DIR` per build tree for two reasons: hashes from
different Yocto releases do not match, and sharing would make "how long does a
first build take on this tree" unmeasurable.

**The second reason has been settled by measuring it.** Since
[ADR-0009](0009-kas-instead-of-repo.md) there is one release and more than one
tree, and the first build on an empty machine is now on record: 490 minutes
from a fresh clone, nothing shared (2026-11-11).

What changed the question was the first kas build, on 2026-11-10. It pointed
`SSTATE_MIRRORS` at the repo-based tree's shared state, with identical
metadata, and still reran 1288 tasks, among them the 76-minute kernel compile:

```
Sstate summary: Wanted 5072 Local 0 Mirrors 4438 Missed 634 Current 0 (87% match, 0% complete)
```

Following busybox down its dependency chain found the cause. Its
`do_recipe_qa` had the same signature in both trees, but the old tree's hash
equivalence database mapped that task hash to an older, equivalent hash. With
hash equivalence on, every task's hash is computed from its dependencies'
*unified* hashes, and shared state is looked up by unified hash
(`sstate.bbclass:942`, `gethash()` returns `sq_data['unihash']`). One remapped
entry near the bottom changes every hash above it. Counted on 2026-11-13, the
old database held 568 such remappings in 7695 rows — after the 2026-11-10
build, so not necessarily the same set it held then. The new tree, starting
from an empty database, knew none of them.

bitbake warned at the start of that build (`sanity.bbclass:1033–1035`):

```
WARNING: You are using a local hash equivalence server but have configured an
sstate mirror. This will likely mean no sstate will match from the mirror.
```

And oe-core has a second check, for a shared `SSTATE_DIR` whose database stays
inside one tree, which says what to do (`sanity.bbclass:1037–1058`):

```
This will prevent sstate reuse, and it is recommended to set the location for the database to a common path
via BB_HASHSERVE_DB_DIR, for example:

BB_HASHSERVE_DB_DIR = "${SSTATE_DIR}"
```

## Decision

**Shared state, downloads and the hash equivalence database live in one
directory outside every build tree, and every tree points at it.**

```
~/git_repo/yocto/shared/
  downloads/
  sstate-cache/
    hashserv.db
```

Each tree's `kas/local.yml` carries:

```yaml
local_conf_header:
  local-shared: |
    DL_DIR = "/home/isaac/git_repo/yocto/shared/downloads"
    SSTATE_DIR = "/home/isaac/git_repo/yocto/shared/sstate-cache"
    BB_HASHSERVE_DB_DIR = "${SSTATE_DIR}"
```

- `BB_HASHSERVE` stays `auto`. Each tree's bitbake starts its own server on
  its own socket (`TOPDIR/hashserve.sock`); `BB_HASHSERVE_DB_DIR` makes them
  open the same database file (`cooker.py:338–340`).
- **Moving these paths does not change any task hash.** `DL_DIR` and
  `BB_HASHSERVE` are in `BB_HASHEXCLUDE_COMMON`, `SSTATE_DIR` in
  `BB_BASEHASH_IGNORE_VARS` (`bitbake.conf:958–969`). `BB_HASHSERVE_DB_DIR`
  is not listed, but only the cooker and the sanity check read it; the build
  below still hit 5067 of 5072.
- **One build at a time.** The database is SQLite on a local disk. bitbake
  refuses outright to put it on NFS (`cooker.py`); two concurrent builds on
  one file have not been tried.
- Today the shared directory holds what `kas-clean` produced on 2026-11-11.
  Two trees use it: `kas-clean`, a clean reference built from a GitHub clone,
  and `kas-dev`, built from the working copy of this layer.

## Results

| | Tree | Shared state | Database | Wall clock | Sstate summary | Tasks run |
|---|---|---|---|---|---|---|
| 2026-11-11 | `kas-clean`, empty | none | new | 490 m 02 s | Missed 5072 of 5072 | 9100 |
| 2026-11-13, E1 | `kas-clean`, `tmp/` deleted | its own | its own | 13 m 01 s | Local 5068 of 5072 | 14 |
| 2026-11-13, E2 | `kas-dev`, new tree | **shared** | **shared** | 19 m 41 s | Local 5067 of 5072 | 15 |
| 2026-11-10 | first kas tree | old tree, as a mirror | new, private | 117 m 33 s | Mirrors 4438, Missed 634 | 1288 |

E2 includes cloning the 16 repositories (2 minutes) and a first parse in a new
tree. The shared-`SSTATE_DIR` warning did not appear, the database's
write-ahead log in `shared/sstate-cache/` was written during the build, and `kas-dev/build/cache/` has no
database of its own. Every build produced the same `rootfs.manifest`.

For comparison with ADR-0003: its 13 minutes 21 seconds is buildstats'
`Elapsed time` (801 s) on the repo-based tree; the same figure for E1 is 580 s.
Different trees, not the same experiment.

**What E2 does not show.** The database `kas-clean` produced has no
remappings — 0 of 5036 rows map a task hash to a different unified hash. A new
tree with an empty database of its own would therefore compute the same
hashes, and E2 would very likely have hit 99 % without sharing the database at
all. That is an inference; it was not run. E2 shows that a second tree can
use shared state built elsewhere. The
case for sharing the database rests on the 2026-11-10 build and the lookup by
unified hash, not on E2. The direct control — shared `SSTATE_DIR`, private
database — was not run.

## Consequences

### Accepted

- **The trees are no longer independent.** Losing `shared/` costs every tree
  a 490-minute rebuild. It is the one directory worth backing up.
- **The database will start to matter.** Remappings accumulate whenever a
  change alters a task's inputs but not its output — the normal course of
  working on the layer in `kas-dev`. That is when a private database would
  start missing.
- **A new NXP release means a new first-build measurement** and objects from
  two releases in one directory, of which the old ones are never used again.
  Either a separate shared directory per release, or clearing it at the
  switch.
- The repo-based tree (`wrynose-imx`) keeps its own shared state and
  downloads, outside this arrangement, until it is retired.

### Gained

- A new tree, or a tree whose `tmp/` was deleted, is back in about 13 to 20
  minutes instead of eight hours.
- ADR-0003's habit of deleting `tmp/` gets cheaper still: the cost of being
  wrong is a 13-minute rebuild.

### Revisit if

- Two builds need to run at once, or the shared directory moves to another
  machine or a network mount. Then a standing `bitbake-hashserv` server, with
  `BB_HASHSERVE` pointing at it, replaces the shared file.
- A rebuild from the shared directory takes much longer than E1 and E2 did.

## References

- `sources/build-logs/imx8mp/2026-11-10-kas-first-build.md` — the 87 % build,
  the busybox chain, the two databases
- `sources/build-logs/imx8mp/2026-11-11-kas-clean-build.md` — the first build
  from nothing
- `sources/build-logs/cross/2026-11-11-bitbake-sstate-summary-current.md` —
  what the Sstate summary counts; lookup by unified hash
- `sources/build-logs/cross/2026-11-13-bitbake-hashserv-options.md` —
  `bitbake-hashserv`, `BB_HASHSERVE_DB_DIR`, the hash exclusion lists, the
  sanity checks
- `sources/build-logs/imx8mp/2026-11-13-sstate-e1-rebuild-from-own-sstate.md`
- `sources/build-logs/imx8mp/2026-11-13-sstate-e2-shared-sstate-and-hashserv.md`
  and its addendum — E2, the remapping counts
- [ADR-0003](0003-build-tree-lifecycle.md), [ADR-0009](0009-kas-instead-of-repo.md)

Drafted by Claude from the build logs above; the decision is the author's.
