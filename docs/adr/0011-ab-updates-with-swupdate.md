# ADR-0011: A/B updates with SWUpdate, each slot a boot and a root partition, U-Boot counting boots

## Status

Accepted (2026-11-18), after one update and one rollback on the board; see
Results. Proposed on 2026-11-17, from a spike: the board's state, a GPT image
built and booted, and U-Boot and SWUpdate source read at the versions this
layer builds. Signing, part of the decision, is not implemented yet.

Amends [ADR-0008](0008-uboot-boot-path.md): how U-Boot finds the partition to
boot, and whether the environment is ever saved.

## Context

Sprint 11 adds over-the-air updates. An update that fails must leave a board
that still boots, so the image is written to a second copy of the system (A/B)
and the bootloader decides which copy to start and falls back when the new one
does not come up.

### What the board does today

From the spike on 2026-11-16:

- **Partitions**: NXP's `imx-imx-boot-bootpart.wks.in`, an msdos table with
  imx-boot raw at 32 KiB, a 256 MiB FAT `boot` partition (kernel, device tree,
  overlays, `extlinux.conf`; ADR-0007) and an ext4 root. The SD card is 29 GiB;
  the image uses 2 GiB of it.
- **U-Boot environment**: in MMC 1 (the SD card) at `0x700000`, 16 KiB
  (`CONFIG_ENV_OFFSET=0x700000`, `CONFIG_ENV_SIZE=0x4000`), one copy
  (`CONFIG_ENV_REDUNDANT` not set). It has never been saved, so every boot
  prints `bad CRC, using default environment`. A `saveenv` there and an erase of
  the same 32 blocks both left the board booting; imx-boot ends at about
  2.3 MiB, well before 7 MiB.
- **No boot counting**: `CONFIG_BOOTCOUNT_LIMIT` is not set, and the
  environment has no `bootlimit` or `altbootcmd`.
- **Linux cannot reach the environment**: no `fw_printenv`, `fw_setenv` or
  `/etc/fw_env.config` in the image.
- **No `sysboot`**: `CONFIG_CMD_SYSBOOT` is not set. `BOOTMETH_EXTLINUX`,
  `PXE_UTILS` and ext4 support are.

### The update agent

SWUpdate and RAUC both have a `wrynose` branch of their Yocto layer, and
neither layer brings the U-Boot side: in both, the bootloader logic is the
project's. RAUC fixes the contract between the agent and U-Boot
(`BOOT_ORDER`, `BOOT_<slot>_LEFT`, a boot script). SWUpdate leaves it to the
project and suggests U-Boot's own boot counter.

NXP's application note AN13872 (Rev. 5) describes SWUpdate on i.MX 6ULL,
8M Mini and 93: two slots, each a boot partition with kernel and device tree
plus a root file system; U-Boot's bootcount with `bootlimit=3` and
`altbootcmd`; a redundant environment; `fw_env.config` written from a
`u-boot-imx` bbappend. NXP's `meta-swupdate-imx` carries this as U-Boot
patches. Its newest branch, `walnascar_6.12.49`, has patches for i.MX 95 only,
and puts the environment at `0x700000` and `0x704000`, 16 KiB each — the same
offset this board already uses. There is no `wrynose` branch and nothing for
the 8M Plus, so the U-Boot side has to be ported here either way.

### Ways to pick the slot in U-Boot

