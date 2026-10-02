FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# CAN ISO-TP as a module, for the UDS work in week 9. can-isotp appears in
# none of the 27 layers, so it has to come from kernel configuration.
#
# A fragment rather than a replacement defconfig: KBUILD_DEFCONFIG points at
# imx_v8_defconfig inside the kernel tree, which NXP maintains. Fragments
# layer on top of it and keep working across vendor kernel updates.
SRC_URI += "file://can-isotp.cfg"

# The gs_usb driver, for a candleLight-firmware USB-CAN adapter acting as the
# far end of the bus. The board has one CAN controller (flexcan1, wired to a
# TJA1051T/3 transceiver on header J27), so the other end has to be external.
#
# The adapter plugs into the board's own USB host rather than the development
# machine's, which keeps both interfaces on the same system and avoids
# forwarding a USB device into WSL2.
#
# Separate from can-isotp.cfg deliberately: that one enables a protocol, this
# one enables a driver for a specific piece of hardware. They are likely to be
# removed at different times.
SRC_URI += "file://can-usb.cfg"

# Carried as a patch series against the vendor tree rather than a fork. The
# kernel work tree keeps a vendor branch pinned at lf-6.18.20-2.0.0 and a
# yongchun branch on top; format-patch regenerates this file after a rebase.
SRC_URI += "file://0001-arm64-dts-imx8mp-frdm-drive-the-status-LED-from-the-.patch"
SRC_URI += "file://0002-arm64-dts-imx8mp-frdm-add-an-HD44780-LCD-behind-a-PC.patch"
SRC_URI += "file://0003-arm64-dts-imx8mp-frdm-move-the-HD44780-LCD-to-an-ove.patch"

# HD44780 16x2 character LCD behind a PCF8574 I2C backpack, for the
# sprint 6 device tree work. AUXDISPLAY is only the menu gate and adds
# no code, but HD44780 is skipped without it. GPIO_PCF857X is already
# set by the vendor defconfig; it is listed so the dependency survives
# a defconfig change. HD44780_COMMON is selected, so it is not listed.
SRC_URI += "file://hd44780.cfg"
