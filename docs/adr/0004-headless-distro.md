# ADR-0004: A custom headless distro, decided before the layer is built

## Status

Accepted (2026-09-23)

## Context

The [vendor BSP baseline](../00-vendor-bsp-baseline.md) was built with NXP's
default, `fsl-imx-xwayland`: Weston, XWayland, Qt6 and the GPU stack.

Nothing in weeks 5 through 8 needs any of it. The custom machine, U-Boot,
kernel, device tree, out-of-tree driver, OTA and secure boot work is all
headless. Worse, week 8 is boot-time optimisation, and optimising an image
full of software that will never ship measures the wrong thing. The same
mistake was already made once on the Raspberry Pi tree, where the test image's
staging multimedia drivers dominated the userspace boot window.

**The timing is forced by cost, not preference.** Changing `DISTRO_FEATURES`
invalidates shared state. On the Scarthgap tree that meant a 0% hit rate and
139 minutes — approximately a full rebuild. Paying that once at the start of
week 5 benefits weeks 5 through 8; paying it mid-week benefits less, and
deferring it to week 8 means paying it there instead.

So the distro has to be settled before the layer gets any content.

### NXP's three distros, and what `-minimal` means

`meta-imx-sdk` provides `fsl-imx-fb`, `fsl-imx-wayland` and
`fsl-imx-xwayland`, each with a `-minimal` variant. The entire difference
between a distro and its `-minimal` counterpart is one line:

```
require conf/distro/include/fsl-imx-extended.inc
```

And that file adds exactly three features:

```
POKY_DEFAULT_DISTRO_FEATURES:append = " qt ${POKY_DEFAULT_DISTRO_FEATURES_IMX_APPEND}"
POKY_DEFAULT_DISTRO_FEATURES_IMX_APPEND ?= "jailhouse xen"
```

`-minimal` is therefore not "no graphics" — it keeps Weston, and adds RDP.
It means "no Qt, no jailhouse, no Xen".

## Decision

**A custom distro, `yongchun`, defined in the layer, based on
`fsl-imx-xwayland-minimal`.**

```
require conf/distro/fsl-imx-xwayland-minimal.conf

DISTRO = "yongchun"
DISTRO_NAME = "yongchun BSP distro"
DISTRO_VERSION = "1.0"

DISTRO_FEATURES:append = " jailhouse"
DISTRO_FEATURES:remove = "wayland x11 virtualization"
```

### In the layer, not in `local.conf`

Three lines in `local.conf` would have the same effect on this machine. They
would not travel: this layer is meant to be cloned, and a `local.conf` setting
is something the next person has to reproduce by hand from documentation. A
distro configuration in `conf/distro/` comes with the layer.

### Qt and the display stack go together

Keeping Qt while removing `wayland` and `x11` was considered and rejected. Qt
needs a display backend; without Wayland or X11 it falls back to eglfs or
linuxfb, which is not a combination NXP ships or tests. That gets the cost of
Qt without an environment that can run it.

NXP offers three tested distros. Assembling a fourth from parts is where
vendor-untested edges live.

### Jailhouse is kept, for a different reason than expected

The initial reasoning was "add back what `-minimal` removed". That turned out
to be the wrong frame. `imx8mp-evk.inc` already declares it at the machine
level:

```
MACHINE_FEATURES:append:use-nxp-bsp = " optee jailhouse mrvl8997 ..."
```

So board support is not what the distro feature controls — the two sides pair
up the way `wifi` does, hardware support against whether the software is
installed. The distro feature governs the userspace tooling
(`/usr/sbin/jailhouse`, the kernel module, `pyjailhouse`).

It is kept because it is cheap, because NXP ships a device tree for it
(`imx8mp-jailhouse-inmate.dtb`), and because adding it later would cost
another rebuild. Type-1 hypervisors and freedom-from-interference are adjacent
to the automotive work this portfolio is aimed at.

Xen is not kept. It is oriented toward server virtualisation.

### `virtualization` is removed

It brings in `containerd`, which cost 3.161 s of boot time in the baseline —
the most expensive `.service` unit measured. A headless BSP has no use for a
container runtime.

Jailhouse does not depend on it: `jailhouse-imx` comes from `meta-imx`, and
`meta-virtualization` only lists jailhouse on its roadmap as unimplemented.

## Results

Two images, because the distro change and the image change were made
separately. `imx-image-core` built under the new distro isolates what the
distro did; `yongchun-image` adds the image recipe's own change.

| | `fsl-imx-xwayland` | `yongchun` distro | `yongchun-image` |
|---|---|---|---|
| Packages | 1615 | 1528 (−87) | **1519** (−96) |
| Image (`.wic.zst`) | 363 MB | 326 MB (−37) | **268 MB** (−95) |
| Weston | present | gone | gone |
| Qt6 | present | gone | gone |
| Jailhouse | present | present | **present** |
| `containerd` | present | present | **gone** |

