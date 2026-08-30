# Yocto Fundamentals

Notes from weeks 1–3, learning Yocto on a Raspberry Pi 4B before moving to the
i.MX8M Plus target. Written while working through the material rather than
afterwards, so the emphasis is on things that were not obvious from the
documentation.

Branch: `scarthgap` (5.0 LTS). See [ADR-0001](adr/0001-yocto-branch-strategy.md)
for why this differs from the branch used from week 4 onwards.

---

## The three pieces, and why the layer README never mentions the others

A layer is not standalone software. It is an overlay, and it assumes the rest
is already there. That is why `meta-raspberrypi`'s README says nothing about
`oe-init-build-env` — the script belongs to poky, not to the layer.

```
poky/                    engine: bitbake + OE-Core + the reference distro
  meta-raspberrypi/      hardware support for one family of boards
  meta-<your-layer>/     your own metadata
       │
  oe-init-build-env      entry point; sets PATH, creates the build directory
       │
  build/conf/
    bblayers.conf        which layers exist
    local.conf           MACHINE, DISTRO, build parameters
```

`source oe-init-build-env` has to be *sourced*, not executed — it modifies the
current shell's environment. Every new terminal needs it again. This is the
single most common cause of `bitbake: command not found`.

Do not install the distribution's `bitbake` package. bitbake ships inside poky,
and having two versions on `PATH` produces failures that are very hard to read.

---

## Layers live outside the build tree

`bblayers.conf` stores **absolute** paths. So does `tmp/`, in the form of
sysroot and cross-toolchain configuration written during the build.

Moving a build directory therefore breaks it twice over:

```
ERROR: The following layer directories do not exist:
ERROR:    /home/.../yocto/poky/meta
ERROR: Please check BBLAYERS in /home/.../build/conf/bblayers.conf
```

and, once that is fixed:

```
ERROR: Error, TMPDIR has changed location. You need to either move it back to
       /home/.../yocto/build/tmp or delete it and rebuild
```

Not everything is path-bound, though, and the distinction matters:

| Directory | Absolute paths inside | Movable | Shareable across trees |
|---|---|---|---|
| `conf/bblayers.conf` | yes | rewrite required | no |
| `tmp/` | yes | delete and rebuild | no |
| `sstate-cache/` | no | yes | possible, rarely useful across branches |
| `downloads/` | no | yes | **yes — clear win** |

The practical conclusion is to keep the layer under version control somewhere
outside the build tree, add it by absolute path, and treat the build tree
itself as disposable:

```
~/git_repo/meta-imx8mp-yongchun/     versioned asset
~/git_repo/yocto/downloads/          shared between trees
~/git_repo/yocto/scarthgap-rpi/      disposable
~/git_repo/yocto/wrynose-imx/        disposable
```

Two trees are unavoidable here because the two phases use different branches,
and a single tree cannot hold two versions of OE-Core.

This separation is also what tools like `kas` exist to formalise: describing
how a build is assembled as declarative configuration, so the tree becomes
a product rather than something that has to be carried around carefully.

---

## Sanity checks fail fast, and that is the point

Both errors above came from `sanity.bbclass`, which validates the environment
before any real work starts. The TMPDIR error surfaced in **four seconds**.

The messages also state the available remedies rather than only reporting a
fault: move it back, or delete and rebuild. That is a deliberate design
choice worth copying — failing before a four-hour build is worth far more
than a precise diagnosis afterwards.

---

## Licensing is enforced, not just recorded

Building `rpi-test-image` stopped with:

```
ERROR: Nothing RPROVIDES 'linux-firmware-rpidistro-bcm43455'
linux-firmware-rpidistro RPROVIDES linux-firmware-rpidistro-bcm43455 but was
skipped: Has a restricted license 'synaptics-killswitch' which is not listed
in your LICENSE_FLAGS_ACCEPTED.
Missing or unbuildable dependency chain was:
  ['rpi-test-image', 'packagegroup-base-extended', 'linux-firmware-rpidistro-bcm43455']
```

The named package is not significant on its own.
`conf/machine/raspberrypi4-64.conf` lists both `bcm43455` and `bcm43456` in
`MACHINE_EXTRA_RRECOMMENDS`, and both carry the same restricted flag — so
more than one package is blocked and the error names whichever the resolver
reached first. Two runs of the same failure named different ones.

