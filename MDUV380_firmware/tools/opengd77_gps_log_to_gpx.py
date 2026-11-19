#!/usr/bin/env python3
#
# opengd77_gps_log_to_gpx.py - download the GPS track log of an MDUV380 / RT3S
# running OpenGD77 and convert it to GPX.
#
# The firmware records the raw NMEA sentences (while the GPS has a fix) into
# the last 2 MB of the 16 MB SPI flash when the GPS mode is set to "On+Log"
# (Options > GPS). Each logging session starts with "AAAA" and ends with "ZZ".
# This tool reads that region over the USB CPS link (radio on, USB connected,
# normal mode) and writes one GPX track segment per logging session.
#
#   python3 opengd77_gps_log_to_gpx.py -o track.gpx            # from the radio
#   python3 opengd77_gps_log_to_gpx.py --dump log.bin -o t.gpx # also keep the raw dump
#   python3 opengd77_gps_log_to_gpx.py --input log.bin -o t.gpx # offline, from a dump
#
# Requires: pyserial (pip install pyserial) when reading from the radio.

import argparse
import datetime
import os
import re
import sys
from xml.sax.saxutils import escape

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LOG_ADDR = 14 * 1024 * 1024   # LOG_FLASH_16MB_START_ADDRESS
LOG_SIZE = 2 * 1024 * 1024    # LOG_FLASH_16MB_MEM_SIZE
BLOCK = 64 * 1024
NMEA_RE = re.compile(rb'\$(G[PNLAB][A-Z]{3}),([^*$\r\n]*)\*([0-9A-Fa-f]{2})')


def read_log_from_radio(port, max_size):
    from opengd77_custom_data import find_port, open_port, read_flash  # pyserial needed here only
    port = port or find_port()
    print(f"Using serial port: {port}", file=sys.stderr)
    data = bytearray()
    blank_blocks = 0
    with open_port(port) as ser:
        addr = LOG_ADDR
        while len(data) < max_size:
            chunk = read_flash(ser, addr, BLOCK)
            data += chunk
            addr += BLOCK
            print(f"\r  read {len(data) // 1024} / {max_size // 1024} kB", end='', file=sys.stderr)
            blank_blocks = (blank_blocks + 1) if chunk.count(0xFF) == len(chunk) else 0
            if blank_blocks >= 2:   # erased flash: no more data (unless the log wrapped)
                break
    print(file=sys.stderr)
    return bytes(data)


def nmea_ok(name, body, cks):
    c = 0
    for b in name + b',' + body:
        c ^= b
    return c == int(cks, 16)


def lat_lon(value, hemi, deg_digits):
    if not value or not hemi:
        return None
    try:
        v = float(value[deg_digits:]) / 60.0 + int(value[:deg_digits])
    except ValueError:
        return None
    return -v if hemi in (b'S', b'W') else v


def parse_sessions(data):
    """Return a list of sessions, each a list of points (dicts)."""
    sessions = []
    # session markers; data before the first marker is kept too (wrapped log)
    for part in re.split(rb'A{4}', data):
        part = part.split(b'ZZ')[0]
        points = []
        last_alt = None
        for m in NMEA_RE.finditer(part):
            name, body, cks = m.group(1), m.group(2), m.group(3)
            if not nmea_ok(name, body, cks):
                continue
            f = body.split(b',')
            kind = name[2:]
            if kind == b'GGA' and len(f) >= 9:
                try:
                    last_alt = float(f[8]) if f[8] else None
                except ValueError:
                    last_alt = None
            elif kind == b'RMC' and len(f) >= 9 and f[1] == b'A':
                lat = lat_lon(f[2], f[3], 2)
                lon = lat_lon(f[4], f[5], 3)
                if lat is None or lon is None:
                    continue
                t = None
                try:
                    hh, mm, ss = int(f[0][0:2]), int(f[0][2:4]), float(f[0][4:])
                    dd, mo, yy = int(f[8][0:2]), int(f[8][2:4]), 2000 + int(f[8][4:6])
                    t = datetime.datetime(yy, mo, dd, hh, mm, int(ss), tzinfo=datetime.timezone.utc)
                except (ValueError, IndexError):
                    pass
                try:
                    speed_kmh = float(f[6]) * 1.852 if f[6] else None
                except ValueError:
                    speed_kmh = None
                p = {'lat': lat, 'lon': lon, 'time': t, 'ele': last_alt, 'speed': speed_kmh}
                if not points or (points[-1]['lat'], points[-1]['lon'], points[-1]['time']) != (lat, lon, t):
                    points.append(p)
        if points:
            sessions.append(points)
    # chronological order (the log is a ring buffer)
    sessions.sort(key=lambda s: s[0]['time'] or datetime.datetime.min.replace(tzinfo=datetime.timezone.utc))
    return sessions


def write_gpx(sessions, path):
    with open(path, 'w', encoding='utf-8') as out:
        out.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        out.write('<gpx version="1.1" creator="OpenGD77 SP7TQ fork" xmlns="http://www.topografix.com/GPX/1/1">\n')
        out.write('  <trk><name>%s</name>\n' % escape('OpenGD77 GPS log'))
        for s in sessions:
            out.write('    <trkseg>\n')
            for p in s:
                out.write('      <trkpt lat="%.6f" lon="%.6f">' % (p['lat'], p['lon']))
                if p['ele'] is not None:
                    out.write('<ele>%.1f</ele>' % p['ele'])
                if p['time'] is not None:
                    out.write('<time>%s</time>' % p['time'].strftime('%Y-%m-%dT%H:%M:%SZ'))
                out.write('</trkpt>\n')
            out.write('    </trkseg>\n')
        out.write('  </trk>\n</gpx>\n')


def main():
    ap = argparse.ArgumentParser(description='Download the OpenGD77 GPS NMEA log (GPS mode "On+Log") and convert it to GPX.')
    ap.add_argument('--port', help='serial port (default: auto-detect)')
    ap.add_argument('--input', '-i', help='convert a raw dump file instead of reading the radio')
    ap.add_argument('--dump', help='also save the raw log region to this file')
    ap.add_argument('--max-kb', type=int, default=LOG_SIZE // 1024, help='read at most this many kB (default: the whole 2 MB)')
    ap.add_argument('--output', '-o', required=True, help='GPX file to write')
    args = ap.parse_args()

    if args.input:
        with open(args.input, 'rb') as f:
            data = f.read()
    else:
        data = read_log_from_radio(args.port, min(args.max_kb * 1024, LOG_SIZE))
        if args.dump:
            with open(args.dump, 'wb') as f:
                f.write(data)

    sessions = parse_sessions(data)
    write_gpx(sessions, args.output)
    n = sum(len(s) for s in sessions)
    print(f"{len(sessions)} session(s), {n} point(s) -> {args.output}")
    if n == 0:
        print('No fixes found: set GPS to "On+Log" in Options > GPS and wait for a fix.', file=sys.stderr)


if __name__ == '__main__':
    main()
