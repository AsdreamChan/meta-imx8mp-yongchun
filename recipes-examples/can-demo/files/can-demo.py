#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
can-demo.py: Python version of can-demo.c.

Send one classical CAN frame on a SocketCAN interface, then print every
frame received on it until interrupted (Ctrl+C).

Usage: can-demo.py <ifname> [--no-recv-own]      e.g. can-demo.py vcan0
"""
import socket
import struct
import sys

# struct can_frame from <linux/can.h>:
#   can_id (u32), len (u8), __pad/__res0/len8_dlc (3 x u8), data[8]
# "=" = native byte order, no automatic alignment padding.
CAN_FRAME = struct.Struct("=IB3x8s")  # .size == 16 == CAN_MTU


def pack_frame(can_id, data):
    """Build the 16 bytes of a struct can_frame."""
    return CAN_FRAME.pack(can_id, len(data), data.ljust(8, b"\x00"))


def unpack_frame(raw):
    """Split 16 bytes into (can_id, len, data[:len])."""
    can_id, length, data = CAN_FRAME.unpack(raw)
    return can_id, length, data[:length]


def format_frame(can_id, length, data):
    """Same text as print_frame() in can-demo.c."""
    if can_id & socket.CAN_EFF_FLAG:
        line = f"{can_id & socket.CAN_EFF_MASK:08X}"
    else:
        line = f"     {can_id & socket.CAN_SFF_MASK:03X}"

    line += f"   [{length}] "

    if can_id & socket.CAN_RTR_FLAG:
        return line + " remote request"
    return line + "".join(f" {b:02X}" for b in data)


def main(argv):
    if len(argv) == 2:
        recv_own = 1
    elif len(argv) == 3 and argv[2] == "--no-recv-own":
        recv_own = 0
    else:
        print(f"usage: {argv[0]} <ifname> [--no-recv-own]", file=sys.stderr)
        return 1
    ifname = argv[1]

    # A raw CAN socket: whole frames in, whole frames out.
    with socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW) as s:
        # Off by default: a socket does not get back the frames it sends
        # itself; other sockets on the interface (candump) still do.
        s.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_RECV_OWN_MSGS,
                     recv_own)

        # The address of a CAN socket is the 1-tuple (ifname,); Python
        # does the if_nametoindex() itself.
        try:
            s.bind((ifname,))
        except OSError as e:
            print(f"{ifname}: {e.strerror}", file=sys.stderr)
            return 1

        s.send(pack_frame(0x123, b"\xDE\xAD\xBE\xEF"))

        try:
            while True:
                raw = s.recv(CAN_FRAME.size)
                if len(raw) != CAN_FRAME.size:
                    print(f"unexpected read of {len(raw)} bytes",
                          file=sys.stderr)
                    continue
                print(format_frame(*unpack_frame(raw)), flush=True)
        except KeyboardInterrupt:
            return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
