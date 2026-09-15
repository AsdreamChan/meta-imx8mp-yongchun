# Reference: Boot Baseline (Raspberry Pi 4B)

Baseline for the boot-time work in week 8. Nothing has been optimised.

- Image: `rpi-test-image`, `MACHINE=raspberrypi4-64`
- Distro: Poky 5.0.20 (scarthgap)
- Kernel: 6.6.63-v8, GCC 13.4.0, binutils 2.42
- Board: Raspberry Pi 4 Model B Rev 1.4, 8 GB
- Console: `ttyS0` at 115200 (mini-UART on GPIO 14/15)
- Init: **SysV init 3.04**, runlevel 5 — not systemd
- Firmware: 2025-04-30T13:33:39, hash `5560078dcc8591a00f57b9068d13e5544aeef3aa`

Raw serial console logs are kept in a private working repository. The relevant excerpts are quoted inline below; the filenames are recorded here so the figures remain traceable to a specific capture: `2026-08-26-rpi-boot-1-undervolted.log` (invalid, see below), `-2-adequate-psu.log`, and `-3-clean-baseline.log` (this one, including the shutdown sequence).

---

## The first baseline was invalid

The first boot reported:

```
[    7.775480] hwmon hwmon1: Undervoltage detected!
[   23.905221] hwmon hwmon1: Voltage normalised
```

A Raspberry Pi 4B needs 5 V at 3 A over USB-C. Below that the firmware
throttles the ARM cores, so every timing figure from that boot was inflated —
and not by a constant factor.

Measured on the same image and the same SD card, changing only the supply:

| Milestone | Undervolted | Adequate PSU | Δ |
|---|---|---|---|
| KVM init → trusted keyrings (interval) | 1.063 s | 0.422 s | −60% |
| Console `ttyS0` enabled | 1.716 | 0.957 | −44.2% |
| Root mounted (ro) | 3.886 | 3.027 | −22.1% |
| `Run /sbin/init` | 3.919 | 3.059 | −21.9% |
| Root remounted r/w | 12.586 | 10.458 | −16.9% |
| Last kernel message | 14.005 | 11.499 | −17.9% |

**Validate the measurement environment before recording a baseline.** An
optimisation compared against an invalid baseline measures the wrong thing.

---

## Timeline

Kernel timestamps in seconds. GPU firmware and bootloader stages run before
the kernel and are not visible here, so real power-on-to-login is higher.

| Time | Event |
|---|---|
| 0.000 | Kernel entry |
| 0.004 | 4 CPUs online |
| 0.065 | KVM hyp mode initialised |
| 0.487 | 0.42 s gap ends — trusted keyrings |
| 0.602 | PCIe link up, 5.0 GT/s x1 |
| 0.955 | `console [ttyS0] enabled` |
| 2.514 | 1.56 s gap ends — vc4-drm bound |
| 2.776 | `Waiting for root device /dev/mmcblk0p2` |
| 2.868 | SD card detected, DDR50, 28.9 GiB |
| 2.940 | Root filesystem mounted read-only |
| 2.973 | `Run /sbin/init as init process` |
| 3.491 | udev starts |
| 3.7–4.4 | Staging drivers and V4L2 device registration |
| 4.434 | brcmfmac firmware load, BCM4345/6 |
| 4.500–5.217 | Bluetooth BCM4345C0 patch load |
| 10.418 | Root remounted read-write |
| 11.484 | Last kernel message (Bluetooth RFCOMM) |
| ~11.5 | Login prompt |

**Kernel to login prompt: roughly 11.5 seconds.**

### Where the time goes

| Interval | Duration | Character |
|---|---|---|
| Root mount → remount r/w | **7.48 s** | Userspace: udev, staging modules, wireless and Bluetooth firmware |
| Console → vc4-drm bound | **1.56 s** | Fixed delay, see below |
| KVM → trusted keyrings | 0.42 s | Unattributed |

The userspace window dominates and is the obvious first target.

### The 1.56 s gap is a wait, not work

Measured across three boots:

| Boot | console → vc4-drm |
|---|---|
| Undervolted | 1.557 s |
| Adequate PSU | 1.558 s |
| Adequate PSU, clean filesystem | 1.559 s |

2 ms of variation across two different CPU clock states, while every
surrounding figure moved by 17–60% between the first and second boots.

CPU throttling cannot affect it, which means nothing is computing during it.
It is a fixed timeout — most likely a firmware round-trip or display
detection.

The distinction matters for week 8: throttling slows *computation*, not
*waiting*. Intervals that did not respond to the power fix are candidates for
removal or deferral, not for speeding up.

---

## Findings

### Init system is SysV, not systemd

