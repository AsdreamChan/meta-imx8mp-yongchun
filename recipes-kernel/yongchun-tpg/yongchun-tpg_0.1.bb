SUMMARY = "Test pattern generator V4L2 capture driver"
DESCRIPTION = "An out-of-tree V4L2 capture driver: one /dev/videoN per \
yongchun,tpg device tree node, producing YUYV colour bars at the frame \
interval the node gives, paced by an hrtimer."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://yongchun-tpg.c \
          "

S = "${UNPACKDIR}"
