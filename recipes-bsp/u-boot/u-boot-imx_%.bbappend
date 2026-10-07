FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://0001-configs-imx8mp_frdm-raise-the-autoboot-delay-to-five.patch"
SRC_URI += "file://0002-imx8mp_frdm-boot-the-yongchun-tpg-composite-device-t.patch"

