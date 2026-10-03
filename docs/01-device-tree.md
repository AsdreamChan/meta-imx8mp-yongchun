# Device Tree on the FRDM-IMX8MPLUS

What the board's device tree is made of, how four changes were added to it,
and how each was checked against the hardware rather than against the source.

- Kernel: NXP `linux-imx`, tag `lf-6.18.20-2.0.0`
- Board: FRDM-IMX8MPLUS, `imx8mp-frdm.dts`
- Changes, carried as patches in `recipes-kernel/linux/linux-imx/`:

| Patch | Change |
|---|---|
| 0001 | Status LED driven by the heartbeat trigger |
| 0002 | HD44780 16×2 LCD behind a PCF8574 I/O expander on I2C3 |
| 0003 | The LCD moved out of the board file into an overlay |
| 0004 | An external LED on GPIO1_IO07, with its own pinctrl group |

The test for this sprint: read a pin's entry in the dts and say what function
the pin has and how it is pulled, then confirm it on the board. Section 3 does
that for two pins, one configured deliberately wrong.

---

## 1. Where the board's tree comes from

```
imx8mp.dtsi                    SoC: every peripheral, mostly status = "disabled"
  │
  ├─ imx8mp-nxp-display.dtsi   /delete-node/ the display pipeline, redefine it
  ├─ imx8mp-nxp-capture.dtsi   /delete-node/ the camera pipeline, redefine it
  │
  └─ imx8mp-frdm.dts           board: enable, wire up, add devices, pinctrl
       │
       └─ imx8mp-frdm-lcd1602.dtso   overlay (patch 0003), applied at build or boot
```

Include order is override order: a later file changes what an earlier one
defined. There are three strengths of change, and they differ in how hard they
are to trace.

| Form | Effect | Used for |
|---|---|---|
| `&label { prop = <…>; };` | Changes the named properties, keeps the rest | Board files switching `status` |
| A child node inside `&label { … }` | Adds to the node | Devices on a bus |
| `/delete-node/ &label;` then a new definition | **Replaces the node entirely** | NXP's display and capture dtsi |

The third is the one that misleads. NXP's two dtsi files delete the SoC file's
ISI, ISP, MIPI CSI, LCDIF, HDMI and LVDS nodes and define their own, mostly
with different `compatible` strings, so a different driver binds. A node seen
in `imx8mp.dtsi` may simply not exist in the board's dtb. For display and
camera questions on this BSP, the NXP dtsi and NXP drivers are the reference,
and the source of truth is the compiled tree, not any one file:

```bash
dtc -I dtb -O dts -s imx8mp-frdm.dtb > imx8mp-frdm.dts.expanded
```

---

## 2. Adding a device: an LCD on I2C3

An HD44780 character LCD behind a PCF8574 I2C expander, wired to the I2C3
pins of the board's 10-pin I/O header (J27). Two nodes: the expander as a GPIO
controller on the bus, and the display as a platform device consuming eight of
its lines.

```dts
&i2c3 {
	pcf8574: gpio-expander@27 {
		compatible = "nxp,pcf8574";
		reg = <0x27>;
		gpio-controller;
		#gpio-cells = <2>;
	};
};

/ {
	display-controller {
		compatible = "hit,hd44780";
		display-height-chars = <2>;
		display-width-chars = <16>;
		data-gpios = <&pcf8574 4 GPIO_ACTIVE_HIGH>, <&pcf8574 5 GPIO_ACTIVE_HIGH>,
			     <&pcf8574 6 GPIO_ACTIVE_HIGH>, <&pcf8574 7 GPIO_ACTIVE_HIGH>;
		enable-gpios = <&pcf8574 2 GPIO_ACTIVE_HIGH>;
		rs-gpios = <&pcf8574 0 GPIO_ACTIVE_HIGH>;
		rw-gpios = <&pcf8574 1 GPIO_ACTIVE_HIGH>;
		backlight-gpios = <&pcf8574 3 GPIO_ACTIVE_HIGH>;
	};
};
```

