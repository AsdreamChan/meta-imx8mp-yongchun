# SWUpdate for A/B updates (ADR-0011): the daemon installs into the slot
# that is not running.
#
# Written by Claude.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += " \
    file://yongchun.cfg \
    file://09-yongchun-slot \
"

do_install:append() {
    install -m 0644 ${UNPACKDIR}/09-yongchun-slot ${D}${libdir}/swupdate/conf.d/
    # Board name and revision (machine conf), checked against
    # sw-description's hardware-compatibility.
    echo "${MACHINE} ${SWUPDATE_HW_REVISION}" > ${D}${sysconfdir}/hwrevision
}

FILES:${PN} += "${sysconfdir}/hwrevision"

# /etc/hwrevision and the slot device names are specific to this machine.
PACKAGE_ARCH = "${MACHINE_ARCH}"
