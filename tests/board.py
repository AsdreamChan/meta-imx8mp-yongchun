import subprocess
import time


def wait_until(predicate, timeout, interval=2.0):
    """Call predicate() every `interval` seconds until it returns True
    or `timeout` seconds have passed. Return True if it ever did."""
    deadline = time.monotonic() + timeout
    while True:
        if predicate():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval)


def read_console_until(port, marker, timeout):
    """Read from the serial port until `marker` (bytes) appears or `timeout`
    seconds have passed. Return everything read, decoded to str, whether or
    not the marker was found."""
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while marker not in buf:
        buf += port.read(256)
        if time.monotonic() >= deadline:
            break

    return buf.decode('utf-8', errors='replace')


def reachable(ssh):
    try:
        return ssh('true', timeout=10).returncode == 0
    except subprocess.TimeoutExpired:
        return False