Two things outside the tree decided whether it worked.

**The kernel configuration fragment needs the parent menu.** `CONFIG_HD44780`
sits inside the `AUXDISPLAY` menu; a fragment containing only the driver
symbol is dropped when the configuration is merged, silently.

```
CONFIG_GPIO_PCF857X=y
CONFIG_AUXDISPLAY=y
CONFIG_HD44780=m
```

**The bus is 1.8 V at the SoC.** The I2C2 and I2C3 pads sit in the 1.8 V I/O
domain; the board translates them to 3.3 V with a bidirectional level shifter
and 10 kΩ pull-ups before they reach the headers. A 3.3 V module is correct on
the header.

`i2cdetect` shows the description taking effect: the address answers either
way, but only with the node present does a driver hold it.

```
20: -- -- -- -- -- -- -- 27 -- …     no node: the device answers, nothing claims it
20: -- -- -- -- -- -- -- UU -- …     with the node: the pcf857x driver holds it
```

---

## 3. Pinctrl: reading a pin from the dts

On i.MX8M every pad has up to three IOMUXC registers: a **mux** register
selecting one of several functions (ALT0–ALT6), a **pad** register for pulls,
drive strength and slew, and for some module inputs a **daisy** register
selecting which pad feeds the input. A pinctrl entry in the dts sets all
three.

```dts
pinctrl_i2c3: i2c3grp {
	fsl,pins = <
		MX8MP_IOMUXC_I2C3_SCL__I2C3_SCL		0x400001c2
		MX8MP_IOMUXC_I2C3_SDA__I2C3_SDA		0x400001c2
	>;
};
```

The macro expands to five numbers; the dts supplies the sixth.

| | `I2C3_SCL__I2C3_SCL` | Meaning |
|---|---|---|
| 1 | `0x210` | Mux register offset from the IOMUXC base (`0x30330000`) |
| 2 | `0x470` | Pad register offset |
| 3 | `0x5B4` | Daisy register offset (`0x000` when there is none) |
| 4 | `0x0` | Mux value: ALT0, I2C3_SCL |
| 5 | `0x4` | Daisy value: take the input from the I2C3_SCL pad |
| 6 | `0x400001c2` | Pad configuration |

The pad value, decoded against the field layout in the reference manual
(IMX8MPRM Rev. 3, §8.2.4):

| Bit | Field | `0x1c2` | Meaning |
|---|---|---|---|
| 8 | PE | 1 | Pull enabled |
| 7 | HYS | 1 | Schmitt input |
| 6 | PUE | 1 | **Pull-up** |
| 5 | ODE | 0 | Push-pull at the pad |
| 4 | FSEL | 0 | Slow slew |
| 2–1 | DSE | `01` | X4 drive |

Note that DSE is not encoded in order of strength: `10` is X2 and `01` is X4.

Bit 30 is not a pad bit at all; the pad register's upper bits are reserved.
The i.MX pinctrl driver treats it as a request to set **SION** in the *mux*
register, which forces the pad's input path on, and strips it from the pad
value (`IMX_PAD_SION` in `drivers/pinctrl/freescale/pinctrl-imx.c`).
I2C needs that, because the controller reads back the line it is driving. The register on the board
confirms it: the mux reads `0x10`, ALT0 plus SION.

### Checking it on the board

Values the bootloader leaves, read in U-Boot before Linux touches pinctrl,
against the same registers after Linux has applied the tree:

| Register | Reset (RM) | U-Boot | Linux |
|---|---|---|---|
| I2C3_SCL mux `0x30330210` | `0x5` (ALT5, GPIO) | `0x5` | `0x10` |
| I2C3_SCL pad `0x30330470` | `0x106` | `0x106` | `0x1C2` |
| I2C3_SCL daisy `0x303305B4` | `0x0` (another pad) | `0x0` | `0x4` |

```
u-boot=> md.l 0x30330210 1
root@yongchun:~# /unit_tests/memtool -32 0x30330210 1
root@yongchun:~# grep I2C3 /sys/kernel/debug/pinctrl/30330000.pinctrl/pinconf-pins
```

