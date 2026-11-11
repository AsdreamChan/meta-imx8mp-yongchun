SUMMARY = "Minimal UDS server over an ISO-TP socket"
DESCRIPTION = "Answers DiagnosticSessionControl (0x10) and \
ReadDataByIdentifier (0x22) on a CAN interface, using the kernel's \
can-isotp module for segmentation and flow control."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://uds-server.c"

S = "${UNPACKDIR}"

do_compile() {
	${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o ${B}/uds-server ${S}/uds-server.c
}

do_install() {
	install -D -m 0755 ${B}/uds-server ${D}${bindir}/uds-server
}

RRECOMMENDS:${PN} = "kernel-module-can-isotp"
