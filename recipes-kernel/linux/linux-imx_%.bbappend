FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# CAN ISO-TP as a module, for the UDS work in week 9. can-isotp appears in
# none of the 27 layers, so it has to come from kernel configuration.
#
# A fragment rather than a replacement defconfig: KBUILD_DEFCONFIG points at
# imx_v8_defconfig inside the kernel tree, which NXP maintains. Fragments
# layer on top of it and keep working across vendor kernel updates.
SRC_URI += "file://can-isotp.cfg"

# Carried as a patch series against the vendor tree rather than a fork. The
# kernel work tree keeps a vendor branch pinned at lf-6.18.20-2.0.0 and a
# yongchun branch on top; format-patch regenerates this file after a rebase.
SRC_URI += "file://0001-arm64-dts-imx8mp-frdm-drive-the-status-LED-from-the-.patch"
