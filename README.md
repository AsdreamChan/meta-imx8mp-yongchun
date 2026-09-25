# meta-imx8mp-yongchun

A Yocto BSP layer for the NXP **FRDM-IMX8MPLUS**, built from scratch rather
than copied from a reference board — and a written record of why each decision
went the way it did.

The layer is headless and deliberately minimal. It defines its own distro,
machine and image on top of NXP's BSP, and carries local changes to the kernel
and bootloader as a patch series against the vendor trees.

| | |
|---|---|
| Board | FRDM-IMX8MPLUS (i.MX 8M Plus, 4 GB LPDDR4) |
| BSP | NXP LF6.18.20_2.0.0, Yocto 6.0 "wrynose" |
| Machine | `yongchun`, derived from `imx8mp-lpddr4-frdm` |
| Distro | `yongchun`, based on `fsl-imx-xwayland-minimal`, display stack removed |
| Image | `yongchun-image` — 268 MB, 1519 packages |
| Boot | 11.5 s to `multi-user.target` (4.4 s kernel + 7.1 s userspace) |

`yongchun` is the machine codename, after the Yongchun district in Taipei.

## Why this exists

I spent ten years in embedded firmware — C, ARM Cortex-M, FreeRTOS, and most
recently AUTOSAR Classic Platform complex device drivers on NXP S32K3 under
ISO 26262 and ASPICE. My Linux experience was application-layer. This project
is where I move down the stack into BSP work, and where the automotive domain
knowledge I already have meets a Linux platform.

The layer is therefore not a minimal "hello world" BSP. It deliberately
includes the parts that only show up in real products: update mechanisms,
read-only rootfs, boot-time measurement, licence hygiene, and a diagnostic
stack.

## Roadmap

Built over a twelve-week plan. Nothing is claimed before it is built.

| Weeks | Scope | Status |
|---|---|---|
| 1–3 | Yocto fundamentals on a Raspberry Pi 4B | ✅ |
| 4 | Vendor BSP baseline on FRDM-IMX8MPLUS, boot flow | ✅ |
| 5 | Custom layer, machine, distro; U-Boot and kernel | ✅ |
| 6 | Device tree: I2C sensor, pinctrl/IOMUX, GPIO LED, overlays | ⬜ |
| 7 | Out-of-tree kernel driver (IIO), packaged as a recipe | ⬜ |
| 8 | Productisation: OTA (A/B), boot time, read-only rootfs | ⬜ |
| 9 | Automotive stack: SocketCAN, UDS server, CP↔Linux comparison | ⬜ |
| 10 | CI, reproducible builds (kas), documentation | ⬜ |
| 11 | Buffer | ⬜ |
| 12 | Wrap-up | ⬜ |

Weeks 1–3 happened on a Raspberry Pi 4B and live in a separate private
scratch repository; only the notes from that phase land here, under `docs/`.

## Quick start

Requires the NXP BSP. The layer is added to an existing setup rather than
replacing it.

```bash
mkdir wrynose-imx && cd wrynose-imx
repo init -u https://github.com/nxp-imx/imx-manifest \
          -b imx-linux-wrynose -m imx-6.18.20-2.0.0.xml
repo sync -j4

git clone <this repository> sources/meta-imx8mp-yongchun

MACHINE=yongchun DISTRO=yongchun source ./imx-setup-release.sh -b build
bitbake-layers add-layer ../sources/meta-imx8mp-yongchun
bitbake yongchun-image
```

Set `BB_NUMBER_THREADS` and `PARALLEL_MAKE` before the first build. They both
default to `nproc` and they multiply — on a sixteen-core machine that is up to
256 concurrent compiler processes, which is enough to exhaust 31 GB of RAM
while `rust-native`, `clang-native`, `qtbase`, `boost` and `gcc` compile
together.

## Layout

