SUMMARY = "EdgeOps Embedded Device Management & Diagnostics Agent"
DESCRIPTION = "C++ diagnostics agent collecting real Linux telemetry, managing systemd services, and serving a REST API and web dashboard."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "sqlite3"
RDEPENDS:${PN} = "sqlite3"

SRC_URI = " \
    file://src/models.hpp \
    file://src/collectors.hpp \
    file://src/collectors.cpp \
    file://src/storage.hpp \
    file://src/storage.cpp \
    file://src/storage_alarms.cpp \
    file://src/systemd_mgr.hpp \
    file://src/systemd_mgr.cpp \
    file://src/http_server.hpp \
    file://src/http_server.cpp \
    file://src/main.cpp \
    file://web/index.html \
"

S = "${WORKDIR}"
PACKAGECONFIG ??= "webui"
PACKAGECONFIG[webui] = ",,,"

do_compile() {
    ${CXX} ${CXXFLAGS} ${LDFLAGS} -std=c++17 -pthread \
        -DAPP_VERSION=\"${PV}\" \
        -I${S}/src \
        ${S}/src/main.cpp \
        ${S}/src/collectors.cpp \
        ${S}/src/storage.cpp \
        ${S}/src/storage_alarms.cpp \
        ${S}/src/systemd_mgr.cpp \
        ${S}/src/http_server.cpp \
        -lsqlite3 -o ${B}/edgeops-agent
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/edgeops-agent ${D}${bindir}/edgeops-agent

    if ${@bb.utils.contains('PACKAGECONFIG', 'webui', 'true', 'false', d)}; then
        install -d ${D}${datadir}/edgeops/web
        install -m 0644 ${S}/web/index.html ${D}${datadir}/edgeops/web/index.html
    fi
}

FILES:${PN} += "${datadir}/edgeops/web"
