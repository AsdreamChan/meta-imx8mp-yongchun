# ADR-0006: The board boots a composite device tree named by U-Boot's default environment

## Status

Accepted (2026-10-07)

## Context

Sprint 7's driver binds to two `yongchun,tpg` nodes. For the driver to load
at boot by modalias, those nodes have to be in the device tree the kernel is
handed. Until now they came from an overlay applied by hand at the U-Boot
prompt on every boot (`fatload`, `fdt apply`, `booti`), which is a test
procedure, not a boot.

The nodes describe no hardware. They are test devices for one driver, which
is why they were written as an overlay in the first place, the same way the
LCD1602 was moved out of the base tree in Sprint 6.

### What decides which device tree is loaded

In NXP's U-Boot for this board (`uboot-imx`, `lf-6.18.20-2.0.0`):

```
board/nxp/imx8mp_frdm/imx8mp_frdm.env:54   fdtfile=CONFIG_DEFAULT_FDT_FILE
board/nxp/imx8mp_frdm/imx8mp_frdm.env:66   loadfdt=fatload mmc ${mmcdev}:${mmcpart} ${fdt_addr_r} ${fdtfile}
configs/imx8mp_frdm_defconfig:41           CONFIG_DEFAULT_FDT_FILE="imx8mp-frdm.dtb"
configs/imx8mp_frdm_defconfig:95           CONFIG_ENV_IS_IN_MMC=y
```

`mmcboot` loads `${fdtfile}` and boots it. So the file the board boots is a
single variable in the default environment, and that default comes from one
defconfig line. The catch is the last line: an environment saved to MMC
replaces the default entirely, and then the defconfig no longer matters.

### Three ways to get the nodes in at boot

| Approach | Where the choice lives | Cost |
|---|---|---|
| **A. Composite dtb, U-Boot default `fdtfile` points at it** | Kernel patch (dtso + composite target), machine conf, U-Boot defconfig patch | Touches U-Boot; depends on no saved environment |
| B. Nodes written directly into `imx8mp-frdm.dts` | Kernel patch only | Test devices become part of the board description, and every tree built on the base (the LCD composite included) inherits them |
| C. Composite dtb, `setenv fdtfile …; saveenv` on the board | The board's MMC environment area | Not in the build; does not survive a new board; once an environment is saved, every later `CONFIG_DEFAULT_*` change in the defconfig silently stops applying |

## Decision

**A.** The overlay moves into the kernel tree as
`imx8mp-frdm-tpg.dtso`, the kernel Makefile builds a composite
`imx8mp-frdm-tpg.dtb` (base + overlay, merged by `fdtoverlay` at build time),
both are added to `KERNEL_DEVICETREE`, and a U-Boot defconfig patch sets
`CONFIG_DEFAULT_FDT_FILE="imx8mp-frdm-tpg.dtb"`.

The plain `imx8mp-frdm.dtb` is still built and still on the boot partition.

### Why not B

It is the smallest change and needs no U-Boot work, which is a real argument.
But the base tree would stop being a description of the board: a reader of
`imx8mp-frdm.dts` should not find two fake devices in it, and the LCD
composite, built on that base, would carry them too. Sprint 6 already took the
other path for the LCD; doing it differently here would leave two conventions
in one layer.

### Why not C

It is the fastest, and it is the one that looks like a fix while being a
manual step. The setting would exist only in one board's MMC environment area. Two further effects are reasoned rather than tested:

- Flashing a new wic image may not overwrite the environment at
  `CONFIG_ENV_OFFSET` (0x700000), so a saved environment could outlive the
  image that it was made for.
- Once any environment is saved, the default environment is no longer
  consulted, so Sprint 5's `CONFIG_BOOTDELAY` change and any later defconfig
  default would stop taking effect without any error.

It also breaks the rule of not running `saveenv` during U-Boot experiments.

### A patch, not a configuration fragment

oe-core wrynose merges `*.cfg` files from `SRC_URI` into the U-Boot `.config`
(`meta/recipes-bsp/u-boot/u-boot-configure.inc:42-44`), and meta-imx's
`u-boot-imx_2026.04.bb` requires `u-boot.inc`, so a one-line
`default-fdt.cfg` would have worked. A defconfig patch was used instead
because Sprint 5's `CONFIG_BOOTDELAY` is already a defconfig patch in the same
file, and one mechanism per file is easier to read than two.

## Results

First boot after flashing, from the serial console:

```
Loading Environment from MMC... Reading from MMC(1)... *** Warning - bad CRC, using default environment
...
45046272 bytes read in 500 ms (85.9 MiB/s)
Booting from mmc ...
83767 bytes read in 3 ms (26.6 MiB/s)
```

- `bad CRC, using default environment`: nothing is saved on this board, so
  the defconfig default applies. The precondition for A holds.
- `83767 bytes` is the size of the deployed `imx8mp-frdm-tpg.dtb`. U-Boot
  loaded the composite.
- The driver loaded at boot with no overlay and no `insmod`; `modprobe`,
  `modprobe -r` and a second reboot all behaved: the done criteria of the
  module-recipe story are met.

## Consequences

### Accepted

- **One name in three places.** `imx8mp-frdm-tpg.dtb` appears in the kernel
  Makefile (via the patch), in `KERNEL_DEVICETREE`, and in the U-Boot patch.
  Renaming it means changing all three. If the file is missing from the boot
  partition, `mmcboot` prints `WARN: Cannot load the DT` and does not boot
  (`imx8mp_frdm.env`, `mmcboot`); recovery is `setenv fdtfile
  imx8mp-frdm.dtb; boot` at the prompt. Not tested.
- **Composites multiply.** Each combination of overlays is its own target. LCD
  and test pattern generator together would be a third composite, not a
  setting.
- **Changing what the board boots now costs a U-Boot rebuild**, which also
  rebuilds `imx-boot`, and a full reflash, because the bootloader is part of
  the wic image.

### Gained

- No manual step at boot. Everything that decides what the board boots is in
  the layer and reproducible from it.
- The base tree still describes only the board, and still boots: the plain
  `imx8mp-frdm.dtb` is on the boot partition for the prompt.

### Unexpected

- **The U-Boot banner date does not show whether U-Boot changed.** It still
  reads `Jun 02 2026` after the rebuild: reproducible builds take the build
  time from the source, and the kernel's `#1 SMP PREEMPT Tue Jun  9` likewise.
  The evidence that the new bootloader ran was behavioural, the size of the
  device tree it loaded.

### Revisit if

- Sprint 8's A/B update needs a persistent U-Boot environment (a bootcount, an
  active-slot variable written by `fw_setenv`). Once an environment is saved,
  the defconfig default stops being authoritative, and `fdtfile` must be set
  by whatever owns the environment from then on.
- More than one or two overlays need to be combined. At that point U-Boot
  applying overlays at boot (a boot script, or a FIT image with configuration
  nodes) scales better than a composite per combination.
- A board turns up with a saved environment. Check for `bad CRC, using default
  environment` before trusting any defconfig default on it.

## References

- `sources/build-logs/imx8mp/2026-10-07-yongchun-tpg-story3-image-autoload.md`
- `sources/build-logs/imx8mp/2026-10-07-story3-serial-boot.txt`
- `sources/build-logs/imx8mp/2026-10-03-lcd1602-overlay-build-and-boot.md` —
  the composite-dtb pattern this follows
- [ADR-0005](0005-carrying-vendor-patches.md) — how the kernel and U-Boot
  patches are carried
