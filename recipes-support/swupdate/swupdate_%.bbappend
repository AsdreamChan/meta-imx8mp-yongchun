# SWUpdate for A/B updates (ADR-0011): the daemon installs into the slot
# that is not running, and only a .swu signed with the build host's key.
#
# Written by Claude.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += " \
    file://yongchun.cfg \
    file://08-yongchun-key \
    file://09-yongchun-slot \
"

# The public half of the key that signs the .swu (SWUPDATE_PRIVATE_KEY in
# yongchun-update). Both are files on the build host, outside this
# repository; set their paths in kas/local.yml (kas/local.yml.example).
SWUPDATE_PUBLIC_KEY ??= ""
# Rebuild when the key file changes, not only its path.
do_install[file-checksums] += "${@d.getVar('SWUPDATE_PUBLIC_KEY') + ':True' if d.getVar('SWUPDATE_PUBLIC_KEY') else ''}"

do_install:append() {
    install -m 0644 ${UNPACKDIR}/08-yongchun-key ${D}${libdir}/swupdate/conf.d/
    install -m 0644 ${UNPACKDIR}/09-yongchun-slot ${D}${libdir}/swupdate/conf.d/
    if [ -z "${SWUPDATE_PUBLIC_KEY}" ]; then
        bbfatal "SWUPDATE_PUBLIC_KEY is not set: see kas/local.yml.example"
    fi
    install -m 0644 ${SWUPDATE_PUBLIC_KEY} ${D}${sysconfdir}/swu_public.pem
    # Board name and revision (machine conf), checked against
    # sw-description's hardware-compatibility.
    echo "${MACHINE} ${SWUPDATE_HW_REVISION}" > ${D}${sysconfdir}/hwrevision
}

FILES:${PN} += "${sysconfdir}/hwrevision ${sysconfdir}/swu_public.pem"

# /etc/hwrevision and the slot device names are specific to this machine.
PACKAGE_ARCH = "${MACHINE_ARCH}"