| Approach | Result |
|---|---|
| `bootflow scan mmc1:<part>`, which the source accepts as one partition (`bootdev-uclass.c:695–718`) | On the board, any label — `mmc1`, `mmc1:1`, `mmc1:2`, the bootdev's full name — finds nothing, even after the bootdevs are probed. The error is not printed (`bootflow.c:565–567`). Cause unknown |
| Toggle the bootable flag between slots; a scan of the whole disk only looks at bootable partitions (`bootdev-uclass.c:170–174`; on the board p2 returns `-22`) | Every update would rewrite the partition table from Linux |
| A boot script that loads the kernel itself (RAUC's example) | Overlays (ADR-0007) would have to be applied by hand with `fdt apply` |
| **`sysboot` on the slot's boot partition** | Reads that slot's `extlinux.conf`, so `FDT` and `FDTOVERLAYS` work as now. Needs `CONFIG_CMD_SYSBOOT` |

### Where the kernel goes

| Layout | Partitions | |
|---|---|---|
| A. Kernel inside each root (`/boot`) | rootA, rootB, data | One image per update, fits msdos. Departs from NXP's layout; overlays can no longer be edited from a PC |
| **B. A boot and a root partition per slot (NXP's layout)** | bootA, rootA, bootB, rootB, data | Matches AN13872. Five partitions: msdos has four primaries, so GPT |
| C. One shared boot partition | boot, rootA, rootB, data | The kernel is outside A/B: an update cannot change it or roll it back, and new modules would meet an old kernel |

### GPT on this board

Tested on 2026-11-16 with a copy of the current wks changed only to
`bootloader --ptable gpt`:

- U-Boot (`CONFIG_EFI_PARTITION`, 128 entries at the default offset) and the
  kernel (`CONFIG_EFI_PARTITION`) read GPT.
- The image's first usable LBA is 34 (17 KiB), and imx-boot is byte-identical
  at 32 KiB (`cmp` against `imx-boot.tagged`).
- wic turns `--active` into the `LegacyBIOSBootable` attribute, and U-Boot
  treats it as the bootable flag: p1 boots, p2 is skipped with `-22`, as with
  msdos.
- The kernel mounts the root and shows the partition names. It warns that the
  backup GPT is not at the end of the card: it is at the end of the 2 GiB image.

## Decision

**SWUpdate, NXP's slot layout (B) on GPT, U-Boot's boot counter, and `sysboot`
to start the chosen slot.** U-Boot's environment variables follow
`meta-swupdate-imx` where they can, so the design reads like NXP's.

### Partitions (GPT)

| # | Name | Contents | Size |
|---|---|---|---|
| — | — | imx-boot, raw at 32 KiB; U-Boot environment at 7 MiB | — |
| 1 | bootA | FAT: `Image`, dtb, dtbo, `extlinux/extlinux.conf` | 256 MiB |
| 2 | rootA | ext4 root | 3 GiB |
| 3 | bootB | as bootA | 256 MiB |
| 4 | rootB | as rootA | 3 GiB |
| 5 | data | ext4, kept across updates (the read-only root's writable layer) | 1 GiB |

Sizes are fixed rather than grown to the card, so the backup GPT stays where
wic puts it. The root file system is 1.7 GiB today.

### U-Boot

As patches in `recipes-bsp/u-boot/u-boot-imx/`, the same way as ADR-0008's:

- **Config**: `CONFIG_CMD_SYSBOOT`, `CONFIG_BOOTCOUNT_LIMIT`,
  `CONFIG_BOOTCOUNT_ENV`, `CONFIG_ENV_REDUNDANT` with
  `CONFIG_ENV_OFFSET_REDUND=0x704000`. NXP's patch names the redundant
  environment `CONFIG_SYS_REDUNDAND_ENVIRONMENT`; in this U-Boot (2026.04) it
  is `CONFIG_ENV_REDUNDANT` (`env/Kconfig:481`).
- **Environment**: `bootslot` (`dualA` or `dualB`), `bootlimit=3`, and an
  `adjustbootsource` that sets the slot's boot and root partition numbers
  (1 and 2, or 3 and 4).
- **`bootcmd`**: run `adjustbootsource`, then
  `sysboot mmc 1:<boot partition> any ${scriptaddr} /extlinux/extlinux.conf`.
  The existing `bootflow scan` and `bsp_bootcmd` stay after it as fallbacks.
- **`altbootcmd`**: switch `bootslot` to the other slot, clear
  `upgrade_available` and `bootcount`, save, and boot.

`bootcount_env.c` stores and reads `bootcount` only while
`upgrade_available` is set (and saves the environment each time); once
`bootcount` exceeds `bootlimit`, `autoboot.c:508–511` runs `altbootcmd`
instead of `bootcmd`. A normal boot writes nothing.

### extlinux

Each boot partition carries the same `extlinux.conf`. Its `APPEND` takes the
root from the environment, `root=/dev/mmcblk1p${rootpart}`: U-Boot expands
macros in `APPEND` before setting `bootargs` (`pxe_utils.c:654`).
`FDT` and `FDTOVERLAYS` stay as ADR-0007 has them.

### Linux and SWUpdate

- `libubootenv` and `/etc/fw_env.config` with both copies,
  `/dev/mmcblk1 0x700000 0x4000` and `/dev/mmcblk1 0x704000 0x4000`.
- `sw-description` holds a boot image and a root image. SWUpdate writes them
  to the slot that is not running, then sets `bootslot` to it,
  `upgrade_available=1` and `bootcount=0`.
- A service started late in the boot clears `upgrade_available` and
  `bootcount`: the new slot is kept. If it never runs, U-Boot rolls back after
  `bootlimit` attempts.
- **Update images are signed** and SWUpdate refuses unsigned ones. The private
  key stays on the build host outside this repository, like `kas/local.yml`;
  only the public key goes into the image. Signing is part of Sprint 11, done
  before the sprint is accepted.

### Not updated

- **imx-boot (SPL, ATF, U-Boot)** is outside A/B. AN13872 does not recommend
  updating the bootloader (section 4.3); a power cut during it can leave the
  board unbootable.
- **The U-Boot environment** is not part of either slot. Changing the boot logic
  later means changing the environment through the update, or erasing it.

## Alternatives considered

- **RAUC.** Its U-Boot contract is fixed and documented with an example script.
  It moves to the backlog: NXP's material for i.MX is SWUpdate, and the U-Boot
  side above serves either agent.
- **Layout A, the kernel in each root's `/boot`** (three partitions, msdos).
  One image per update, fewer partitions, no change of partition table. Not
  taken, to stay with NXP's layout, which is what AN13872 and NXP's layer
  describe and what a reader coming from them expects.
- **Layout C, one shared boot partition.** The smallest change, but the kernel
  would not be updated or rolled back with the root.
- **`bootflow scan` on one partition.** Would keep `bootcmd` as it is. Does not
  work on this board, cause unknown (Context).
- **Unsigned images for this sprint.** Would save the key handling. Not taken:
  an update path that installs anything it is given is not one to show.

## Consequences

### Accepted

- **The environment is saved from now on.** The first boot of an update saves
  it, and `fw_setenv` writes it from Linux. After that U-Boot uses the saved
  values, not the defaults compiled into a new U-Boot: ADR-0008's "this project
  does not `saveenv`" no longer holds, and a change to the default environment
  needs the old one erased to take effect.
- **The partition table is GPT.** Linux tools warn that the backup table is not
  at the end of the card. GPT's header sits in sector 1, where i.MX keeps the
  optional secondary image table for a second bootloader copy (AN13872, p.50).
  The boot ROM falls back to that copy when the primary fails authentication
  in the closed security setting (p.48), so it belongs with secure boot; using
  it later would conflict with GPT.
- **Two images per update**, boot and root, and both slots carry a copy of the
  kernel and device trees.
- **A reset the board makes on its own during boot counts as a failed boot.**
  The board reset itself early in systemd on 2026-11-16 (right after
  `Hostname set`) and on 2026-11-17 (about 1.5 s after it, on its usual power
  supply); the cause is not known. Such a reset makes a rollback test
  meaningless unless the serial log shows that every counted boot followed a
  `reboot`, as it did in the test under Results.

### Gained

- An interrupted or broken update leaves the previous slot to boot.
- The kernel, device trees and overlays update and roll back with the root.
- The design and its variable names can be read against AN13872 and NXP's
  layer.

### Revisit if

- `bootflow scan` with a partition label is found to work; `sysboot` and its
  config line could then go.
- The board boots from eMMC. Device and partition numbers are written for the
  SD card (`mmc 1`, `/dev/mmcblk1`).
- The root file system outgrows 3 GiB.

## Results

On the board on 2026-11-18, with one `.swu` built from this layer, unsigned.
The board started on slot A; nothing was flashed.

- **Update, A to B.** SWUpdate chose `copy-B`, wrote the boot image (18.7 MB
  gzipped) to bootB and the root image (336.5 MB gzipped) to rootB, and set
  `bootslot=dualB`, `upgrade_available=1`, `bootcount=0`. U-Boot saved the
  environment once and booted slot dualB from `mmc 1:3`, with
  `root=/dev/mmcblk1p4`. `yongchun-mark-good` ran, and after login the
  environment read `dualB`, `0`, `0`. On the next boot U-Boot saved nothing.
- **Rollback, B to A.** From slot B, an update to A, and before rebooting the
  service was masked in the new rootA (a link to `/dev/null`), so the new slot
  boots but never reports success. Slot dualA booted three times, with
  `bootcount` 1, 2 and 3, each saved by U-Boot. On the fourth boot U-Boot
  printed `Warning: Bootlimit (3) exceeded. Using altbootcmd.` and
  `Boot limit reached on slot dualA, rolling back ...`, saved, and booted
  dualB from `mmc 1:3`. Linux came up on rootB with `dualB`, `0`, `0`.
- **Every reset was a `reboot`.** U-Boot started six times in the run, each
  time two lines after the kernel's `reboot: Restarting system`, so no counted
  boot came from the board resetting itself. The resets of 2026-11-16 and
  2026-11-17 remain unexplained. The author suspects the supply: the power
  cable then in use came with the Raspberry Pi and may carry 3 A or less,
  while a USB hub and the CANable drew from the board. Not measured yet.
- **The rollback boot writes the environment twice**: once when `bootcount`
  reaches 4, once in `altbootcmd`.

Not yet done:

- **Signing** (Decision, "Update images are signed"). The `.swu` above is
  unsigned and the image accepts unsigned ones.
- **SSH host keys and the machine ID** are generated anew after each update,
  because they live in the root file system; `ssh` clients see a changed host
  key. They belong on the data partition with the read-only root.

## References

- `sources/build-logs/imx8mp/2026-11-16-ota-spike-boot-env-and-partitions.md`
  and its two addenda — the board's partitions, environment, U-Boot config,
  `saveenv` and erase, `mmc@30b50000` is `mmcblk1`
- `sources/build-logs/imx8mp/2026-11-16-uboot-bootflow-label-scan.md` —
  `bootflow scan` with a label
- `sources/build-logs/imx8mp/2026-11-16-gpt-wic-test.md` — the GPT test,
  and the two open items (journal not loaded on the first flash, the reset)
- `sources/build-logs/imx8mp/2026-11-17-uboot-ab-slot-bootcount-and-fw-env.md`,
  `2026-11-17-ota-step2-gpt-ab-slots.md`,
  `2026-11-17-ota-step3-swupdate-rootfs-to-slot-b.md`,
  `2026-11-18-ota-step4-boot-partition-in-swu.md` — the U-Boot side, the
  five-partition card, and SWUpdate writing the root, then the boot partition
- `sources/build-logs/imx8mp/2026-11-18-ota-step6-mark-good-and-rollback.md` —
  the update and the rollback under Results
- `sources/build-logs/cross/2026-11-18-swupdate-bootloader-handler-systemd-and-uboot-bootcount-env.md`
  and its addendum — why SWUpdate needs `CONFIG_BOOTLOADERHANDLER` and
  `CONFIG_SYSTEMD`, and how `bootcount` is stored
- `sources/build-logs/cross/2026-11-16-ota-layers-wrynose-branches.md`,
  `…-ota-swupdate-rauc-uboot-integration.md`,
  `…-uboot-extlinux-append-and-redundant-env.md`,
  `…-uboot-bootcount-env-and-single-partition-scan.md`,
  `…-meta-swupdate-imx-uboot-changes.md` — source read in the cloud at the
  versions this layer builds
- `sources/vendor-docs/an13872-r5-swupdate-imx-excerpt.txt`,
  `…-excerpt-p12.txt` and `…-excerpt-p48-51.txt` — AN13872, Rev. 5
- [ADR-0007](0007-sensor-overlays-at-boot.md) — overlays through extlinux
- [ADR-0008](0008-uboot-boot-path.md) — U-Boot's boot path, amended here

Drafted by Claude from the sources above; the decision is the author's.
