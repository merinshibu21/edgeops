import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
MAX_LINES = 200


def check_line_limit(content: str, limit: int = MAX_LINES) -> bool:
    return len(content.splitlines()) <= limit


def validate_layer_conf(text: str) -> bool:
    required = [
        r'BBFILE_COLLECTIONS\s*\+=\s*"meta-edgeops"',
        r'LAYERSERIES_COMPAT_meta-edgeops\s*=\s*"scarthgap"',
    ]
    return all(re.search(pat, text) for pat in required)


def validate_distro_conf(text: str) -> bool:
    return 'DISTRO = "edgeops"' in text and "systemd" in text


# --- Positive Tests (Assignment 1 & Assignment 2) ---
def test_layer_conf_exists_and_valid():
    content = (REPO_ROOT / "conf/layer.conf").read_text(encoding="utf-8")
    assert validate_layer_conf(content)


def test_distro_conf_enables_systemd():
    content = (REPO_ROOT / "conf/distro/edgeops.conf").read_text(encoding="utf-8")
    assert validate_distro_conf(content)


def test_kernel_fragment_enables_ikconfig():
    cfg = (REPO_ROOT / "recipes-kernel/linux/files/edgeops.cfg").read_text(encoding="utf-8")
    assert "CONFIG_IKCONFIG=y" in cfg
    assert "CONFIG_IKCONFIG_PROC=y" in cfg


def test_kernel_bbappend_references_fragment():
    bb = (REPO_ROOT / "recipes-kernel/linux/linux-yocto_%.bbappend").read_text(encoding="utf-8")
    assert "edgeops.cfg" in bb


def test_custom_motd_configured_in_base_files():
    bb = (REPO_ROOT / "recipes-core/base-files/base-files_%.bbappend").read_text(encoding="utf-8")
    assert "${sysconfdir}/motd" in bb
    assert "EdgeOps" in bb


def test_all_tracked_source_files_under_200_lines():
    exts = {".bb", ".bbappend", ".conf", ".cfg", ".py", ".sh", ".cpp", ".h"}
    for path in REPO_ROOT.rglob("*"):
        if path.is_file() and ".git" not in path.parts and path.suffix in exts:
            lines = len(path.read_text(encoding="utf-8").splitlines())
            assert lines <= MAX_LINES, f"{path} has {lines} lines (limit {MAX_LINES})"

# --- Positive Tests (Assignment 4: Systemd & Config) ---
def test_systemd_service_unit_valid():
    svc = (REPO_ROOT / "recipes-apps/edgeops-agent/files/edgeops-agent.service").read_text(encoding="utf-8")
    assert "ExecStart=/usr/bin/edgeops-agent" in svc
    assert "Restart=on-failure" in svc or "Restart=always" in svc
    assert "WantedBy=multi-user.target" in svc


def test_edgeops_conf_has_thresholds_and_services():
    conf = (REPO_ROOT / "recipes-apps/edgeops-agent/files/edgeops.conf").read_text(encoding="utf-8")
    for key in ("port", "cpu_threshold", "mem_threshold", "disk_threshold", "services"):
        assert key in conf


def test_agent_recipe_enables_systemd_and_installs_conf():
    bb = (REPO_ROOT / "recipes-apps/edgeops-agent/edgeops-agent_1.0.bb").read_text(encoding="utf-8")

    assert "inherit systemd" in bb
    assert 'SYSTEMD_SERVICE:${PN} = "edgeops-agent.service data.mount"' in bb
    assert 'SYSTEMD_AUTO_ENABLE:${PN} = "enable"' in bb
    assert "${sysconfdir}/edgeops/edgeops.conf" in bb
    assert "file://data.mount" in bb


# --- Negative Tests ---
def test_reject_layer_conf_missing_scarthgap():
    bad_conf = 'BBFILE_COLLECTIONS += "meta-edgeops"\nLAYERSERIES_COMPAT_meta-edgeops = "kirkstone"\n'
    assert not validate_layer_conf(bad_conf)


def test_reject_distro_conf_without_systemd():
    bad_distro = 'DISTRO = "edgeops"\nINIT_MANAGER = "sysvinit"\n'
    assert not validate_distro_conf(bad_distro)


def test_no_hardcoded_secrets_in_repo():
    secret_pattern = re.compile(r"(ghp_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|-----BEGIN [A-Z ]+ KEY-----)")
    for path in REPO_ROOT.rglob("*"):
        if path.is_file() and ".git" not in path.parts:
            text = path.read_text(encoding="utf-8", errors="ignore")
            assert not secret_pattern.search(text), f"Potential secret in {path}"

def test_agent_service_requires_persistent_data_mount():
    svc = (REPO_ROOT / "recipes-apps/edgeops-agent/files/edgeops-agent.service").read_text(encoding="utf-8")

    assert "Requires=data.mount" in svc
    assert "data.mount" in svc.split("After=", 1)[1].splitlines()[0]


def test_persistent_data_mount_configured_correctly():
    mount = (REPO_ROOT / "recipes-apps/edgeops-agent/files/data.mount").read_text(encoding="utf-8")

    assert "What=/dev/disk/by-label/EDGEOPS_DATA" in mount
    assert "Where=/data" in mount
    assert "Type=ext4" in mount


# --- Boundary Condition Tests ---
def test_line_limit_boundary_199_passes():
    assert check_line_limit("\n".join(["x = 1"] * 199))


def test_line_limit_boundary_200_passes():
    assert check_line_limit("\n".join(["x = 1"] * 200))


def test_line_limit_boundary_201_fails():
    assert not check_line_limit("\n".join(["x = 1"] * 201))