Two observations follow. The pad comes out of reset as a GPIO with a pull-down
and its daisy pointing at a different pad, so every one of the three values
in the dts matters. And the reset values are listed only in the IOMUXC memory
map table; the register diagrams leave the Reset row blank.

### Wrong on purpose: I2C3_SCL as a GPIO

One change, the mux of one pin, pad configuration untouched:

```diff
-	MX8MP_IOMUXC_I2C3_SCL__I2C3_SCL		0x400001c2
+	MX8MP_IOMUXC_I2C3_SCL__GPIO5_IO18	0x400001c2
```

The bus stopped. Every device on it timed out, and everything depending on
those devices deferred:

```
SCL pad no longer connected to the I2C3 controller
  └─ i2c-2 transfers time out
       ├─ pcf857x 2-0027: probe failed, error -110
       │    └─ display-controller: deferred, supplier 2-0027 not ready
       └─ wm8962 2-001a: probe failed, error -110
            └─ sound-wm8962: deferred
```

Three lessons came out of it.

- **The root is a hard error; the deferrals are its victims.**
  `/sys/kernel/debug/devices_deferred` lists only the victims. The cause is
  upstream in `dmesg`, and its errno says what kind: `-110` (timeout) means the
  bus itself is not working. An earlier failure on another bus, where one device
  did not respond on a working bus, returned `-ENXIO` instead.
- **The damage was exactly one bus.** The audio codec on the same bus failed
  with the LCD; the camera chain on I2C2 was identical before and after, which
  served as the control.
- **`pinmux-pins` did not show it.** It reports which device claimed a pin
  through which group, and the I2C controller still owned the pin. The mux
  value is visible only in the register.

### Right, with a fresh pin: an LED on GPIO1_IO07

An LED with a series resistor on pin 16 of the 40-pin header, which is
GPIO1_IO07. The GPIO1 bank is supplied at 3.3 V, so the pad drives the LED
directly.

```dts
gpio-leds {
	compatible = "gpio-leds";
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_gpio_led>;
	…
	led-ext {
		label = "ext";
		gpios = <&gpio1 7 GPIO_ACTIVE_HIGH>;
		default-state = "off";
	};
};

pinctrl_gpio_led: gpioledgrp {
	fsl,pins = <
		MX8MP_IOMUXC_GPIO1_IO07__GPIO1_IO07	0x6
	>;
};
```

Read from the dts alone: mux ALT0 (GPIO), no daisy; pad `0x6` is pull
disabled, CMOS, push-pull, slow slew, DSE X6. The reset value `0x106` differs
only in PE: the configuration removes the reset pull-down.

The pin worked *before* the change, from user space, because its reset mux
already selects GPIO. Working is not the same as described. The before and
after:

| Check | No pinctrl entry | With 0004 |
|---|---|---|
| `pinmux-pins` | `(MUX UNCLAIMED) (GPIO UNCLAIMED)` | `gpio-leds` via `gpioledgrp` |
| `pinconf-pins` | `N/A` | `0x6` |
| `gpioinfo` line 7 | `input`, no consumer | `output consumer="ext"` |
| `gpioset -c gpiochip0 7=1` | LED on | `Device or resource busy` |
| Pad register `0x30330290` | `0x106` | `0x6` |

The `busy` is the point: once the tree describes the pin, the kernel owns it,
and the LED is driven through `/sys/class/leds/ext/brightness`.

---

## 4. Overlays

Patch 0003 moved the LCD out of the board file into
`imx8mp-frdm-lcd1602.dtso`. An overlay cannot see the base tree when it is
compiled, so references to it stay symbolic:

```dts
/dts-v1/;
/plugin/;

&i2c3 {
	#address-cells = <1>;
	#size-cells = <0>;
	pcf8574: gpio-expander@27 { … };
};

&{/} {
	display-controller { … };
};
```

