#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
boot_timeline.py: time the boot from U-Boot SPL to the login prompt.

Reads the board's serial console, stamps every line with the time it
arrived on the host, and prints when each boot stage started, counted
from the first "U-Boot SPL" line. Every line goes to --out with its
time, so the numbers can be checked against the log afterwards.

Start this first, then reboot the board (e.g. "reboot" over ssh), or
pass --reboot and it sends the reboot itself once the port is open.
Close any other program that has the port open.

Usage: boot_timeline.py --port /dev/ttyACM0 [--reboot root@board]
                        [--out boot.log] [--timeout 90]
"""
import argparse
import subprocess
import sys
import time

import serial

# Each string must appear only on the line that marks its stage. The
# SPL banner reads "U-Boot SPL 2026...", so it does not also match
# "U-Boot 2026", which is the banner of U-Boot proper.
MARKERS = [
    ("U-Boot SPL", "SPL"),
    ("U-Boot 2026", "U-Boot proper"),
    ("Hit any key to stop autoboot", "autoboot wait"),
    ("Scanning global bootmeth", "bootflow scan"),
    ("Starting kernel", "kernel"),
    ("Run /sbin/init", "systemd"),
    ("login:", "login"),
]


def main():
    p = argparse.ArgumentParser(description="Time the boot stages from the serial console.")
    p.add_argument("--port", required=True,
                   help="serial device, e.g. /dev/ttyACM0")
    p.add_argument("--reboot", metavar="SSH_TARGET",
                   help="send 'reboot' to this ssh target after opening the port, e.g. root@10.194.1.225")
    p.add_argument("--out", default="boot.log",
                   help="log of every line with its time (default: %(default)s)")
    p.add_argument("--timeout", type=int, default=90,
                   help="seconds to wait for the login prompt (default: %(default)s)")

    args = p.parse_args()

    try:
        # A short timeout, because the login prompt ends without a
        # newline: readline() only returns it when the timeout expires,
        # and the stamp would be late by up to that much.
        port = serial.Serial(args.port, 115200, timeout=0.1)
    except serial.SerialException as e:
        print(f"{args.port}: {e}", file=sys.stderr)
        return 1

    seen = {}
    # utf-8 explicitly: the default is the locale's encoding, which on
    # a Chinese Windows is cp950 and cannot write U+FFFD, the character
    # errors="replace" puts in for the noise the board sends at reset.
    with port, open(args.out, "w", encoding="utf-8") as log:
        # Drop whatever the console printed before we started (an old
        # prompt, a half line), so the first line read is a new one.
        port.reset_input_buffer()

        # Popen, not run: reading has to go on while ssh is still
        # connected, or the lines that arrive meanwhile get late stamps.
        reboot = None
        if args.reboot:
            reboot = subprocess.Popen(["ssh", "-o", "BatchMode=yes", args.reboot, "reboot"],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # monotonic, not time.time(): the wall clock can be stepped by
        # NTP while we measure; only differences between stamps matter.
        start = time.monotonic()
        deadline = start + args.timeout
        # With a short timeout, readline() can return part of a line.
        # Keep the parts in buf until the newline arrives, and look for
        # markers in what has arrived so far, so a marker split across
        # two reads is still found, and one at the end of a line without
        # a newline (the login prompt) is found as soon as it arrives.
        buf = b""
        line_t = None
        while time.monotonic() < deadline:
            chunk = port.readline()
            if not chunk:
                continue
            t = time.monotonic()
            if not buf:
                line_t = t
            buf += chunk
            text = buf.decode("utf-8", errors="replace")

            for marker, label in MARKERS:
                # Only the first time: a later line with the same text
                # must not move the stage.
                if marker in text and marker not in seen:
                    seen[marker] = t

            if buf.endswith(b"\n") or "login:" in seen:
                log.write(f"{line_t - start:9.3f} {text.rstrip()}\n")
                buf = b""

            if "login:" in seen:
                break

        if reboot is not None:
            try:
                reboot.wait(timeout=5)
            except subprocess.TimeoutExpired:
                reboot.kill()

    if "U-Boot SPL" not in seen:
        print("Never saw 'U-Boot SPL'.", file=sys.stderr)
        return 1
    if "login:" not in seen:
        print(f"No login prompt within {args.timeout} s; stages seen so far:", file=sys.stderr)

    t0 = seen["U-Boot SPL"]
    prev = t0
    for marker, label in MARKERS:
        if marker in seen:
            t = seen[marker] - t0
            print(f"{label:15s} {t:7.3f}  (+{seen[marker] - prev:.3f})")
            prev = seen[marker]

    return 0 if "login:" in seen else 1


if __name__ == "__main__":
    sys.exit(main())
