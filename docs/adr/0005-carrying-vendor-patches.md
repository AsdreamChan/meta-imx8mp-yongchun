# ADR-0005: Local changes are carried as a patch series, not a fork

## Status

Accepted (2026-09-25)

## Context

The layer needs to change the vendor's kernel and bootloader — a device tree
property here, a defconfig option there. NXP will publish a new BSP in six
months. Those changes have to survive it.

Three ways to do this, and only one of them survives more than one upgrade.

| Approach | What happens at the next BSP release |
|---|---|
| Edit the sources under `tmp/work/` | Gone at the next `cleansstate`. Not a candidate, but worth naming because it is what the first instinct suggests |
| Fork the vendor tree and point `SRC_URI` at the fork | Merging an entire kernel. Conflicts appear in files nobody touched, and the diff against upstream stops being legible |
| **Keep the vendor tree untouched and maintain changes as a patch series** | `git rebase`. Conflicts are limited to the files actually modified |

The third is standard practice in the embedded Linux world, and it is what
`SRC_URI += "file://….patch"` exists for. This record exists because doing it
correctly involves several details that are not obvious until something fails.

### `SRCREV` is what makes the vendor tree a fixed point

```
SRCBRANCH="lf-6.18.y"
SRCREV="b096ce610e956cc2596006343df8a2a26ed6e019"
```

The `y` in `lf-6.18.y` is a literal wildcard, not a version. It is one
long-lived branch that follows every 6.18 stable release — by the time this
BSP was being used, its tip had moved seventeen releases ahead, to
`lf-6.18.37-2.1.0`.

**A branch name is not reproducible.** `SRCBRANCH` tells the fetcher where to
look; `SRCREV` decides what it gets. The same reasoning applies one level up,
where `repo manifest -r` pins every repository in the BSP manifest to a hash.

## Decision

**A work tree per component, with two branches: `vendor` pinned at the BSP's
`SRCREV`, and `yongchun` carrying local commits on top.**

```
git clone -b <SRCBRANCH> <vendor url>
git checkout -b vendor <tag matching SRCREV>
git checkout -b yongchun vendor
# … commit changes on yongchun …
git format-patch vendor..yongchun -o /tmp/patches
```

The generated patches go into the layer and are named explicitly in
`SRC_URI`, one line each:

```
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI += "file://0001-arm64-dts-imx8mp-frdm-drive-the-status-LED-....patch"
```

A wildcard (`file://*.patch`) would survive regeneration better, but it gives
up ordering — which matters for a series — and makes the recipe stop being a
list of what is applied. With a handful of patches, the explicit list is worth
the maintenance.

### `Upstream-Status` belongs in the commit message

`do_patch` fails, not warns, without it:

```
ERROR: linux-imx-6.18.20+git-r0 do_patch: QA Issue: Missing Upstream-Status in patch
… [patch-status]
ERROR: Fatal QA errors were found, failing task.
```

The requirement is there because a BSP that accumulates twenty patches needs
to know which can be dropped at the next upgrade (`Accepted` — upstream has
them now) and which are permanent (`Inappropriate` — configuration, never
going upstream). Without the marker, a patch series only grows.

**Adding the line to the `.patch` file by hand does not work.** The next
`format-patch` regenerates the file and the line is gone. It has to go in the
commit message in the work tree, where regeneration carries it.

The cost is that the tag appears in the kernel commit message, which would look
out of place if the patch were ever submitted upstream. For a patch marked
`Inappropriate [configuration]` that is not a real cost. A patch intended for
upstream would need the other approach and a note not to regenerate it
blindly.

### Both components use the same flow

The kernel was done first, then U-Boot without modification to the method:
the same two branches, the same `format-patch`, the same `FILESEXTRAPATHS`
and `SRC_URI`, the same `Upstream-Status`. The U-Boot change took ten minutes
of build time at a 96% shared-state hit rate.

That the flow transferred unchanged is the point of this record. It is not a
kernel technique.

## The upgrade rehearsal

Not simulated. `lf-6.18.37-2.1.0` existed in the vendor repository already —
seventeen stable releases ahead of the BSP.

Done on a separate pair of branches so the working ones stayed put:

```
git checkout -b vendor-next lf-6.18.37-2.1.0
git checkout -b yongchun-next yongchun
git rebase vendor-next yongchun-next
git format-patch vendor-next..yongchun-next -o /tmp/patches-next
```

The rebase applied cleanly. The regenerated patch differs from the original by
one line:

```
< From 6c29a08d13b9fbb28c55bef869fa8055654bdb35 Mon Sep 17 00:00:00 2001
---
> From 33722ff17ea4fcae39612c19255d9de8d326b61f Mon Sep 17 00:00:00 2001
```

Same diff, same context lines, same line numbers, same `index` hashes. Only
the commit hash, which changes because rebasing gives the commit a new parent.

**The limits of that result matter more than the result.** The change is one
line, in a board device tree, in a file that is specific to this board and so
has no reason to move. A driver change, or an upgrade across a major kernel
version rather than within a stable series, would not go this way. Nothing
here says patch series are cheap in general; it says this patch was cheap.

## Consequences

### Accepted

- **Patch filenames come from the commit subject.** Amending a subject
  produces a differently named file, and `SRC_URI` does not follow. Regenerate
  and update the recipe together, and delete the old file — it will not be
  overwritten.
- **A work tree per component, outside the Yocto tree**, not under version
  control here. Only the generated patches are. Recreating a work tree from
  scratch means cloning again and checking out the tag matching `SRCREV`.
- **Shallow clones bite.** `--depth 50` did not reach the BSP's `SRCREV` in
  either repository; `git fetch --deepen` was needed both times. The
  `git cat-file -t <rev>` check takes a second and is worth doing before
  creating branches.
- **`git rev-list --count A..B` before `format-patch`.** A malformed range
  silently means "every commit from the beginning" — in the kernel repository
  that is over a million patch files written to disk.

### Unexpected

**Patching the kernel changes its local version, and therefore the module
path.**

```
before: 6.18.20-2.0.0-gb096ce610e95
after:  6.18.20-2.0.0-gcf47426d183d
```

`kernel-yocto` commits patches into the kernel's own git tree, so `HEAD`
moves and the `-g<hash>` suffix follows. Modules install under
`/lib/modules/<version>/`, which means **adding a one-line device tree patch
relocates every kernel module**.

Nothing broke, because everything was rebuilt together. It matters for the
out-of-tree driver work in week 7, where a module built against one kernel
version will not load under another.

### Gained

- The vendor tree stays legible. `git log vendor..yongchun` is exactly the set
  of local changes, and `git diff` against the vendor tag shows nothing but
  them.
- The upgrade path is `git rebase` rather than a merge, so conflicts are
  bounded by what was actually modified.
- `Upstream-Status` makes the series auditable: at the next BSP release, the
  patches marked `Accepted` can be dropped without reading their diffs.

### Revisit if

- The number of patches grows past what an explicit `SRC_URI` list can carry
  legibly. `devtool` maintains the same structure with tooling around it, and
  would be the next step rather than a different approach.
- A change needs to go upstream. The `Upstream-Status` line in the commit
  message would have to be handled differently for that patch.

## References

- `sources/build-logs/imx8mp/2026-09-25-custom-layer-distro-machine-kernel.md`
- [ADR-0002](0002-scarthgap-to-wrynose-migration.md) — `SRCREV` pinning at the
  manifest level

