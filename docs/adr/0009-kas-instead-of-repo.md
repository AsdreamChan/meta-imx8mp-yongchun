# ADR-0009: The build is set up with kas, not with repo and a hand-edited local.conf

## Status

Accepted (2026-11-12). Implemented in the layer in Sprint 10 (`kas/`,
commit `d444dc6`); see Results.

## Context

Until Sprint 10 the i.MX 8M Plus build came from NXP's own flow:
`repo init` on `imx-manifest`, `repo sync`, then `setup-environment` in a build
directory whose `conf/local.conf` was edited by hand. This layer was linked
into `sources/` as a symlink.

That tree built, but most of what made it build was not in this repository:

- **The other 16 repositories and their commits** came from NXP's manifest, not
  from anything versioned here. Which manifest, at which tag, was in the
  author's notes.
- **The layer list** was whatever `setup-environment` and later hand edits left
  in `bblayers.conf`. The tree also checks out three repositories that are not
  in it (`base`, `meta-browser`, `meta-timesys`).
- **`local.conf`** carried the NXP EULA acceptance (`ACCEPT_FSL_EULA = "1"`),
  the 6 × 6 parallelism that keeps this host clear of the OOM killer, and the
  package format — none of it reviewable or reproducible from the layer.
- **The layer itself was built from the working copy**, so a build could
  include changes that were never committed.

Sprint 10's definition of done asks for the opposite: someone else, starting
from this repository, can reproduce the build environment. A CI job needs the
same thing without a person at the keyboard.

## Decision

**The build environment is described in `kas/` and nowhere else.**

- `kas/yongchun.yml` pins the 16 repositories at the commits NXP's
  `imx-manifest` `rel_imx_6.18.20_2.0.0` resolved to (taken with
  `repo manifest -r` on 2026-11-10), enables the same 28 layers, and sets
  `machine`, `distro` and `target`.
- **Accepting the NXP EULA is a separate file**, `kas/accept-fsl-eula.yml`, that
  must be named on the command line. The default configuration never accepts
  it.
- **Per-host settings go in `kas/local.yml`**, which is ignored by git.
  `kas/local.yml.example` holds the parallelism, with a note that it is sized
  for this host.
- kas is run from an empty directory outside this repository: it clones the
  other repositories and creates `build/` in the current directory.

```
mkdir kas-work && cd kas-work
L=path/to/meta-imx8mp-yongchun
cp $L/kas/local.yml.example $L/kas/local.yml
kas build $L/kas/yongchun.yml:$L/kas/accept-fsl-eula.yml:$L/kas/local.yml
```

### Alternatives considered

- **Keep repo and document the steps in the README.** Nothing would check that
  the document and the tree agree, which is the problem this decision exists to
  fix.
- **A repo manifest of our own** that adds this layer to NXP's. That pins the
  repositories, but `local.conf` and the layer list would still be set up by
  hand or by a script next to it. kas keeps the repositories, the layers and
  the configuration fragments in one file that can be reviewed in a diff.

## Results

Two builds, both against the repo-based tree as reference.

| | 2026-11-10 | 2026-11-11 |
|---|---|---|
| Layer | working copy | fresh clone from GitHub, `sprint-10` at `d444dc6` |
| `local.yml` | `DL_DIR` and `SSTATE_MIRRORS` pointing at the repo-based tree | `local.yml.example` as is |
| Wall clock | 117 m 33 s | **490 m 02 s** |
| Tasks | 9100, 7812 not rerun | 9100, **0 not rerun** |
| Sstate summary | Wanted 5072, Mirrors 4438, Missed 634 | Wanted 5072, **Missed 5072** |
| `rootfs.manifest` | identical (`cmp`) | **identical (`cmp`)** |

Both builds see the same 16 commits and the same 28 layers with the same
priorities as the repo-based tree. The second build is the evidence for the
definition of done: committed content only, nothing shared with the old tree,
one `kas build`.

**What an identical `rootfs.manifest` shows** is that the image has the same
packages at the same versions. It does not show that the binaries are the
same — a changed source without a changed `PV` would not appear in it.

**What the second build does not show.** It ran on the author's machine, as the
author, with kas already installed. It tests that the repository is complete,
not that another host has what the build needs. A parse on a clean CI runner
is the next step; a full build does not fit in a free runner.

## Consequences

### Accepted

- **kas is a host dependency** (5.5 was used). NXP's documentation assumes the
  repo layout, so their instructions no longer map onto this tree one to one.
- **The pins are updated by hand.** A new NXP release means resolving its
  manifest again and editing 16 commits in `kas/yongchun.yml`.
- **The layer order in `bblayers.conf` differs from the repo-based tree.** kas
  sorts layers by path. Names and priorities are identical and the image is
  unchanged, but two layers of equal priority are now searched in a different
  order.
- **kas uses the current directory as its work directory.** Running it inside
  an existing build tree clones 16 repositories into that tree, which happened
  once on 2026-11-11.

### Gained

- The build environment is reviewable: a change to a pin, a layer or a
  `local.conf` line is a diff in this repository.
- A CI job can set up the same environment without interaction.
- The EULA is accepted by a deliberate choice on the command line, not by a
  line left in a file.

### Unexpected

The first kas build reused the repo-based tree's shared state through
`SSTATE_MIRRORS` and still reran 1288 tasks, the Linux kernel among them
(76 minutes). The metadata was identical; the two trees' hash equivalence
databases were not. bitbake warned about exactly this at the start of the
build:

```
WARNING: You are using a local hash equivalence server but have configured an
sstate mirror. This will likely mean no sstate will match from the mirror.
```

Sharing shared state between trees therefore means sharing the hash
equivalence data with it. That changes the assumption in
[ADR-0003](0003-build-tree-lifecycle.md) that `SSTATE_DIR` stays per tree, and
is left to the shared-state story that follows.

### Revisit if

- Updating the pins for a new NXP release takes longer than redoing the repo
  setup would.
- A clean-host check (CI or `kas-container`) finds a host dependency that
  `kas build` on this machine hides.

## References

- `sources/build-logs/imx8mp/2026-11-10-kas-first-build.md` and its addendum —
  the first build, the commit and layer comparison, the hash equivalence
  investigation
- `sources/build-logs/imx8mp/2026-11-11-kas-clean-build.md` — the build from a
  fresh clone
- `sources/build-logs/cross/2026-11-11-bitbake-sstate-summary-current.md` —
  what the Sstate summary fields count
- [ADR-0003](0003-build-tree-lifecycle.md) — build trees and shared state

Drafted by Claude from the build logs above; the decision is the author's.
