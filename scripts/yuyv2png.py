#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Convert one raw YUYV (YUY2) frame to PNG, with the standard library only.

    v4l2-ctl -d /dev/video3 --stream-mmap --stream-count=1 --stream-to=frame.yuv
    python3 yuyv2png.py frame.yuv 640 480 frame.png

BT.601 limited range, as the yongchun-tpg driver reports (SMPTE 170M).
"""

import struct
import sys
import zlib


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else int(v)


def yuv_to_rgb(y, cb, cr):
    # BT.601 limited range: Y spans 16-235, Cb/Cr 16-240 around 128.
    # 1.164 = 255/219 stretches Y back to 0-255; the other factors are
    # the BT.601 colour-difference coefficients scaled the same way.
    c = y - 16
    d = cb - 128
    e = cr - 128
    return (clamp(1.164 * c + 1.596 * e),
            clamp(1.164 * c - 0.392 * d - 0.813 * e),
            clamp(1.164 * c + 2.017 * d))


def yuyv_to_rows(data, width, height):
    # Every four bytes are two pixels, Y0 Cb Y1 Cr: the pair shares one
    # Cb/Cr (4:2:2), so the two pixels differ only in brightness.
    bpl = width * 2
    for row in range(height):
        line = data[row * bpl:(row + 1) * bpl]
        out = bytearray()
        for i in range(0, bpl, 4):
            y0, cb, y1, cr = line[i:i + 4]
            out += bytes(yuv_to_rgb(y0, cb, cr))
            out += bytes(yuv_to_rgb(y1, cb, cr))
        yield bytes(out)


def write_png(path, width, height, rows):
    # A PNG is an 8-byte signature followed by chunks, each laid out as
    # length, type, data, CRC-32 of type+data. Three are enough here:
    # IHDR (size and pixel format), IDAT (zlib-compressed rows), IEND.
    def chunk(kind, body):
        crc = zlib.crc32(kind + body) & 0xffffffff
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)

    raw = b"".join(b"\x00" + r for r in rows)   # filter type 0 per row
    # 8 bits per channel, colour type 2 (RGB), default compression and
    # filter method, no interlace
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 5:
        sys.exit("usage: yuyv2png.py IN.yuv WIDTH HEIGHT OUT.png")
    src, width, height, dst = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    data = open(src, "rb").read()
    need = width * height * 2
    if len(data) < need:
        sys.exit(f"{src}: {len(data)} bytes, one {width}x{height} YUYV frame needs {need}")
    if len(data) > need:
        print(f"note: {len(data) // need} frames in file, converting the first", file=sys.stderr)
    write_png(dst, width, height, yuyv_to_rows(data[:need], width, height))


if __name__ == "__main__":
    main()
