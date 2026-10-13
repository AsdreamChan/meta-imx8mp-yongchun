import pytest
import subprocess
from board import wait_until, reachable


def test_rmmod_unloads(ssh):
    ssh('modprobe yongchun_tpg')

    assert ssh('rmmod yongchun_tpg').returncode == 0
    assert ssh('lsmod | grep -qw yongchun_tpg').returncode == 1
    assert ssh('test -d /sys/bus/platform/drivers/yongchun-tpg/').returncode != 0


def test_modprobe_loads(ssh):
    ssh('rmmod yongchun_tpg')

    assert ssh('modprobe yongchun_tpg').returncode == 0
    assert ssh('lsmod | grep -qw yongchun_tpg').returncode == 0

    check_list = ssh('ls -d /sys/bus/platform/drivers/yongchun-tpg/tpg-*')
    assert check_list.returncode == 0
    assert 'tpg-30hz\n' in check_list.stdout and 'tpg-9hz\n' in check_list.stdout
    assert len(check_list.stdout.splitlines()) == 2


@pytest.mark.slow
def test_reboot_autoloads(ssh):
    def module_loaded():
        try:
            return ssh('lsmod | grep -qw yongchun_tpg', timeout=10).returncode == 0
        except subprocess.TimeoutExpired:
            return False

    # The connection can drop before reboot returns, so its return code
    # and a timeout here mean nothing either way.
    try:
        ssh('reboot', timeout=10)
    except subprocess.TimeoutExpired:
        pass

    # Wait for the board to go down first. Until it does, ssh still reaches
    # the system that has not rebooted yet, and every check below would pass.
    assert wait_until(lambda: not reachable(ssh), timeout=60), \
        "board still reachable 60 s after reboot"
    assert wait_until(lambda: reachable(ssh), timeout=180), \
        "board not back within 180 s"
    # sshd can be up before udev has loaded the module.
    assert wait_until(module_loaded, timeout=30), \
        "yongchun_tpg not loaded 30 s after ssh came back"

    devices = ssh('ls -d /sys/bus/platform/drivers/yongchun-tpg/tpg-*')
    assert devices.returncode == 0, devices.stderr
    assert len(devices.stdout.splitlines()) == 2
