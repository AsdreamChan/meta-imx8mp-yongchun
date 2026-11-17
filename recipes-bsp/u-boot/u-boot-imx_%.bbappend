FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://0001-configs-imx8mp_frdm-shorten-the-autoboot-delay-to-on.patch"
SRC_URI += "file://0002-imx8mp_frdm-boot-the-yongchun-tpg-composite-device-t.patch"
SRC_URI += "file://0003-imx8mp_frdm-define-fdtoverlay_addr_r-for-extlinux-ov.patch"
SRC_URI += "file://0004-imx8mp_frdm-scan-the-SD-card-first-and-boot-only-thr.patch"

# A/B slots and the boot counter for OTA updates (ADR-0011). fw_env.config
# tells fw_printenv/fw_setenv (libubootenv) where the two copies of the
# environment are; u-boot.inc installs it into /etc when it is in SRC_URI.
SRC_URI += "file://0005-imx8mp_frdm-boot-the-A-B-slot-named-in-bootslot-with.patch"
SRC_URI += "file://fw_env.config"

# libubootenv falls back to /etc/u-boot-initial-env when neither copy of
# the environment is valid yet (a freshly flashed card).
UBOOT_INITIAL_ENV = "u-boot-initial-env"
