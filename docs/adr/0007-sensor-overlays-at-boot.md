# ADR-0007: Pluggable sensors reach the device tree as overlays listed in extlinux.conf

## Status

Accepted (2026-10-09), on the strength of a hand-written spike. The layer
implementation is not done yet; see Implementation.

## Context

Sprint 7 brought up a BMP280 on I2C3 with the mainline driver. Its node existed
only in a development overlay, `bmp280.dtbo`, applied by hand at the U-Boot
prompt on every boot. With the normal boot the board has no BMP280 node, so the
driver never loads, and a Sprint 10 test on the board cannot cover it.

The sensor is a breadboard module. It can be absent, moved to another address,
or joined by others (a BH1750 is on order). That makes it unlike the test
pattern generator nodes of ADR-0006, which are fixed for the life of one driver.

ADR-0006 named this point as a reason to revisit it: "More than one or two
overlays need to be combined. At that point U-Boot applying overlays at boot
(a boot script, or a FIT image with configuration nodes) scales better than a
composite per combination."

### What the boot already does

From the base dtb boot test (2026-10-09) and NXP's U-Boot (`uboot-imx`,
`lf-6.18.20-2.0.0`):

```
configs/imx8mp_frdm_defconfig:40   CONFIG_BOOTCOMMAND="run sr_ir_v2_cmd;bootflow scan -lb; run bsp_bootcmd"
configs/imx8mp_frdm_defconfig:196  CONFIG_OF_LIBFDT_OVERLAY=y
boot/Kconfig:589-592               config BOOTMETH_EXTLINUX ... default y
```

and on the board, `bootmeth list` shows `extlinux` at order 0.

Every boot already runs `bootflow scan` before NXP's `bsp_bootcmd`. Until now it
found nothing (`0 bootflows, 0 valid`) and fell through. An
`extlinux/extlinux.conf` on a scanned partition is found first. U-Boot's
extlinux parser accepts an `FDTOVERLAYS` line and applies each overlay to the
main device tree before booting (`boot/pxe_utils.c`, `label_boot_fdtoverlay()`).

On the build side, oe-core wrynose's `uboot-extlinux-config.bbclass` writes
that line from `UBOOT_EXTLINUX_FDTOVERLAYS` (lines 70, 141, 150–151, the same
locally and upstream). `bitbake -e virtual/bootloader` shows the class is
inherited by `u-boot-imx` and not enabled (`UBOOT_EXTLINUX` unset).

### Four ways to get sensor nodes in at boot

| Approach | Where the choice lives | Cost |
|---|---|---|
| A. Another composite: base + tpg + bmp280, U-Boot default `fdtfile` points at it | Kernel patch, machine conf, U-Boot defconfig patch | One composite per combination of modules; a full kernel rebuild (about 46 min) and a reflash to add or drop a module; an absent module still has a node |
| B. NXP's `bsp_bootcmd` extended to apply a list of overlays | A patch to `imx8mp_frdm.env` written by hand | A boot script of our own; a failed `fdt apply` can leave the tree half applied, so the fallback has to be written too |
| **C. `extlinux.conf` with `FDT` and `FDTOVERLAYS`** | Machine conf (`UBOOT_EXTLINUX_*`), one line in the U-Boot environment, the kernel patch that builds the `.dtbo` | The boot no longer goes through `bsp_bootcmd`, which changes how OP-TEE's reserved memory is described (see Results); `bootargs` is written a second time, in `APPEND` |
| D. Leave it manual | Nothing | The default boot has no sensor; nothing on the board can be tested automatically |

## Decision

**C.** The layer enables `UBOOT_EXTLINUX` with `FDT` pointing at the ADR-0006
composite and `FDTOVERLAYS` listing one overlay per module, and the file is put
on the FAT boot partition as `/extlinux/extlinux.conf`. A U-Boot environment
patch defines `fdtoverlay_addr_r`. Adding or removing a module is then a change
to one line on the boot partition, with no kernel or U-Boot rebuild.

OP-TEE's reserved memory is accepted as U-Boot's fallback describes it on this
path (see Results).

### Why not A

It is the smallest step from ADR-0006 and has no boot-flow risk. But it makes a
removable module part of the default hardware description, and the number of
composites grows with every combination. ADR-0006 already lists this as the
condition for moving on.

### Why not B

It reaches the same result as C with a script we would have to write and
maintain, against a U-Boot that already ships a parser for the standard format.

### Why not D

A sensor the board does not boot with cannot be part of the Sprint 10 tests.

## Results

A hand-written `extlinux/extlinux.conf` on the boot partition, using the
development `bmp280.dtbo`, no build, three boots
(`2026-10-09-extlinux-fdtoverlays-spike.md`):

1. **Without `fdtoverlay_addr_r`** the board booted through extlinux
   (`Running BSP bootcmd` never printed), printed `Invalid fdtoverlay_addr_r
   for loading overlays`, skipped every overlay and booted without the sensor.
   Neither `imx8mp_frdm.env` nor the defconfig defines the variable. The failure
   is one line on the serial console and nothing else.
2. **With `setenv fdtoverlay_addr_r 0x43400000`** at the prompt (not saved),
   U-Boot retrieved `/bmp280.dtbo`, and `bmp280_i2c` loaded at boot by modalias.
