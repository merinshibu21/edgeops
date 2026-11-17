# meta-edgeops

Custom Yocto layer for **EdgeOps: Embedded Edge Device Management & Diagnostics Unit**.

## 1. Build the platform
- **What changed:** Created the `meta-edgeops` layer, configured `MACHINE = "qemuarm64"`, and added custom distro `edgeops` (`conf/distro/edgeops.conf`) with `systemd` and `package_ipk`.
- **Why:** Establishes an isolated product layer separate from upstream `poky` targeting 64-bit ARM with `systemd` service supervision and IPK package management.
- **Verification:** Built `core-image-minimal`, booted via `runqemu qemuarm64 nographic`, and verified `aarch64` kernel, `/etc/os-release` (`EdgeOps Embedded Linux 1.0`), and `systemd` running as PID 1.
