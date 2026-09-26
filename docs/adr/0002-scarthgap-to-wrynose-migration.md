# ADR-0002: Migrating from Scarthgap to Wrynose at week 4

## Status

Accepted (2026-09-15). Amended 2026-09-16, 2026-09-20 and 2026-09-23 as the
migration was carried out — see the correction notes on items 2 and 4, and the disk estimate
superseded by [ADR-0003](0003-build-tree-lifecycle.md).

Implements the second half of [ADR-0001](0001-yocto-branch-strategy.md).

## Context

[ADR-0001](0001-yocto-branch-strategy.md) settled the branch strategy:
Scarthgap for weeks 1–3 on a Raspberry Pi 4B, Wrynose from week 4 on the
FRDM-IMX8MPLUS. It recorded *why* the switch is unavoidable — `meta-imx` only
gained `imx8mp-lpddr4-frdm.conf` after Scarthgap, so the target board does not
exist on the release the week 1–3 reference material targets.

It did not record *what* would actually break. Three weeks of work surfaced
five concrete differences, one of which was found by hitting it; a sixth
appeared in week 5 while customising the distro.

This record exists so week 4 starts from a checklist rather than from
discovery.

## Known differences

### 1. `poky` no longer exists as an aggregate — **verified**

`git.yoctoproject.org/poky` has no branch beyond `walnascar`. From 6.0 the
reference environment is assembled from `openembedded-core` and `meta-yocto`
as separate repositories.

```
git ls-remote --heads https://git.yoctoproject.org/poky
git ls-remote --heads https://github.com/openembedded/openembedded-core
```

NXP's `imx-linux-wrynose` manifest reflects this: it fetches `bitbake`,
`openembedded-core` and `meta-yocto` rather than `poky`.

Confirmed on the target tree: the manifest fetches 27 repositories and `poky`
is not among them. `meta-poky` the *layer* is still present, provided by
`meta-yocto` — it is the aggregate repository that is gone, not the layer.

**Impact: none on setup.** The manifest handles source acquisition, and the
entry point is unchanged —
`MACHINE=<machine> DISTRO=fsl-imx-<backend> source ./imx-setup-release.sh`.
`bblayers.conf`, `local.conf`, layer priority and recipe syntax are unaffected.

Evidence: `sources/build-logs/cross/2026-08-27-branch-survey.md`

### 2. `S = "${WORKDIR}"` will break — **verified on both**

The first hand-written recipe failed with:

```
| cc1: fatal error: hello.c: No such file or directory
```

`S` defaults to `${WORKDIR}/${BP}`, which assumes unpacking a tarball produces
a `name-version` directory. A `file://` source does not, so `do_compile` runs
one directory away from where the source landed.

On Scarthgap the fix is `S = "${WORKDIR}"`, because `SRC_URI` unpacks directly
there. Confirmed by the absence of the variable that would change this:

```
$ bitbake -e hello | grep -B8 "^UNPACKDIR="
(no output)
```

Newer releases introduce `UNPACKDIR`, separating the unpack destination from
`WORKDIR`. **Any recipe relying on `S = "${WORKDIR}"` needs revisiting.**

> **Verified on Wrynose, 2026-09-20.** Originally recorded as expected.

```
$ bitbake -e imx-image-core | grep -B8 "^UNPACKDIR="
# $UNPACKDIR
#   set .../openembedded-core/meta/conf/bitbake.conf:417
#     [_defaultval] "${WORKDIR}/sources"
UNPACKDIR=".../imx-image-core/1.0/sources"
```

The default is **`${WORKDIR}/sources`**, not `${WORKDIR}`. So on Wrynose:

```
S = "${WORKDIR}"      # works on Scarthgap, breaks here
S = "${UNPACKDIR}"    # correct
```

Evidence: `sources/build-logs/rpi/2026-09-06-first-handwritten-recipe.md` (Scarthgap),
`sources/build-logs/imx8mp/2026-09-17-imx-first-build-three-failures.md` (Wrynose)

### 3. `LAYERSERIES_COMPAT_<layer>` must be updated

Every layer declares which Yocto releases it is compatible with. A layer
declaring only `scarthgap` produces a parse-time error on Wrynose.

**Impact: one line per layer.** Trivial, but it fails the build immediately,
so it belongs at the top of the checklist rather than the bottom.

### 4. `local.conf` is de-emphasised in favour of configuration fragments — **corrected 2026-09-16**

> **Correction.** This item originally claimed that NXP's
> `imx-setup-release.sh` is purely the old workflow and that fragments
> therefore do not apply. That is wrong. Verified on setup:

The setup script writes `build/conf/toolcfg.conf` and enables a fragment:

```
OE_FRAGMENTS += "core/yocto/root-login-with-empty-password"
```

`bitbake-config-build list-fragments` works and offers others, including
`core/yocto/sstate-mirror-cdn` and `core/yocto/sbom-cve-check`.

**The actual situation is hybrid, weighted toward the old mechanism:**

| Mechanism | State |
|---|---|
| `bblayers.conf` | present, 40 lines |
| `local.conf` | present, but **21 lines** |
| `toolcfg.conf` + `OE_FRAGMENTS` | written automatically by the setup script |
| `bitbake-config-build` | available |

Core configuration — `MACHINE`, `DISTRO`, `DL_DIR`, `PACKAGE_CLASSES`,
`ACCEPT_FSL_EULA` — remains in `local.conf`. Only the optional
root-login-without-password feature is expressed as a fragment.

For comparison, the Scarthgap tree's `local.conf` carried roughly seven
hand-added settings on top of a 200-line sample. Twenty-one lines is a real
reduction.

