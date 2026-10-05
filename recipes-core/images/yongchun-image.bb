SUMMARY = "Headless BSP image for the FRDM-IMX8MPLUS"

require recipes-fsl/images/imx-image-core.bb

# Docker pulls containerd in as a runtime dependency; containerd cost 3.161 s
# of boot time in the baseline and a headless BSP has no use for either.
# Overriding the indirection is cleaner than removing the package name, which
# the package manager would reinstall to satisfy docker.
DOCKER:mx8-nxp-bsp = ""

# v4l2-ctl and media-ctl for the sprint 7 V4L2 driver work. media-ctl
# is a separate package of v4l-utils, so both are listed. vivid already
# arrives through kernel-modules in MACHINE_EXTRA_RRECOMMENDS, but a
# recommendation can be dropped when the image is trimmed; listing it
# here makes it a hard dependency.
IMAGE_INSTALL += "v4l-utils media-ctl kernel-module-vivid"

export IMAGE_BASENAME = "yongchun-image"
