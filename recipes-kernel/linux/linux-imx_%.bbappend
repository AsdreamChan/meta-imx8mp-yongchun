FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# CAN ISO-TP as a module, for the UDS work in week 9. can-isotp appears in
# none of the 27 layers, so it has to come from kernel configuration.
#
# A fragment rather than a replacement defconfig: KBUILD_DEFCONFIG points at
# imx_v8_defconfig inside the kernel tree, which NXP maintains. Fragments
# layer on top of it and keep working across vendor kernel updates.
SRC_URI += "file://can-isotp.cfg"
