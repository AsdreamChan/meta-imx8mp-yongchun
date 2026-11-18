SUMMARY = "FAT image of the boot partition, for SWUpdate"
DESCRIPTION = "The files wic puts on bootA (IMAGE_BOOT_FILES) in a FAT \
image the size of the boot partition, compressed. yongchun-update writes \
it raw to the boot partition of the slot that is not running (ADR-0011)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Written by Claude.

inherit deploy nopackages

# The content comes from this machine's IMAGE_BOOT_FILES.
PACKAGE_ARCH = "${MACHINE_ARCH}"

DEPENDS = "dosfstools-native mtools-native"

# Nothing is fetched or unpacked.
S = "${UNPACKDIR}"
B = "${WORKDIR}/build"

# The same size as bootA and bootB in yongchun-ab.wks.in (256 MiB), in KiB
# as mkfs.vfat -C takes it. The empty space compresses away.
BOOTFS_SIZE_KB = "262144"

BOOTFS_NAME = "yongchun-bootfs${IMAGE_MACHINE_SUFFIX}.vfat.gz"
IMAGE_MACHINE_SUFFIX ??= "-${MACHINE}"

do_configure[noexec] = "1"
do_compile[noexec] = "1"
do_install[noexec] = "1"

# Every file in IMAGE_BOOT_FILES has been deployed once the image's wic
# task has run, since wic copies the same list onto bootA. Waiting for it
# saves listing the recipes that deploy them here.
do_deploy[depends] += "yongchun-image:do_image_wic"
# += : deploy.bbclass already cleans (and so creates) DEPLOYDIR this way.
do_deploy[cleandirs] += "${B}/bootfs"

# IMAGE_BOOT_FILES entries are read as wic's bootimg-partition reads them,
# as far as Claude recalls it (not checked against wic's source): "src"
# lands at the same path (mcore-demos/x.bin stays in mcore-demos/),
# "src;dst" at dst; a src with a glob lands each match under dst (a
# directory), or at the top without one. Compare the result with bootA in
# the .wic before relying on it.
do_deploy() {
    export MTOOLS_SKIP_CHECK=1
    staging=${B}/bootfs

    # Single quotes: the entries contain ";", which the shell (and bitbake's
    # shell parser) would otherwise read as a command separator. set -f
    # keeps the unquoted expansion below from globbing in the current
    # directory; globs are expanded on purpose in the deploy directory.
    boot_files='${IMAGE_BOOT_FILES}'
    set -f
    for entry in $boot_files; do
        src="${entry%%;*}"
        dst=""
        if [ "$src" != "$entry" ]; then
            dst="${entry#*;}"
        fi
        case "$src" in
        *\**)
            set +f
            files=$(echo ${DEPLOY_DIR_IMAGE}/$src)
            set -f
            for f in $files; do
                install -D -m 0644 "$f" "$staging/${dst:+$dst/}$(basename "$f")"
            done
            ;;
        *)
            install -D -m 0644 "${DEPLOY_DIR_IMAGE}/$src" "$staging/${dst:-$src}"
            ;;
        esac
    done
    set +f

    rm -f ${B}/bootfs.vfat
    mkfs.vfat -n BOOT -C ${B}/bootfs.vfat ${BOOTFS_SIZE_KB}
    mcopy -i ${B}/bootfs.vfat -vsmpQ $staging/* ::/
    gzip -n -9 -c ${B}/bootfs.vfat > ${DEPLOYDIR}/${BOOTFS_NAME}
}

addtask deploy after do_prepare_recipe_sysroot before do_build
