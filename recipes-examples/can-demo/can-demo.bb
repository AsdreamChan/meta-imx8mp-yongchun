SUMMARY = "Minimal SocketCAN raw socket example"
DESCRIPTION = "Sends one classical CAN frame on an interface and prints \
every frame received on it. C version in ${PN}, Python version in \
${PN}-python."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "\
        file://can-demo.c \
        file://can-demo.py \
"

S = "${UNPACKDIR}"

do_compile() {
	${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o ${B}/can-demo ${S}/can-demo.c
}

do_install() {
	install -D -m 0755 ${B}/can-demo ${D}${bindir}/can-demo
        install -D -m 0755 ${S}/can-demo.py ${D}${bindir}/can-demo.py
}

PACKAGES =+ "${PN}-python"
FILES:${PN}-python = "${bindir}/can-demo.py"
RDEPENDS:${PN}-python = "python3-core python3-io"
