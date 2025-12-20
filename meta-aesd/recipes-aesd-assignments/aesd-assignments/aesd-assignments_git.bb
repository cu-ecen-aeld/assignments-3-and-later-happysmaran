LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Ensure the source points to your repo and the server directory
SRC_URI = "git://git@github.com/cu-ecen-aeld/assignments-3-and-later-happysmaran.git;protocol=ssh;branch=master"
PV = "1.0+git${SRCPV}"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git/server"

# --- INIT SYSTEM CONFIGURATION ---
inherit update-rc.d
INITSCRIPT_PACKAGES = "${PN}"
INITSCRIPT_NAME:${PN} = "aesdsocket-start-stop.sh"
INITSCRIPT_PARAMS:${PN} = "defaults 99"

# --- PACKAGING CONFIGURATION ---
FILES:${PN} += "${bindir}/aesdsocket"
FILES:${PN} += "${sysconfdir}/init.d/aesdsocket-start-stop.sh"

# Add threading support for Assignment 6
TARGET_LDFLAGS += "-pthread -lrt"

do_configure () {
    :
}

do_compile () {
    oe_runmake
}

do_install () {
    # Install the binary to /usr/bin
    install -d ${D}${bindir}
    install -m 0755 ${S}/aesdsocket ${D}${bindir}/

    # Install the init script to /etc/init.d
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/aesdsocket-start-stop.sh ${D}${sysconfdir}/init.d/
}