- `&i2c3` compiles to a placeholder target and an entry in `__fixups__`, to be
  resolved against the base tree's `__symbols__`. The base must be built with
  symbols; here the kernel build added `-@` to the base dtb because it is the base
  of a composite target (inferred from the output, not from the makefiles).
- `&pcf8574` is internal to the overlay; its uses are listed in
  `__local_fixups__` and renumbered when the overlay is applied.
- `#address-cells` and `#size-cells` are repeated because the compiler cannot
  read them from the base.

The overlay can be applied at two points. The kernel Makefile can merge it at
build time into a composite dtb:

```make
imx8mp-frdm-lcd1602-dtbs := imx8mp-frdm.dtb imx8mp-frdm-lcd1602.dtbo
dtb-${CONFIG_ARCH_MXC} += imx8mp-frdm-lcd1602.dtb
```

or U-Boot can apply it at boot:

```
run loadimage
run loadfdt
fdt addr ${fdt_addr_r}
fdt resize 4096
fatload mmc ${mmcdev}:${mmcpart} 0x43400000 imx8mp-frdm-lcd1602.dtbo
fdt apply 0x43400000
run mmcargs
booti ${loadaddr} - ${fdt_addr_r}
```

The board's own `mmcboot` cannot be used for the second: it loads the device
tree itself and would overwrite the merged one.

Three boots, same image:

| Check | Base dtb | Overlay applied in U-Boot | Composite dtb |
|---|---|---|---|
| `/proc/device-tree/display-controller/compatible` | absent | `hit,hd44780` | `hit,hd44780` |
| `pcf857x 2-0027: probed` | — | yes | yes |
| `/dev/lcd` | — | yes | yes |
| Address 0x27 in `i2cdetect` | `27` | `UU` | `UU` |
| Phandle of the expander | — | `0x120` | `0x120` |

The two merges agree on everything compared — the same phandle numbers, and
the overlay's properties coming out in reverse order in both — as expected,
since both use libfdt's overlay code. The base boot is the control: the hardware answered at 0x27
throughout, so the difference is the description alone.

One limitation found on the way: the `.dtbo` was built without `-@`, so the overlay's own labels do not reach the merged tree, and a second
overlay could not refer to `&pcf8574`.

---

## 5. What the kernel actually receives

The dtb in the image is not quite the tree the kernel boots with. Comparing
the build output with `/sys/firmware/fdt` on the running board, both
decompiled with `dtc -s` so that order does not matter:

```bash
cp /sys/firmware/fdt /tmp/live.dtb                       # on the board
dtc -I dtb -O dts -s imx8mp-frdm.dtb > built.dts
dtc -I dtb -O dts -s live.dtb > live.dts
diff built.dts live.dts                                  # 37 lines
```

Everything in the difference was added by U-Boot:

| Where | Added | Kind |
|---|---|---|
| `/serial-number` | The SoC's unique ID | Identity of this board |
| `ethernet@30be0000`, `ethernet@30bf0000` | `local-mac-address` | Identity of this board |
| `/chosen` | `bootargs`, `u-boot,version`, `smbios3-entrypoint` | Boot-time parameters |
| `/memory` | The 32 MB at `0x56000000` cut out of the RAM ranges | Memory layout |
| `/reserved-memory` | `optee_core` (30 MB) and `optee_shm` (2 MB), `no-map` | Memory layout |
| `/firmware/optee` | The OP-TEE node | Secure world |
| CAAM `jr@3000` | `status = "disabled"` | Secure world |

None of these could be written into the dts: they differ per board or are
known only at boot. The practical consequence is where to look. When a node is
enabled in the source and Linux still does not use it — the third CAAM job
ring here — or when the RAM Linux reports does not match the dts, the answer is
in the live tree.

---

## 6. A graph that is only half a graph: the camera pipeline

The board's camera path shows the cost of section 1's replaced nodes. On this
board the NXP camera kit attaches to the CSI1 connector: an onsemi AP1302
image signal processor with an AR0144 sensor behind it, sending YUV over four
MIPI lanes to the SoC's CSI receiver, which feeds the ISI.

