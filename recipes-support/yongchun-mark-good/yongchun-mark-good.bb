SUMMARY = "Mark the running A/B slot good once it has booted"
DESCRIPTION = "A systemd service that clears upgrade_available and \
bootcount late in boot, so U-Boot keeps the slot instead of rolling back \
(ADR-0011)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Written by Claude.

SRC_URI = " \
    file://yongchun-mark-good \
    file://yongchun-mark-good.service \
"

S = "${UNPACKDIR}"

inherit allarch systemd

SYSTEMD_SERVICE:${PN} = "yongchun-mark-good.service"

do_install() {
    install -D -m 0755 ${UNPACKDIR}/yongchun-mark-good ${D}${sbindir}/yongchun-mark-good
    install -D -m 0644 ${UNPACKDIR}/yongchun-mark-good.service \
        ${D}${systemd_system_unitdir}/yongchun-mark-good.service
}

RDEPENDS:${PN} = "libubootenv-bin"
