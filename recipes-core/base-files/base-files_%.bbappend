do_install:append() {
    echo "EdgeOps Embedded Device Management & Diagnostics Unit (qemuarm64)" > ${D}${sysconfdir}/motd
}
