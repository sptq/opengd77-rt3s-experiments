#!/usr/bin/env python3
#
# opengd77_screenshot.py - save the screen of an MDUV380 / RT3S running
# OpenGD77 as a PNG, over the USB CPS link (radio on, USB cable connected,
# normal mode; close any CPS / browser tab that uses the radio first).
#
#   python3 opengd77_screenshot.py -o screen.png            # 160x128 x3
#   python3 opengd77_screenshot.py -o screen.png --scale 4
#   python3 opengd77_screenshot.py -o shot.png --count 5 --interval 2
#
# The firmware returns its frame buffer (CPS read, area 6): 160 x 128 pixels,
# 16 bit, byte swapped RGB565 (or BGR565 on some LCDs: use --bgr when red
# and blue are swapped).
#
# Requires: pyserial (pip install pyserial)

import argparse
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

CPS_ACCESS_DISPLAY_BUFFER = 6
WIDTH, HEIGHT = 160, 128
READ_CHUNK = 1024


def read_area(ser, area, address, length):
    from opengd77_custom_data import _read_exact
    out = bytearray()
    while length > 0:
        n = min(length, READ_CHUNK)
        ser.write(struct.pack('>BBIH', ord('R'), area, address, n))
        if _read_exact(ser, 1) != b'R':
            raise IOError(f"read failed at 0x{address:06X}")
        ln = struct.unpack('>H', _read_exact(ser, 2))[0]
        out += _read_exact(ser, ln)
        if ln != n:
            raise IOError(f"short read at 0x{address:06X}: asked {n} got {ln}")
        address += n
        length -= n
    return bytes(out)


def to_rgb(buf, bgr):
    rows = []
    for y in range(HEIGHT):
        row = bytearray()
        for x in range(WIDTH):
            i = (y * WIDTH + x) * 2
            v = (buf[i] << 8) | buf[i + 1]          # undo the byte swap
            a = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            r8, g8, b8 = (a << 3) | (a >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)
            row += bytes((b8, g8, r8) if bgr else (r8, g8, b8))
        rows.append(bytes(row))
    return rows


def write_png(path, rows, scale):
    w, h = WIDTH * scale, HEIGHT * scale
    raw = bytearray()
    for row in rows:
        line = bytearray(b'\x00')
        for x in range(WIDTH):
            line += row[x * 3:x * 3 + 3] * scale
        for _ in range(scale):
            raw += line

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(description='Save the OpenGD77 (MDUV380 / RT3S) screen as PNG over USB.')
    ap.add_argument('--port', help='serial port (default: auto-detect)')
    ap.add_argument('--output', '-o', required=True, help='PNG file (with --count: a number is added before .png)')
    ap.add_argument('--scale', type=int, default=3, help='pixel scale (default 3)')
    ap.add_argument('--bgr', action='store_true', help='swap red and blue (BGR565 LCDs)')
    ap.add_argument('--count', type=int, default=1, help='number of screenshots')
    ap.add_argument('--interval', type=float, default=1.0, help='seconds between screenshots')
    args = ap.parse_args()

    from opengd77_custom_data import find_port, open_port
    port = args.port or find_port()
    with open_port(port) as ser:
        for n in range(args.count):
            buf = read_area(ser, CPS_ACCESS_DISPLAY_BUFFER, 0, WIDTH * HEIGHT * 2)
            path = args.output
            if args.count > 1:
                base, ext = os.path.splitext(args.output)
                path = f"{base}_{n + 1:02d}{ext or '.png'}"
            write_png(path, to_rgb(buf, args.bgr), max(1, args.scale))
            print(path)
            if n + 1 < args.count:
                time.sleep(args.interval)


if __name__ == '__main__':
    main()