Poky's default is `sysvinit`. Any analysis relying on `systemd-analyze` does
not apply here. The alternatives are `initcall_debug` on the kernel command
line for the kernel phase, `grabserial` for wall-clock timestamps including
pre-kernel stages, and `bootchart` for userspace.

Switching to systemd via `DISTRO_FEATURES` is possible, but that changes what
is being measured rather than measuring it — a decision record, not
optimisation work.

### Console handover

```
[    0.957168] printk: console [ttyS0] disabled
[    0.957377] fe215040.serial: ttyS0 at MMIO 0xfe215040 (irq = 38, base_baud = 62500000) is a 16550
[    0.957403] printk: console [ttyS0] enabled
```

Everything before 0.957 is replayed from the kernel ring buffer once the real
console driver registers, not printed live. `console=ttyS0,115200` on the
command line is what makes this work.

`ttyS0` is the mini-UART. The PL011 (`ttyAMA1`) is bound to the Bluetooth
controller — the standard Pi 4 arrangement, and the reason mini-UART baud rate
tracks the core clock unless `dtoverlay=disable-bt` is used.

### Staging drivers

Seven modules report `module is from the staging directory, the quality is
unknown, you have been warned`.

`rpi-test-image` pulls in the full multimedia stack: V4L2 decode, encode, ISP,
image_fx, encode_image, plus `rpivid` HEVC. Twelve `/dev/video*` nodes are
registered. For a headless target nearly all of this is removable — a win for
both boot time and image size.

### Filesystem state after a clean shutdown

A `poweroff` remounts root read-only before cutting power:

```
[   29.984922] EXT4-fs (mmcblk0p2): re-mounted 26019d63-... ro. Quota mode: none.
[   30.093536] kvm: exiting hardware virtualization
[   30.098239] reboot: Power down
```

The following boot no longer reports `recovery required`; only a routine
`orphan cleanup on readonly fs` remains. Skipping recovery saves roughly 90 ms:

| | Recovery needed | Clean |
|---|---|---|
| Root mounted ro | 3.027 | 2.940 |
| `Run /sbin/init` | 3.059 | 2.973 |
| Last kernel message | 11.499 | 11.484 |

**The FAT boot partition is still reported dirty**, even after a clean
shutdown:

```
FAT-fs (mmcblk0p1): Volume was not properly unmounted. Some data may be corrupt.
```

The shutdown sequence shows no unmount of `mmcblk0p1`. The dirty flag was
likely set when the image was written and has never been cleared, since
nothing mounts the partition read-write and unmounts it cleanly. Not yet
investigated; `fsck.vfat` on the host, or a single clean rw mount cycle,
should settle it.

### Shutdown takes about 6.7 s

The wall clock printed after remount (`12:39:39` at kernel time 10.418) and
the shutdown broadcast (`12:39:52`) place the `poweroff` at roughly kernel
time 23.4 s. Power down occurs at 30.098 s.

Most of that is SysV scripts stopping services that a headless target does not
need — avahi, bluetoothd, dbus, connman, rpcbind, NFC, telephony. The same
services also account for much of the 7.4 s userspace window during boot.

### Cosmetic, not investigated

```
udevd[173]: specified group 'i2c' unknown
udevd[173]: specified group 'spi' unknown
udevd[173]: specified group 'gpio' unknown
ip: SIOCGIFFLAGS: No such device
vc4-drm gpu: [drm] Cannot find any crtc or sizes
alsa-lib ... failed to import hw:0 use case configuration -2
Fri Mar  9 12:34:56 UTC 2018
```

The udev groups are referenced by rules but never created. The `ip` failure
and the DRM message are expected with no cable and no display attached. The
ALSA errors follow from the HDMI audio devices having no UCM configuration.
The date reflects the absence of an RTC — the Pi has none, and no network time
source was reachable.

---

## Notes for week 8

- Baseline is now clean and confirmed. Kernel to login: **11.484 s**;
  shutdown: **~6.7 s**.
- Clear the FAT dirty flag so `mmcblk0p1` stops reporting a bad unmount.
- Remove `debug-tweaks` and re-measure. Empty root password and permissive SSH
  are development conveniences that must not ship.
- Capture wall-clock time from power-on with `grabserial`, so GPU firmware and
  bootloader stages are included.
- Attribute the 0.42 s kernel gap with `initcall_debug`. It was 1.06 s
  undervolted, so it is computation, not a timeout.
- Identify what the 1.56 s fixed delay is waiting for. Three measurements
  agree to within 2 ms across different clock states, so it is a timeout —
  the fix is removal or deferral, not optimisation.
- The 7.48 s userspace window is the largest target. Dropping the staging
  multimedia stack and deferring Bluetooth firmware load are the first moves.
  The same services dominate shutdown, so cuts there pay twice.