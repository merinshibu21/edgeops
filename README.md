# EdgeOps — Embedded Edge Device Management & Diagnostics Unit

Custom Yocto Project (Scarthgap 5.0) BSP/distro layer (`meta-edgeops`) targeting `qemuarm64` with `systemd` init management, custom kernel configuration fragments, and rootfs customization.

## Repository Structure
- `conf/layer.conf` — Yocto layer registration (`edgeops`, priority 6, `scarthgap` compatibility)
- `conf/distro/edgeops.conf` — Custom distribution policy (`systemd`, `pam`, `usrmerge`)
- `recipes-kernel/linux/` — Out-of-tree `linux-yocto` kernel configuration fragment (`CONFIG_IKCONFIG=y`)
- `recipes-core/base-files/` — Custom rootfs login banner (`/etc/motd`)
- `tests/` — Automated layer, distro, kernel config, and code-quality validation tests
- `.github/workflows/ci.yml` — Automated PR & release CI validation pipeline