**Impact on the plan: none.** Customising the machine in week 5 goes through
`local.conf` and a machine configuration file, as planned. But the claim that
fragments are absent here was false, and `core/yocto/sstate-mirror-cdn` is
worth looking at — on the Scarthgap tree the same thing took four hand-written
variables in `local.conf`, and getting one of them wrong silently produced a
0% mirror hit rate.

### 5. `bitbake-setup` exists

Not a break, but worth knowing it is the documented path in the 6.0 Quick
Start. Deliberately unused here. See ADR-0001.

### 6. `POKY_DEFAULT_DISTRO_FEATURES` is empty — **added 2026-09-23**

> Found while customising the distro in week 5, not anticipated when this
> record was written.

On Scarthgap, `meta-poky/conf/distro/poky.conf` defines it:

```
POKY_DEFAULT_DISTRO_FEATURES = "opengl ptest multiarch wayland vulkan"
DISTRO_FEATURES ?= "${DISTRO_FEATURES_DEFAULT} ${POKY_DEFAULT_DISTRO_FEATURES}"
```

On Wrynose the same file sets it to the empty string:

```
$ bitbake -e | grep -B15 "^POKY_DEFAULT_DISTRO_FEATURES="
#   set .../meta-yocto/meta-poky/conf/distro/poky.conf:16
#     ""
POKY_DEFAULT_DISTRO_FEATURES=""
```

Those features are still present in the final `DISTRO_FEATURES` — `opengl`,
`ptest`, `multiarch` and `vulkan` all survive. They are assembled somewhere
else now.

**Impact: a diagnostic, not a build.** Week 3 used this variable to tell "the
distro file was never loaded" apart from "it was loaded but the assignment
lost" — an empty value meant the `require` chain was broken. **That test does
not work here**, because empty is the correct value. Distinguishing the two
cases on Wrynose needs a different variable, or `bitbake -e | grep -B15` on
whatever the distro file itself sets.

## Decision

Migrate at the platform boundary, in this order:

1. `repo init` the NXP `imx-linux-wrynose` manifest, `repo sync`, and set up
   with `imx-setup-release.sh`. Machine is `imx8mp-lpddr4-frdm`.
   **Done 2026-09-16**: 27 repositories, 881 MB of metadata, 4124 recipes
   parsed against Scarthgap's 963.
2. Build the vendor BSP unmodified first. Record the environment, the
   duration, and every error encountered — that baseline is the reference for
   everything after it.
   **Done 2026-09-20** after four attempts; see
   [`00-vendor-bsp-baseline.md`](../00-vendor-bsp-baseline.md).
3. Confirm `UNPACKDIR` exists (`bitbake -e <recipe> | grep -B8 "^UNPACKDIR="`)
   before writing any recipe with a `file://` source.
   **Done**: it exists, default `${WORKDIR}/sources`. See item 2.
4. Port `meta-mylayer`'s contents into `meta-imx8mp-yongchun`, updating
   `LAYERSERIES_COMPAT` and revisiting `S` in the process.

The Scarthgap tree stays on disk rather than being deleted. Two trees cannot
share one directory — a single build tree cannot hold two versions of
OE-Core — but `DL_DIR` is shared between them, since source tarballs overlap
almost completely.

`SSTATE_DIR` stays separate per tree. Hashes from different releases will not
match, so sharing gains nothing and makes "how long does a first build take on
this tree" unmeasurable.

## Consequences

### Accepted

- ~~Disk usage roughly doubles. The VHDX needs sparse mode enabled before
  week 4.~~ **Superseded by [ADR-0003](0003-build-tree-lifecycle.md).** The
  vendor BSP's `tmp/` alone reached 214 GB — an order of magnitude beyond the
  estimate — and Microsoft has since disabled sparse VHD mode over
  data-corruption risk.
- Recipe syntax drift between 5.0 and 6.0 beyond the five items above will be
  discovered rather than anticipated. Five is what three weeks surfaced; it is
  not a complete list.
- Two build environments means two sets of `conf/` to keep straight, and one
  more way to run a command in the wrong tree. That already happened once on
  a single tree — `oe-init-build-env` without an argument creates `build/` in
  the current directory, and the resulting qemux86-64 defaults are easy to
  miss.

### Gained

- The target phase runs on the current LTS with support until 2030 and
  first-class board support.
- The migration is experienced rather than avoided. It maps onto the week-5
  work on carrying vendor patches across BSP releases, and onto a question BSP
  interviews actually ask.

### Revisit if

- The transition costs materially more than a day. Walnascar is the only
  branch carrying both `poky` and board support, though it is already EOL —
  falling back to it for the whole project would be the alternative.

## References

Sources marked `sources/...` are in a private working repository; they are cited for traceability rather than as links.

- [ADR-0001](0001-yocto-branch-strategy.md)
- `sources/build-logs/cross/2026-08-27-branch-survey.md` — branch availability
- `sources/build-logs/rpi/2026-09-06-first-handwritten-recipe.md` — `S` and the
  absence of `UNPACKDIR`
- `sources/vendor-docs/2026-08-27-meta-imx-8mp-machines.md` — which `meta-imx`
  branches carry `imx8mp-lpddr4-frdm.conf`
- `sources/build-logs/imx8mp/2026-09-17-imx-first-build-three-failures.md` — the
  migration as carried out, and `UNPACKDIR` on Wrynose
- [ADR-0003](0003-build-tree-lifecycle.md) — disk usage and build tree lifecycle
- [`00-vendor-bsp-baseline.md`](../00-vendor-bsp-baseline.md) — the resulting baseline
