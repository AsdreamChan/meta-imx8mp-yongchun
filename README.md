# meta-imx8mp-yongchun

A from-scratch Yocto BSP layer for the **NXP i.MX8M Plus** (FRDM-IMX8MPLUS),
built to production practice rather than tutorial practice: custom machine and
distro, out-of-tree kernel driver, OTA updates, boot-time budget, and an
automotive CAN/UDS communication stack.

`yongchun` is the machine codename, after the Yongchun district in Taipei.

> **Status: in progress.** This layer is being built over a 12-week plan.
> The roadmap below marks what exists today and what does not. Nothing here
> is claimed before it is built.

---

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

---

## Roadmap

| Weeks | Scope | Status |
|---|---|---|
| 1–3 | Yocto fundamentals on Raspberry Pi 4B (learning platform) | 🔜 |
| 4 | Vendor BSP baseline on FRDM-IMX8MPLUS, boot flow analysis | ⬜ |
| 5 | Custom layer, machine, distro; U-Boot and kernel customisation | ⬜ |
| 6 | Device tree: I2C sensor, pinctrl/IOMUX, GPIO LED, overlays | ⬜ |
| 7 | Out-of-tree kernel driver (IIO), packaged as a recipe | ⬜ |
| 8 | Productisation: OTA (A/B), boot time, read-only rootfs | ⬜ |
| 9 | Automotive stack: SocketCAN, UDS server, CP↔Linux comparison | ⬜ |
| 10 | CI, reproducible builds (kas), documentation | ⬜ |
| 11 | Buffer | ⬜ |
| 12 | Wrap-up | ⬜ |

Weeks 1–3 happen on a Raspberry Pi 4B and live in a separate private
scratch repo; only the notes from that phase land here, under `docs/`.

---

## Hardware

| Board | Role |
|---|---|
| **NXP FRDM-IMX8MPLUS** | Primary target |
| Raspberry Pi 4B | Learning platform, weeks 1–3 |
| Orange Pi Zero (Allwinner H3) | Stretch goal: mainline-only bring-up, no vendor BSP |

---

## Quickstart

> Not yet available — this section will be filled in once the layer builds
> (target: week 5). It will use `kas` so the whole environment is
> reproducible with a single command.

---

## Layer structure

```
conf/                 layer, machine and distro configuration
recipes-bsp/          U-Boot, device tree, firmware notes
recipes-kernel/       kernel bbappend, defconfig fragments, out-of-tree driver
recipes-automotive/   SocketCAN tooling, UDS diagnostic server
recipes-core/         images and packagegroups
recipes-support/      SWUpdate / OTA
scripts/              environment setup, kas config, boot-time measurement
ci/                   layer checks
docs/                 documentation (see below)
```

---

## Documentation

`docs/` follows [Diátaxis](https://diataxis.fr): tutorials, how-to guides,
reference, and explanation are kept separate rather than blended.

- `docs/adr/` — Architecture Decision Records. Every non-trivial and
  hard-to-reverse choice gets one, including the trade-offs accepted.
- `docs/traceability/` — one requirement traced end to end
  (requirement → design decision → implementation → test), as a worked
  example of the rigour I'm used to from functional-safety work.
- `docs/03-automotive-stack.md` — a comparison of the AUTOSAR Classic
  communication stack against its Linux counterparts. Probably the most
  useful document here for anyone crossing the same boundary.

---

## Licensing

**No vendor binaries are redistributed in this repository.**

NXP firmware blobs — DDR training firmware (`lpddr4_pmu_train_*.bin`), HDMI
firmware, and similar — are covered by NXP's licence terms. They are fetched
at build time through `SRC_URI` with the appropriate `LIC_FILES_CHKSUM`, never
committed here. `.gitignore` blocks `*.bin`, `*.fw` and `*.elf` by default so
that a stray `git add .` cannot leak them.

Yocto distinguishes two mechanisms, and this layer uses both deliberately:

- `LICENSE` and `LIC_FILES_CHKSUM` **record** what a recipe ships and verify
  the licence text has not changed upstream.
- `LICENSE_FLAGS` **blocks**. A recipe carrying a restricted flag will not
  build until the flag is listed in `LICENSE_FLAGS_ACCEPTED`, so restricted
  components cannot end up in an image by accident.

Any restricted component this layer depends on is listed explicitly, with the
reason, rather than being enabled through a blanket accept.

The layer's own source is released under the licence in `LICENSE`.

---

## Contact

Feedback and corrections are welcome — open an issue.