The first link is a standard OF graph. Each end names the other:

```dts
ap1302_0: ap1302_mipi_0@3c {                 /* on I2C2 */
	…
	ports {
		port@2 {
			reg = <2>;
			isp_out_0: endpoint {
				remote-endpoint = <&mipi_csi0_ep>;
				data-lanes = <1 2 3 4>;
				clock-lanes = <0>;
			};
		};
	};
};

&mipi_csi_0 {
	port {
		mipi_csi0_ep: endpoint {
			remote-endpoint = <&isp_out_0>;
			data-lanes = <4>;
			…
		};
	};
};
```

The second link is not a graph at all. NXP's capture dtsi, having deleted the
SoC file's CSI and ISI nodes, redefines the ISI without any ports. Its input is
chosen by a vendor property and its number by an alias:

```dts
aliases {
	isi0 = &isi_0;
	csi0 = &mipi_csi_0;
};

isi_0: isi@32e00000 {
	compatible = "nxp,imx8mp-isi", "nxp,imx8mn-isi";
	interface = <2 0 2>;
	…
};
```

`interface` appears in no binding document; it is read only by NXP's staging
media drivers (`drivers/staging/media/imx/imx8-isi-core.c` and
`imx8-media-dev.c`), as three integers whose meaning the code does not spell
out. Even the two `data-lanes` properties above disagree in form: a list of
lanes on the sensor side, a lane count on NXP's CSI side. For this half of the
pipeline the generic media bindings do not apply, and the reference is the
vendor driver.

### What the board builds without a sensor

Neither camera connector is populated. The dts expects an ADP5585 I/O expander
at I2C2 address 0x34 to switch the camera's power rails; it is not on the
board's schematic, so it presumably sits on the camera module itself, and it is
absent with it. The boot log shows each piece
probing on its own and the whole failing to form:

```
mxc-mipi-csi2-sam 32e40000.csi: lanes: 4, …          CSI receiver: probed
: mipi_csis_imx8mp_phy_reset, No remote pad found!   … with nothing upstream
mxc-isi_v1 32e00000.isi: mxc_isi.0 registered        ISI: probed
mx8-img-md: Registered mxc_isi.0.capture as /dev/video3
mx8-img-md: Unregistered all entities                media device: torn down
   (the last two lines repeat six times)
```

`devices_deferred` gives the reason in the usual upstream order: the missing
expander, then four regulators waiting for it, then the AP1302 waiting for a
regulator, then the media device. Each time deferred probing retried, the
media driver registered the capture node, found the sensor's subdevice absent,
and removed everything again. No `/dev/media0` exists, and the three
`/dev/video*` nodes that do exist are the VPU's encoder and decoder and the
ISI's memory-to-memory device, not a camera.

The lesson generalises: the CSI receiver and the ISI were both healthy, and
the pipeline still did not exist, because one endpoint of the graph had no
driver. Counting `/dev/video*` nodes says nothing about whether a camera came
up; their names and drivers do.

---

## Checks used

| Question | Command |
|---|---|
| What did the build produce? | `dtc -I dtb -O dts -s <dtb>`, grep for a `compatible` string, not a node name |
| Does an overlay have what it needs? | `grep -c '__symbols__ {'` on the base |
| Did a merge connect things? | Every reference to an added node carries that node's `phandle` |
| Which device owns a pin? | `/sys/kernel/debug/pinctrl/*/pinmux-pins` |
| What pad value is set? | `/sys/kernel/debug/pinctrl/30330000.pinctrl/pinconf-pins` (pins with a group only) |
| What mux value is set? | The register: `md.l` in U-Boot, `/unit_tests/memtool` in Linux |
| Why is a device missing? | The first hard error in `dmesg`; `devices_deferred` lists the consequences |
| What did the kernel receive? | `/sys/firmware/fdt`, decompiled and compared |
| What is each video node? | `cat /sys/class/video4linux/video*/name`, and the driver behind `device/` |
