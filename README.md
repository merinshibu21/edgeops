# meta-edgeops

Custom Yocto layer for **EdgeOps: Embedded Edge Device Management & Diagnostics Unit**.

## 1. Build the platform
- **What changed:** Created the `meta-edgeops` layer, configured `MACHINE = "qemuarm64"`, and added custom distro `edgeops` (`conf/distro/edgeops.conf`) with `systemd` and `package_ipk`.
- **Why:** Establishes an isolated product layer separate from upstream `poky` targeting 64-bit ARM with `systemd` service supervision and IPK package management.
- **Verification:** Built `core-image-minimal`, booted via `runqemu qemuarm64 nographic`, and verified `aarch64` kernel, `/etc/os-release` (`EdgeOps Embedded Linux 1.0`), and `systemd` running as PID 1.

## 2. Customise Linux
- **What changed:** Added kernel configuration fragment `recipes-kernel/linux/files/edgeops.cfg` (`CONFIG_IKCONFIG=y`, `CONFIG_IKCONFIG_PROC=y`) via `linux-yocto_%.bbappend`, and extended `base-files` via `recipes-core/base-files/base-files_%.bbappend` to set a custom `/etc/motd` banner.
- **Why:** Exposes the running kernel configuration at `/proc/config.gz` for runtime verification and customizes the rootfs login banner cleanly through `.bbappend` files without altering upstream `poky`.
- **Verification:** Passed `bitbake -c kernel_configcheck virtual/kernel`, rebuilt `core-image-minimal`, booted `qemuarm64`, and verified both `/etc/motd` and `/proc/config.gz`.
