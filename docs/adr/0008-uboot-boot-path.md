# ADR-0008: U-Boot waits one second and goes straight to extlinux on the SD card

## Status

Accepted (2026-10-16). Implemented in the layer in Sprint 8; see
Implementation.

## Context

Sprint 8's boot-time work timed the boot from the serial console with
`tests/boot_timeline.py`, which stamps each console line on the host. From the
SPL banner to `Starting kernel` took 9.1 s (median of three boots, w1–w3):

| Stage | s |
|---|---|
| SPL banner → `Hit any key to stop autoboot` | 1.63 |
| autoboot countdown (until `Scanning for bootflows`) | 4.92 |
| `bootflow scan` → `Starting kernel` | 2.56 |
| **SPL banner → `Starting kernel`** | **9.12** |

Inside the 2.56 s of `bootflow scan` (w1):

```
   10.872 Scanning global bootmeth 'efi_mgr':
   11.020 ^[7^[[r^[[999;999H^[[6n^[8Unable to open OP-TEE session (err=-5)
   11.020 mm_communicate failed!
   11.486 Failed to initialize board for imx8m USB
   11.488 Bus usb@38100000: probe failed, error -62
   11.720 USB XHCI 1.10
   12.849 Bus usb@38200000: 1 USB Device(s) found
   12.852 No bootdevs for 'usb0'
   12.853 Scanning bootdev 'mmc@30b50000.bootdev':
   12.865   0  extlinux     ready   mmc          1  mmc@30b50000.bootdev.part /extlinux/extlinux.conf
```

The global `efi_mgr` bootmeth ran first and failed (0.15 s), then USB came up
and was scanned with nothing bootable on it (1.8 s), all before the SD card,
where extlinux is (ADR-0007). The reasons are in the default environment:

```
board/nxp/imx8mp_frdm/imx8mp_frdm.env   boot_targets=usb0 mmc1 mmc2 dhcp
                                         (no bootmeths: every bootmeth runs, efi_mgr included)
```

The countdown is this layer's own choice. NXP's defconfig leaves
`CONFIG_BOOTDELAY` at its Kconfig default of 2 (`boot/Kconfig:1563–1565`); the
layer's patch of 2026-09-25 raised it to 5, saying "Two seconds is not enough to
reliably interrupt autoboot over a serial console when the terminal is on the
other side of a USB bridge". That was not measured: there is no record of a
test at 2 s.

### What stops autoboot

`common/autoboot.c:395–402` checks for a key before the countdown starts:

```
	/*
	 * Check if key already pressed
	 */
	if (tstc()) {	/* we got a key press	*/
```

and the Kconfig help says: "set to 0 to autoboot with no delay, but you can stop
it by key input. set to -1 to disable autoboot."

Tried by hand on 2026-10-16 at the U-Boot prompt (environment changed with
`setenv`, never saved):

- `setenv bootmeths extlinux; setenv boot_targets mmc1`: no `efi_mgr`, no
  OP-TEE or USB lines; extlinux found on `mmc@30b50000` and the board boots.
- `setenv boot_targets mmc1 mmc2 usb0 dhcp` alone: `efi_mgr` and its OP-TEE
  error still appear (global bootmeths run before any bootdev), USB does not.

## Decision

1. `CONFIG_BOOTDELAY=1`, replacing the patch that set 5.
2. `boot_targets=mmc1 mmc2 usb0 dhcp`: the SD card first, the rest kept as
   fallbacks that are scanned only when the SD card has no bootflow.
3. `bootmeths=extlinux`: `bootflow scan` tries only extlinux, which is how this
   layer boots.

### Why not a delay of 0, or no autoboot prompt at all

With 1 s, pressing a key after `Hit any key to stop autoboot: 1` appears still
stops autoboot (tried by hand, 2026-10-16). That keeps the U-Boot prompt
reachable the usual way, which this project uses often (ADR-0007's spike,
the 2026-10-15 `jr@3000` checks). 0 saves at most one more second and relies
on the key being down before the check. Negative values were not looked into:
-1 disables autoboot (Kconfig help), and anything that removes the key check
locks out the prompt.