3. **OP-TEE.** `/firmware/optee` is identical on both paths. `/reserved-memory`
   is not: `sr_ir_v2_cmd` sets `opteeoverlay no` and only `bsp_bootcmd` sets it
   back to `yes`, so the extlinux path uses U-Boot's fallback with a hard-coded
   4 MiB shared-memory size (`dt_optee.c:53-54`, `optee.h:8`).

   | | `bsp_bootcmd` (OP-TEE's own overlay) | extlinux (U-Boot fallback) |
   |---|---|---|
   | `optee_core` | `0x56000000`, 30 MiB | `0x56000000`, 28 MiB |
   | `optee_shm` | `0x57e00000`, 2 MiB | `0x57c00000`, 4 MiB |
   | Total | `0x56000000`–`0x57ffffff` | the same |
   | Driver | 4.10, dynamic shared memory, `/dev/tee0` | the same |

   The total is the same and all of it is `nomap`, so Linux uses none of it on
   either path; only the label on 2 MiB differs, and the driver reports dynamic
   shared memory, which reads as not taking its buffers from that node.
4. **Back out.** Renaming `extlinux/` to `extlinux.off` returned the board to
   `bsp_bootcmd` on the next boot (OP-TEE's 2 MiB layout came back, the sensor
   went away).

## Consequences

### Accepted

- **Two boot paths exist.** `bootflow scan` with extlinux is the normal path;
  `bsp_bootcmd` with ADR-0006's `fdtfile` is the fallback, and is what runs
  whenever the extlinux file is missing.
- **OP-TEE's reserved memory is described by U-Boot, not by OP-TEE**, on the
  normal path. The alternative is changing NXP's `CONFIG_BOOTCOMMAND` so that
  `opteeoverlay` is `yes` before `bootflow scan`; that also changes what the
  EFI path gets, for a reason NXP did not write down, so it is not done.
- **A missing `fdtoverlay_addr_r` fails silently.** The board boots without its
  sensors. A boot test has to check the sensor is present, not only that the
  board came up.
- **`bootargs` in two places**: `mmcargs` in U-Boot's environment and `APPEND`
  in `extlinux.conf`.
- **A third U-Boot patch**, for `fdtoverlay_addr_r`.

### Gained

- A module is added or removed by editing one line on the boot partition, from
  Linux, with no rebuild.
- The overlay mechanism is U-Boot's standard one, not NXP's script or ours.

### Revisit if

- Sprint 8's update mechanism (RAUC or SWUpdate) needs its own boot script for
  A/B slots. The slot selection and the overlay list have to live in the same
  place, which may not be extlinux.conf.
- OP-TEE moves to static shared memory, or its memory layout changes. The
  fallback's hard-coded 4 MiB would then disagree with OP-TEE in a way that
  matters.

## Implementation

Not in Sprint 7. Candidates: Sprint 8, next to the update mechanism, because
both decide the boot path; or Backlog. Decided by the owner.

What the spike showed has to be in the layer, and what it did not test:

- **U-Boot env patch**: `fdtoverlay_addr_r=0x43400000`, the address used in the
  spike, between `fdt_addr_r` (0x43000000) and `scriptaddr` (0x43500000).
- **Paths**: the class defaults to `KERNEL ../${KERNEL_IMAGETYPE}` and
  `FDTDIR ../`, relative to a file installed in the root filesystem at
  `/boot/extlinux/`. The spike used absolute paths on the FAT boot partition.
  Set `UBOOT_EXTLINUX_KERNEL_IMAGE`, `UBOOT_EXTLINUX_FDT` and
  `UBOOT_EXTLINUX_FDTOVERLAYS` to absolute paths, and get the deployed
  `extlinux.conf` (`u-boot.inc:368-370`) onto the boot partition through
  `IMAGE_BOOT_FILES` as `extlinux.conf;extlinux/extlinux.conf`. Not tested.
- **Console**: the class default is `console=${console},${baudrate}`, and the
  extlinux parser expands U-Boot variables in `APPEND`
  (`pxe_utils.c:654-656`). This board's `console` is already `ttymxc1,115200`
  (`imx8mp_frdm.env:50`), so the default would give
  `console=ttymxc1,115200,115200`. Set `UBOOT_EXTLINUX_CONSOLE` explicitly.
  Read from source, not tested.
- **The `.dtbo`**: a kernel patch adds `imx8mp-frdm-bmp280.dtso` and
  `KERNEL_DEVICETREE` lists the `.dtbo`, as ADR-0006 did for the tpg overlay.
- **Checks after flashing**: `Running BSP bootcmd` absent from the serial log,
  no `Invalid fdtoverlay_addr_r`, `/proc/cmdline` (not looked at in the
  spike), the sensor bound.

## References

- [ADR-0006](0006-boot-device-tree-selection.md) — the composite this builds on, and the revisit condition
- `sources/build-logs/imx8mp/2026-10-09-extlinux-fdtoverlays-spike.md` — the spike
- `sources/build-logs/imx8mp/2026-10-09-base-dtb-boot.md` — the `CONFIG_BOOTCOMMAND` sequence and the fallback to `bsp_bootcmd`
- `sources/build-logs/imx8mp/2026-10-08-bmp280-mainline-iio.md` — the BMP280 bring-up and the manual overlay
- `sources/build-logs/imx8mp/2026-10-08-bmp280-mainline-iio-addendum.md` §4a — `opteeoverlay`

