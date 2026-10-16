SUMMARY = "Device tree overlays for modules plugged into the yongchun board"
DESCRIPTION = "Overlays that U-Boot applies at boot through extlinux.conf \
(ADR-0007). They are built here, outside the kernel tree, so that adding \
or changing a module's overlay does not rebuild the kernel."

COMPATIBLE_MACHINE = "yongchun"

inherit devicetree

SRC_URI = "file://imx8mp-frdm-bmp280.dtso \
           file://imx8mp-frdm-bh1750.dtso \
"