### Why not `boot_targets=mmc1` alone

It saves the same 1.8 s on a board that boots from the SD card, and drops the
eMMC, USB and network fallbacks. Keeping them costs nothing while the SD card
boots.

### Why not leave `bootmeths` unset

Only `bootmeths` removes `efi_mgr` (0.15 s and the OP-TEE error on every boot).
Nothing in this layer boots through EFI, a boot script or PXE.

## Results

Timed from Windows (a1–a3, after; w1–w3, before), median of three boots:

| Stage | Before | After | Change |
|---|---|---|---|
| SPL banner → `Hit any key` | 1.63 | 1.68 | +0.06 |
| countdown → `Scanning for bootflows` | 4.92 | 0.92 | −4.00 |
| `Scanning for bootflows` → `Starting kernel` | 2.56 | 0.58 | −1.99 |
| **SPL banner → `Starting kernel`** | **9.12** | **3.18** | **−5.94** |
| SPL banner → `login:` | 23.05 | 16.84 | −6.2 |

The +0.06 s before `Hit any key` was the same in all three boots and is not
explained.

### How the numbers were taken

- Windows, not WSL. WSL's clock ran about 6 % slow against the board and
  against an HTTP `Date` header (121 s against 129 s), with either clocksource;
  Windows' clock matched the board (120.67 s against 120.35 s, two ssh logins
  included).
- Only the stages up to `Starting kernel` are clean. Before `login:` the console
  carries terminal queries (`ESC[6n`, `ESC P+q`) sent while starting the getty;
  nothing answers them, and about 2 s pass before the login banner. The time to
  `login:` measured this way varies by more than 1 s between boots. Linux-side
  timing stays on the board (`serial-getty@ttymxc1` active time).

## Consequences

### Accepted

- `bootflow scan` no longer tries EFI, boot scripts or PXE. If it finds
  nothing, `bootcmd` still runs `bsp_bootcmd`.
- If the SD card has no bootflow, eMMC, then USB, then the network are scanned,
  each only through extlinux.
- U-Boot keeps using its built-in environment (`bad CRC, using default
  environment`; this project does not `saveenv`), so these values come only
  from the patches.

### Revisit if

- An update scheme needs EFI (capsule updates; `CONFIG_EFI_HAVE_CAPSULE_UPDATE`
  is set in the defconfig) or a boot script. Sprint 9's slot work is the first
  place this could come up.
- The board boots from eMMC. `mmc2` is second in `boot_targets`, behind an SD
  card that may still hold an old image.
- Someone cannot stop autoboot in 1 s on their setup (not tried from a cold
  power-on, where the USB serial bridge may reconnect late).

## Implementation

The patches live as commits on `yongchun` over `vendor` in the `uboot-imx` tree
and are exported with `git format-patch` to
`recipes-bsp/u-boot/u-boot-imx/`:

- `0001-configs-imx8mp_frdm-shorten-the-autoboot-delay-to-on.patch` (replaces
  `0001-...-raise-the-autoboot-delay-to-five.patch`)
- `0004-imx8mp_frdm-scan-the-SD-card-first-and-boot-only-thr.patch`

## References

- `sources/build-logs/imx8mp/2026-10-16-boot-time-uboot-timeline.md` — the
  measurements, the WSL clock, the terminal queries
- `sources/build-logs/imx8mp/2026-10-15-boot-time-crng-moal-caam.md` — the
  Linux side of the boot time
- ADR-0007 — why the board boots through extlinux
- U-Boot `lf-6.18.20-2.0.0`: `boot/Kconfig:1563–1569`,
  `common/autoboot.c:388–413`, `board/nxp/imx8mp_frdm/imx8mp_frdm.env`
