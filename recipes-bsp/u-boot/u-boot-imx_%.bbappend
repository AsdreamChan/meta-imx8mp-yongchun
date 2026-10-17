FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://0001-configs-imx8mp_frdm-shorten-the-autoboot-delay-to-on.patch"
SRC_URI += "file://0002-imx8mp_frdm-boot-the-yongchun-tpg-composite-device-t.patch"
SRC_URI += "file://0003-imx8mp_frdm-define-fdtoverlay_addr_r-for-extlinux-ov.patch"
SRC_URI += "file://0004-imx8mp_frdm-scan-the-SD-card-first-and-boot-only-thr.patch"

