SUMMARY = "Minimal IIO driver for the ROHM BH1750 light sensor"
DESCRIPTION = "An out-of-tree I2C/IIO driver: one-shot high-resolution \
readings exposed as in_illuminance_raw and in_illuminance_scale, bound to \
yongchun,bh1750 device tree nodes."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://yongchun-bh1750.c \
          "

S = "${UNPACKDIR}"

