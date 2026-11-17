# mount.sh (udev-extraconf-automount) mounts every new partition under
# /run/media unless /etc/fstab names it or a line in
# /etc/udev/mount.ignorelist.d/ matches its device name (mount.sh:31-37).
# Without this, the slot that is not running is mounted read-write at boot.
#
# Written by Claude.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://sd-card.ignorelist"

do_install:append() {
    install -m 0644 ${UNPACKDIR}/sd-card.ignorelist \
        ${D}${sysconfdir}/udev/mount.ignorelist.d/sd-card.ignorelist
}

FILES:${PN}-automount += "${sysconfdir}/udev/mount.ignorelist.d/sd-card.ignorelist"