Worth knowing while debugging: the package in the message is an example of
the problem, not the cause of it. Chasing that specific name wastes time.

Yocto separates two mechanisms that are easy to conflate:

- `LICENSE` and `LIC_FILES_CHKSUM` **record**. The checksum fails the build if
  upstream changes its licence text, forcing a human to look again.
- `LICENSE_FLAGS` **blocks**. A recipe carrying a restricted flag will not
  build until that flag appears in `LICENSE_FLAGS_ACCEPTED`.

The second exists so that a component with legal implications cannot reach a
shipping image by accident. Accepting it is a one-line change, but it is a
line someone has to write deliberately:

```
LICENSE_FLAGS_ACCEPTED = "synaptics-killswitch"
```

The dependency chain in the error message is the useful part: image →
packagegroup → blocked package. Locating what actually pulled in the
requirement usually needs no further investigation.

---

## Composition is the recurring pattern

`rpi-test-image` does not define an image from scratch; it pulls in a core
image and adds to it. The same shape appears in machine configuration —
`imx8mp-lpddr4-frdm.conf` consists of
`require conf/machine/include/imx8mp-evk.inc` plus a handful of overrides —
and again in device tree, where a board `.dts` includes a SoC `.dtsi`.

Learning it once covers all three.

The two keywords are not interchangeable. `require` fails the parse if the
target is missing; `include` only warns and carries on. A machine
configuration that cannot find its SoC include should fail loudly, so
`require` is correct there. An image recipe adding optional content can
reasonably use `include`.

`bitbake-layers show-recipes "*image*"` is a better way to find what can
actually be built than searching the filesystem, because it reflects the
current configuration rather than what happens to exist on disk.
`show-overlayed` answers the related question of which recipes have been
overridden by another layer — useful when a change appears to have no effect.

---

## Why the second build takes 1% of the time

| | First build | Second build |
|---|---|---|
| Wall clock | 148m 41s | **1m 28s** |
| Tasks attempted | 5468 | 5468 |
| Did not need rerunning | 1659 | 5458 |
| sstate match | 27% | 95% |

The only change between the two was fixing the host `PATH` so that bitbake
used the distribution's Python rather than a Homebrew build of 3.14, which
made the `websockets` module — and therefore the hash equivalence server —
reachable.

Two mechanisms are doing the work:

**Shared state (sstate)** caches task output. If the inputs to a task are
unchanged, the result is restored instead of recomputed. Note that the first
build already benefited: 728 of its cache hits came from an earlier,
abandoned `qemux86-64` build. Native and cross tooling does not depend on
`MACHINE`, so switching target boards does not invalidate it — deleting
`tmp/` after changing `MACHINE` is unnecessary.

**Hash equivalence** relaxes the matching rule. Rather than requiring
identical inputs, it asks whether two different inputs are known to produce
equivalent output, and reuses the cached result if so.

The second build showed this directly. `NATIVELSBSTRING` changed from
`ubuntu-24.04` to `universal` as a side effect of the PATH change. That
variable forms part of the sstate path, so on a naive reading every native
artefact should have been invalidated. Ten tasks were rerun.

Without a reachable hash equivalence server, `SSTATE_MIRRORS` is close to
useless: locally computed hashes will not match the objects published on the
mirror, which is why `Mirrors` stayed at 0 throughout the first build.

The first build is also a reminder that fetching, not compiling, is often the
bottleneck. Its slowest single task was `linux-raspberrypi do_fetch` at 1h 18m,
cloning the full kernel history — more than half the total wall clock.

---

## Environment notes (WSL 2)

- Keep the build tree on the ext4 filesystem. Building under `/mnt/c` or
  `/mnt/d` is slow enough to be unusable.
- The VHDX grows and does not shrink. Yocto consumes tens of gigabytes per
  build, and deleting `tmp/` does not return the space to Windows. Enable
  sparse mode: `wsl --shutdown` then
  `wsl --manage <distro> --set-sparse true`.
- A Homebrew installation on `PATH` will shadow the system Python. Yocto
  expects the distribution's Python; anything installed through `apt` into
  `/usr/lib/python3/dist-packages/` is invisible to a Homebrew interpreter.

---

## Open questions

- Why does the host `PATH` affect the value of `NATIVELSBSTRING`?
- `Mirrors` remained 0 on the second build even with hashserv working. Is the
  mirror simply not consulted once local sstate is sufficient?