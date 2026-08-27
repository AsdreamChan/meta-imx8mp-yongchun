# ADR-0001: Yocto branch strategy — Scarthgap for learning, Wrynose for the target

## Status

Accepted (2026-08-26)

## Context

This project builds a Yocto BSP for the NXP FRDM-IMX8MPLUS over twelve weeks.
Weeks 1–3 use a Raspberry Pi 4B as a learning platform; from week 4 onwards the
work moves to the i.MX8M Plus target. A branch had to be chosen before any of
that could start.

Four constraints turned out to interact, and no single branch satisfies all of
them.

### Constraint 1 — Yocto release support windows

Non-LTS releases are supported for roughly seven months; LTS releases for four
years. At the time of writing:

| Release | Codename | Status |
|---|---|---|
| 6.0 | Wrynose | LTS, April 2026, supported to April 2030 |
| 5.3 | Whinlatter | non-LTS |
| 5.2 | Walnascar | non-LTS, EOL |
| 5.0 | Scarthgap | LTS, April 2024, supported to April 2028 |
| 4.0 | Kirkstone | EOL |

### Constraint 2 — `poky` is being retired as the aggregated entry point

`git.yoctoproject.org/poky` has no branch beyond `walnascar`. There is no
`whinlatter` and no `wrynose`. Meanwhile `openembedded-core` and `meta-yocto`
both carry `wrynose` branches.

From 6.0 the reference environment is assembled from separate repositories
rather than from the `poky` aggregate, alongside the new `bitbake-setup`
tool and configuration fragments.

Verified with:

```
git ls-remote --heads https://git.yoctoproject.org/poky
git ls-remote --heads https://github.com/openembedded/openembedded-core
```

### Constraint 3 — the target board is not supported on Scarthgap

`meta-imx` only gained a machine configuration for this board after Scarthgap:

| `meta-imx` branch | `imx8mp-lpddr4-frdm.conf` |
|---|---|
| `scarthgap-6.6.52-2.2.2` | absent |
| `walnascar-6.12.49-2.2.0` | present |
| `whinlatter-6.18.2-1.0.0` | present |
| `wrynose-6.18.20-2.0.0` | present |

The machine is `imx8mp-lpddr4-frdm`, described as *NXP i.MX 8M Plus FRDM with
LPDDR4*. It inherits `conf/machine/include/imx8mp-evk.inc` and overrides the
device tree basename and U-Boot defconfig.

**A branch switch between the learning phase and the target phase is therefore
unavoidable.** No branch supports both the book-aligned learning material and
the board.

### Constraint 4 — learning material

The book being used for weeks 1–3 targets Scarthgap (ISBN 7121500752).
During the phase where getting stuck is most likely, matching the reference
material has real value.

### What the `poky` retirement does *not* change

NXP's `imx-linux-wrynose` manifest no longer fetches `poky`; it fetches
`bitbake`, `openembedded-core` and `meta-yocto` separately. But the setup
entry point is unchanged — still
`MACHINE=<machine> DISTRO=fsl-imx-<backend> source ./imx-setup-release.sh`.

So the change is in **how sources are obtained**, not in **how a build is
configured**. `bblayers.conf`, `local.conf`, layer priority and recipe syntax
are unaffected. This materially lowers the cost of the switch.

## Decision

**Use Scarthgap for weeks 1–3, then move to Wrynose from week 4.**

- Weeks 1–3, Raspberry Pi 4B: `poky` and `meta-raspberrypi` on `scarthgap`,
  set up manually with `oe-init-build-env`.
- Weeks 4+, FRDM-IMX8MPLUS: NXP's `imx-linux-wrynose` manifest via `repo`,
  set up with `imx-setup-release.sh`, machine `imx8mp-lpddr4-frdm`.

The switch happens at a natural boundary: platform changes, and the phase
changes from learning concepts to building the deliverable.

`bitbake-setup` and configuration fragments are **not** used. The manual
workflow is what vendor BSPs and existing production projects use, and it is
what the week-2 goal (being able to explain `BBPATH`, `BBFILES` and
`LAYERDEPENDS`) requires — the new tooling hides exactly those mechanics.

## Consequences

### Accepted

- Two branches instead of one. Some drift in recipe syntax and variables
  between 5.0 and 6.0 will have to be absorbed at week 4.
- Weeks 1–3 are spent on a release that will reach end of life in 2028,
  earlier than 6.0's 2030.
- `bitbake-setup` remains unlearned for now. It is the documented path in the
  6.0 Quick Start, so this is a deliberate gap, not an oversight.

### Gained

- The learning phase is aligned with available reference material, at the
  point where being blocked is most costly.
- The target phase runs on the current LTS with four years of support and
  first-class board support.
- The migration is experienced first-hand rather than avoided. That maps
  directly onto the week-5 work on carrying vendor patches across BSP
  releases, and onto a question BSP interviews actually ask: *the vendor
  shipped a new BSP — how do you bring your changes forward?*

### Mitigation

Differences encountered during weeks 1–3 that are known to have changed in
6.0 are recorded as they come up, so the week-4 transition is informed rather
than discovered. The three known ones are: source acquisition (`poky` vs
separate repositories), the reduced role of `local.conf` in favour of
fragments, and the existence of `bitbake-setup`.

### Revisit if

- NXP backports the FRDM machine configuration to a Scarthgap release —
  then a single-branch path becomes possible.
- The week-4 transition costs materially more than a day, in which case
  staying on Walnascar for the whole project (the only branch with both
  `poky` and board support, though already EOL) should be reconsidered.

## References

- `meta-imx` machine configuration: `meta-imx-bsp/conf/machine/imx8mp-lpddr4-frdm.conf`
- NXP manifest: `https://github.com/nxp-imx/imx-manifest`, branch `imx-linux-wrynose`
- Yocto releases: `https://docs.yoctoproject.org/ref-manual/release-process.html`
