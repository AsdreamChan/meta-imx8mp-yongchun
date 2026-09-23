SUMMARY = "Headless BSP image for the FRDM-IMX8MPLUS"

require recipes-fsl/images/imx-image-core.bb

# Docker pulls containerd in as a runtime dependency; containerd cost 3.161 s
# of boot time in the baseline and a headless BSP has no use for either.
# Overriding the indirection is cleaner than removing the package name, which
# the package manager would reinstall to satisfy docker.
DOCKER:mx8-nxp-bsp = ""

export IMAGE_BASENAME = "yongchun-image"
