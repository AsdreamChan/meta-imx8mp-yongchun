import pytest
import subprocess
from board import wait_until, read_console_until, reachable


@pytest.mark.slow
@pytest.mark.console
def test_boot_applies_overlay(ssh, console):
    def bmp280_loaded():
        try:
            return ssh('cat /sys/bus/iio/devices/*/name | grep -qw bmp280',
                       timeout=10).returncode == 0
        except subprocess.TimeoutExpired:
            return False

    console.reset_input_buffer()

    # The connection can drop before reboot returns, so its return code
    # and a timeout here mean nothing either way.
    try:
        ssh('reboot', timeout=10)
    except subprocess.TimeoutExpired:
        pass

    log = read_console_until(console, b'Starting kernel', timeout=90)
    tail = '\n'.join(log.splitlines()[-30:])

    assert 'Starting kernel' in log, \
        f"no 'Starting kernel' within 90 s; last 30 lines:\n{tail}"
    assert 'with extlinux' in log, \
        f"did not boot through extlinux; last 30 lines:\n{tail}"
    assert 'Retrieving file: /imx8mp-frdm-bmp280.dtbo' in log, \
        f"overlay not loaded; last 30 lines:\n{tail}"
    assert 'Invalid fdtoverlay_addr_r' not in log, \
        f"fdtoverlay_addr_r missing, overlays skipped; last 30 lines:\n{tail}"
    assert 'Running BSP bootcmd' not in log, \
        f"fell back to bsp_bootcmd; last 30 lines:\n{tail}"

    # SSH comes back after the kernel and userspace are up.
    assert wait_until(lambda: reachable(ssh), timeout=180), \
        "board not back within 180 s"
    # sshd can be up before udev has loaded the module.
    assert wait_until(bmp280_loaded, timeout=30), \
        "BMP280 not loaded 30 s after ssh came back"
