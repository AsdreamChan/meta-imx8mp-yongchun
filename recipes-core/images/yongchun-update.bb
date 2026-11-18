SUMMARY = "SWUpdate image (.swu) for the yongchun A/B slots"
DESCRIPTION = "Boot partition and root file system of yongchun-image for \
the slot that is not running, and the U-Boot variables that boot it once \
(ADR-0011). Not signed yet."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Written by Claude.

SRC_URI = "file://sw-description"

inherit swupdate

IMAGE_DEPENDS = "yongchun-image yongchun-bootfs"
SWUPDATE_IMAGES = "yongchun-image"
SWUPDATE_IMAGES_FSTYPES[yongchun-image] = ".rootfs.ext4.gz"
# yongchun-bootfs deploys a complete file name, which SWUPDATE_IMAGES
# takes as it is.
SWUPDATE_IMAGES += "${SWU_BOOTFS}"

# The images' names in the .swu, for sw-description, which also uses
# SWUPDATE_HW_REVISION from the machine conf.
SWU_ROOTFS = "yongchun-image${IMAGE_MACHINE_SUFFIX}.rootfs.ext4.gz"
SWU_BOOTFS = "yongchun-bootfs${IMAGE_MACHINE_SUFFIX}.vfat.gz"