```
conf/
  layer.conf                  priority 10, above meta-imx-bsp (8) and meta-freescale (5)
  distro/yongchun.conf        no qt, no xen, no display stack; jailhouse kept
  machine/yongchun.conf       FRDM derivative; device trees trimmed from 11 to 2
recipes-core/images/
  yongchun-image.bb           requires imx-image-core; drops docker
recipes-kernel/linux/
  linux-imx_%.bbappend
  linux-imx/can-isotp.cfg     CONFIG_CAN_ISOTP=m
  linux-imx/0001-*.patch      device tree: status LED on the heartbeat trigger
recipes-bsp/u-boot/
  u-boot-imx_%.bbappend
  u-boot-imx/0001-*.patch     defconfig: autoboot delay
docs/
```

## Documentation

Written as the work happened, not reconstructed afterwards. Figures come from
measurements taken at the time; where something could not be established, it
says so.

**Explanation**

- [`00-yocto-fundamentals.md`](docs/00-yocto-fundamentals.md) — six things
  that were not obvious from the documentation, each backed by a measurement
- [`00-vendor-bsp-baseline.md`](docs/00-vendor-bsp-baseline.md) — NXP's BSP
  built and booted unmodified: boot chain, boot time, and what went wrong
  getting there

**Reference**

- [`reference-bitbake-diagnostics.md`](docs/reference-bitbake-diagnostics.md) —
  the commands that answer "where did this value come from", "what depends on
  this", "is the build actually running", and the behaviour that makes each
  necessary
- [`reference-boot-baseline.md`](docs/reference-boot-baseline.md) — the
  Raspberry Pi boot baseline from weeks 1–3, including a first measurement
  that turned out to be invalid and why keeping it was useful

**Decisions**

- [ADR-0001](docs/adr/0001-yocto-branch-strategy.md) — two Yocto releases,
  because no single one covers both the reference material and the board
- [ADR-0002](docs/adr/0002-scarthgap-to-wrynose-migration.md) — what actually
  broke in the migration, amended three times as it was carried out
- [ADR-0003](docs/adr/0003-build-tree-lifecycle.md) — the build tree is
  disposable; shared state is not
- [ADR-0004](docs/adr/0004-headless-distro.md) — removing the display stack,
  and why that had to be decided before the layer had any content
- [ADR-0005](docs/adr/0005-carrying-vendor-patches.md) — local changes as a
  patch series, with an upgrade rehearsal across seventeen stable releases

Corrections are marked where they are made. ADR-0002 claimed NXP's setup
script does not use configuration fragments; it does, and the record says so
rather than being quietly rewritten.

## Notes

**Patches are carried, not forked.** Each vendor component has a work tree
with a `vendor` branch pinned at the BSP's `SRCREV` and a local branch on top;
`git format-patch` regenerates what lives here. Upgrading is a rebase, so
conflicts are bounded by the files actually modified. Every patch carries an
`Upstream-Status` line, which `do_patch` enforces — without it a patch series
only accumulates, with no way to tell which entries upstream has since taken.

**No vendor binaries are redistributed in this repository.** NXP firmware
blobs — DDR training firmware (`lpddr4_pmu_train_*.bin`) and similar — are
covered by NXP's licence terms. They are fetched at build time through
`SRC_URI`, never committed here, and `.gitignore` blocks `*.bin`, `*.fw` and
`*.elf` so that a stray `git add .` cannot leak them.

Yocto distinguishes two licensing mechanisms, and this layer uses both
deliberately:

- `LICENSE` and `LIC_FILES_CHKSUM` **record** what a recipe ships and verify
  the licence text has not changed upstream. A checksum mismatch fails
  `do_populate_lic`, and the message asks whether the `LICENSE` value is still
  correct — not to update the checksum.
- `LICENSE_FLAGS` **blocks**. A recipe carrying a restricted flag will not
  build until the flag is named in `LICENSE_FLAGS_ACCEPTED`, so restricted
  components cannot reach an image by accident.

Restricted components this layer depends on are accepted by name, with the
reason, rather than through a blanket accept.

**Not a production configuration.** Root logs in without a password, inherited
from the vendor setup script's `root-login-with-empty-password` fragment.

## Status

Week 5 of 12 — see the roadmap above. Issues and corrections are welcome,
particularly on anything an ADR marks as unresolved.

## Licence

MIT. See [LICENSE](LICENSE).
