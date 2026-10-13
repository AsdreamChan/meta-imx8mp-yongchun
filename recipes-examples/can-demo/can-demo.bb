SUMMARY = "Minimal SocketCAN raw socket example"
DESCRIPTION = "Sends one classical CAN frame on an interface and prints \
every frame received on it."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://can-demo.c"

S = "${UNPACKDIR}"

do_compile() {
	${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o ${B}/can-demo ${S}/can-demo.c
}

do_install() {
	install -D -m 0755 ${B}/can-demo ${D}${bindir}/can-demo
}

