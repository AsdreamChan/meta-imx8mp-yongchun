# ADR-0002: Migrating from Scarthgap to Wrynose at week 4

## Status

Accepted (2026-09-15)

Implements the second half of [ADR-0001](0001-yocto-branch-strategy.md).

## Context

[ADR-0001](0001-yocto-branch-strategy.md) settled the branch strategy:
Scarthgap for weeks 1–3 on a Raspberry Pi 4B, Wrynose from week 4 on the
FRDM-IMX8MPLUS. It recorded *why* the switch is unavoidable — `meta-imx` only
gained `imx8mp-lpddr4-frdm.conf` after Scarthgap, so the target board does not
exist on the release the week 1–3 reference material targets.

It did not record *what* would actually break. Three weeks of work have now
surfaced five concrete differences, one of which was found by hitting it.

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

**Impact: none on setup.** The manifest handles source acquisition, and the
entry point is unchanged —
`MACHINE=<machine> DISTRO=fsl-imx-<backend> source ./imx-setup-release.sh`.
`bblayers.conf`, `local.conf`, layer priority and recipe syntax are unaffected.

Evidence: `sources/build-logs/2026-08-27-branch-survey.md`

### 2. `S = "${WORKDIR}"` will break — **verified on Scarthgap, expected on Wrynose**

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

The presence of `UNPACKDIR` on Wrynose has not been verified directly — check
it first thing.

Evidence: `sources/build-logs/2026-09-06-first-handwritten-recipe.md`

### 3. `LAYERSERIES_COMPAT_<layer>` must be updated

Every layer declares which Yocto releases it is compatible with. A layer
declaring only `scarthgap` produces a parse-time error on Wrynose.

**Impact: one line per layer.** Trivial, but it fails the build immediately,
so it belongs at the top of the checklist rather than the bottom.

### 4. `local.conf` is de-emphasised in favour of configuration fragments

From 6.0 the documented workflow uses `bitbake-setup` and
`bitbake-config-build enable-fragment ...`, leaving `local.conf` largely
empty.

**Impact: none, deliberately.** NXP's `imx-setup-release.sh` is the old
workflow, and so is every existing production project. ADR-0001 records the
decision not to use `bitbake-setup`: the fragment tooling hides exactly the
layer mechanics this project is meant to demonstrate.

The two workflows produce different build directory layouts, so **pick one and
stay with it**.

### 5. `bitbake-setup` exists

Not a break, but worth knowing it is the documented path in the 6.0 Quick
Start. Deliberately unused here. See ADR-0001.

## Decision

Migrate at the platform boundary, in this order:

1. `repo init` the NXP `imx-linux-wrynose` manifest, `repo sync`, and set up
   with `imx-setup-release.sh`. Machine is `imx8mp-lpddr4-frdm`.
2. Build the vendor BSP unmodified first. Record the environment, the
   duration, and every error encountered — that baseline is the reference for
   everything after it.
3. Confirm `UNPACKDIR` exists (`bitbake -e <recipe> | grep -B8 "^UNPACKDIR="`)
   before writing any recipe with a `file://` source.
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

- Disk usage roughly doubles. The VHDX needs sparse mode enabled before week
  4 (`wsl --shutdown`, then `wsl --manage <distro> --set-sparse true`),
  since it grows and never shrinks.
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
- `sources/build-logs/2026-08-27-branch-survey.md` — branch availability
- `sources/build-logs/2026-09-06-first-handwritten-recipe.md` — `S` and the
  absence of `UNPACKDIR`
- `sources/vendor-docs/2026-08-27-meta-imx-8mp-machines.md` — which `meta-imx`
  branches carry `imx8mp-lpddr4-frdm.conf`
