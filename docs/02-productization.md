# Productization

What it takes to turn the BSP from a development board image into something a
product could ship. Each section is written when its story is done; the rest
(read-only rootfs, OTA, secure boot) come in Sprint 10.

## Boot time

U-Boot now hands over to the kernel 5.9 s sooner (9.1 s → 3.2 s from the SPL
banner). In Linux, the kernel's random number generator is ready 1.7 s sooner;
the serial login moved by less than the spread between boots.

### How it is measured

The boot is measured in two parts, each with the clock that sees it.

| Part | From → to | Clock | Tool |
|---|---|---|---|
| Firmware | `U-Boot SPL` banner → `Starting kernel ...` | host, stamping each serial line | `tests/boot_timeline.py` |
| Linux | kernel start → `serial-getty@ttymxc1.service` active | board (`CLOCK_MONOTONIC`, same as dmesg) | `systemctl show -p ActiveEnterTimestampMonotonic` |

- The Linux part ends at the serial getty, not at `multi-user.target`, which
  also waits for dbus, avahi and ofono and is not when a user can log in.
- `boot_timeline.py` runs on Windows. Under WSL the host clock ran about 6 %
  slow against both the board and an HTTP `Date` header, with either
  clocksource; Windows matched the board.
- The host-side time to `login:` is not used. Starting the getty sends
  terminal queries (`ESC[6n`, XTGETTCAP) to the console, which nothing on the
  host answers, and the login banner follows about 2 s later; the result
  varies by more than 1 s between boots. That the 2 s is a reply timeout has
  not been checked in the systemd source.
- The two parts are not added into one boot time. They were measured on
  different builds (the Linux part on 2026-10-15, the firmware part on
  2026-10-16) with different clocks, and U-Boot prints `Starting kernel` about
  0.3 s before the kernel's clock starts at 0, a gap estimated from a fit, not
  measured.
- Kernel configuration: the layer's `debug.cfg` (dynamic debug, ftrace) is on.
  These numbers are not comparable with the Sprint 5 baseline.
- Every figure is the median of three boots of the same image.

### Results

Firmware, SPL banner → `Starting kernel` (s):

| Stage | Before | After |
|---|---|---|
| SPL → `Hit any key to stop autoboot` | 1.63 | 1.68 |
| autoboot countdown | 4.92 | 0.92 |
| `bootflow scan` → `Starting kernel` | 2.56 | 0.58 |
| **Total** | **9.12** | **3.18** |

Linux, kernel start → (s):

| Configuration | serial getty | `crng init done` |
|---|---|---|
| Before | 10.43 | 8.93 |
| Wi-Fi driver left out, `caam_jr` loaded early | 10.20 | 7.20 |

`Run /sbin/init` came at 4.34–4.39 s in all six boots, before and after.

### What changed

| Change | Commit | Effect |
|---|---|---|
| Leave the NXP Wi-Fi driver (moal) and its firmware out of the image; load `caam_jr` from `modules-load.d` | `<hash>` `conf/machine/yongchun.conf` | `crng init done` 1.7 s earlier in every boot. Serial getty 0.23 s earlier, which is inside the 0.3 s spread between boots |
| U-Boot: autoboot delay 5 s → 1 s; scan the SD card first; `bootmeths=extlinux` | `<hash>` (ADR-0008) | 5.94 s off the firmware part |

**Wi-Fi and the CRNG.** The board is headless and uses Ethernet. With moal,
`systemd-modules-load` took about 1.5 s. Without moal, `crng init done` came
only at 10.4 s, and dbus and avahi became active right after it, later than
with moal. The CAAM hardware RNG (`rng-caam`) registered only at 11.4 s,
because udev loaded `caam_jr` late; loaded from `modules-load.d`, it registers
at about 7.3 s and `crng init done` follows. Loading `caam_jr` early alone left
the getty where it was. What these show is timing, not mechanism: that moal's
interrupts fed the CRNG, that dbus and avahi blocked on it, and how much
`rng-caam` contributes have not been traced in the kernel or systemd source.

**U-Boot.** The countdown was this layer's own setting (5 s, raised from NXP's
2 s for a reason that was never measured). `bootflow scan` ran the global
`efi_mgr` bootmeth and brought up USB, finding nothing on either, before
reaching the SD card. See ADR-0008.

### Where the time goes now

Each row from its own measurement (see above); they are not a single boot.

| Part | s |
|---|---|
| SPL → `Starting kernel` (host) | 3.2 |
| `Starting kernel` → kernel clock 0 (estimated) | about 0.3 |
| kernel start → `Run /sbin/init` (dmesg) | 4.4 |
| `/sbin/init` → serial getty (board) | 5.8 |

Not looked at yet:

- The kernel's 4.4 s. `initcall_debug` has not been run.
- The 2 s of unanswered terminal queries before the serial login banner.
- `debug.cfg`: what a production kernel config would save.

### Reproducing

```
# Firmware part, from Windows (the board must accept ssh without a prompt)
uv run --no-project --with pyserial python tests\boot_timeline.py --port COM8 --reboot root@<board> --out boot.log

# Linux part, on the board after it boots
systemctl show -p ActiveEnterTimestampMonotonic --value serial-getty@ttymxc1.service
dmesg | grep 'crng init done'
```

### References

- ADR-0008: U-Boot boot path
- ADR-0007: why the board boots through extlinux
- bsp-scratch `sources/build-logs/imx8mp/2026-10-15-boot-time-crng-moal-caam.md`
- bsp-scratch `sources/build-logs/imx8mp/2026-10-16-boot-time-uboot-timeline.md`