`wayland` remains as a library at 1.24.0, pulled in as a dependency; the
compositor is gone.

Builds: 98 m 14 s for the distro change, 6 m 27 s for the image change.

### On the board

```
Welcome to yongchun BSP distro 1.0 (wrynose)!

Startup finished in 4.457s (kernel) + 7.443s (userspace) = 11.901s
multi-user.target reached after 7.430s in userspace.

# systemctl get-default
multi-user.target
# systemctl status weston
Unit weston.service could not be found.
# ls -l /usr/sbin/jailhouse
-rwxr-xr-x 1 root root 68104 Apr  5  2011 /usr/sbin/jailhouse
```

| | baseline | `yongchun-image` |
|---|---|---|
| Kernel | 4.783 s | 4.457 s |
| Userspace | 9.441 s | **7.443 s** |
| Total | 14.224 s | **11.901 s** |
| Default target | `graphical.target` | **`multi-user.target`** |
| Weston | `[FAILED]` every boot | not installed |

**The default target changed without being asked to.** Removing `wayland` from
`DISTRO_FEATURES` means the image recipe's
`contains('DISTRO_FEATURES', 'wayland', 'weston', '', d)` no longer expands,
weston is never installed, and systemd falls back to `multi-user.target`. One
feature removal, three consequences.

### Removing a 3.161 s service saved 2.0 s

`containerd` cost 3.161 s in the baseline. Userspace dropped by 2.0 s, not
3.2 s, and the blame list explains why:

| | baseline | now |
|---|---|---|
| `dev-mmcblk1p2.device` | 3.494 s | 3.579 s |
| `containerd.service` | 3.161 s | — |
| `ldconfig.service` | not in the top 20 | **1.603 s** |
| `systemd-udev-trigger` | 1.396 s | 1.446 s |

`ldconfig` was there all along, overlapped with something that is now gone.
**Removing a service does not save the time that service took**, because the
parallel structure reorganises around the gap.

Worth carrying into week 8: measure after every change rather than adding up
what each one is supposed to save.

`dev-mmcblk1p2.device` barely moved, which is the expected behaviour for a
unit that is waiting rather than working.

## Consequences

### Accepted

- **`containerd` survived the change.** `DISTRO_FEATURES` governs what *can*
  be built; `IMAGE_INSTALL` governs what *is* installed. Removing
  `virtualization` did not remove a package the image recipe asks for
  directly. Which recipe pulls it in has not been determined yet — the
  dependency graph query to answer it was malformed.

  This is the right thing to fix in the custom image recipe in week 5, not by
  another distro change. Image-level choices belong at the image level.

- **No display.** Weston is not installed and the default target is
  `multi-user.target`; nothing graphical can be demonstrated on this image.
  That is the intent, but it closes off casually showing the board doing
  something visible.

### Gained

- Week 8 will measure boot time for software that would actually ship.
- The distro travels with the layer.
- Qt is gone, which removes one of the five recipes that were compiling
  simultaneously when the first build ran out of memory.

### Unexpected — and it contradicts an earlier finding

Shared state hit 78%, not 0%:

```
Sstate summary: Wanted 2876 Local 2268 Mirrors 0 Missed 608 Current 2263 (78% match, 88% complete)
Removing 180 stale sstate objects for arch armv8a
Removing 116 stale sstate objects for arch imx8mp_lpddr4_frdm
Removing   8 stale sstate objects for arch allarch
Removing   6 stale sstate objects for arch x86_64
```

The equivalent change on the Scarthgap tree gave `Wanted 2065 Local 2
Missed 2063` — a 0% hit rate, 1942 stale objects removed, and 139 minutes. That
measurement is where the claim "changing the distro costs approximately a full
rebuild" came from, and it appears in the Scarthgap notes and in week 3's
retrospective.

**This is a counterexample, and the reason is not known.** Several things
differ between the two cases and none has been isolated: the Scarthgap change
switched both the distro name and its base, the two trees differ in how they
assemble `DISTRO_FEATURES`, and their hash equivalence state is not the same.

Recorded as open rather than explained. The earlier claim needs qualifying,
not deleting — it was measured, and so is this.

### Revisit if

- The absence of a display becomes an obstacle. Switching back is another
  rebuild, so it would be a considered change rather than a convenience.

## References

- [`00-vendor-bsp-baseline.md`](../00-vendor-bsp-baseline.md) — what this is
  measured against, including the 3.161 s `containerd` figure
- `sources/build-logs/2026-09-15-distro-change-cost.md` — the Scarthgap
  measurement this contradicts
- `sources/build-logs/2026-09-20-imx-boot-composition.md` — what `-minimal`
  removes, and the `fsl-imx-extended.inc` contents
- `sources/build-logs/2026-09-23-yongchun-image-first-boot.log` — the boot
  quoted above

