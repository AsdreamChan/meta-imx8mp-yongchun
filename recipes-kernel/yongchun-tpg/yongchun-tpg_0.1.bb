SUMMARY = "Test pattern generator for the sprint 7 V4L2 driver work"
DESCRIPTION = "An out-of-tree platform driver that binds to yongchun,tpg \
device tree nodes. It reads each node's frame interval; the V4L2 capture \
side follows in a later step."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://yongchun-tpg.c \
          "

S = "${UNPACKDIR}"

