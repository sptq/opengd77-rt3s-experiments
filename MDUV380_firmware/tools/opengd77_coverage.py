#!/usr/bin/env python3
#
# opengd77_coverage.py - download the coverage survey (main menu > Coverage) of an
# RT3S / MD-UV380 running the SP7TEAM OpenGD77 firmware over USB, and save it as a
# coloured KML map (Google Earth / Maps), GPX or CSV.
#
#   python3 opengd77_coverage.py --kml coverage.kml
#   python3 opengd77_coverage.py --kml map.kml --csv points.csv --gpx track.gpx
#   python3 opengd77_coverage.py --input dump.bin --kml map.kml   # offline, from a dump
#
# Layout (functions/fieldData.h): the last 64 KB of the 16 MB SPI flash; the first
# 4 KB hold the PACE plan, then 16-byte points: UTC time (u32, 0xFFFFFFFF = empty),
# lat / lon (i32, degrees * 1e6), dBm (i16), flags (u8), frequency hint (u8).
#
# Colours: green >= -100 dBm (good), yellow -100..-110, orange -110..-118, red below.
#
# Requires: pyserial (pip install pyserial) when reading from the radio.

import argparse
import csv
import datetime
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

AREA_ADDR = 16 * 1024 * 1024 - 64 * 1024
POINTS_ADDR = AREA_ADDR + 4096
POINTS_SIZE = 64 * 1024 - 4096
FLAG_FIX, FLAG_CARRIER, FLAG_DMR, FLAG_SESSION = 0x01, 0x02, 0x04, 0x08


def parse(data):
    points, session = [], 0
    for off in range(0, len(data) - 15, 16):
        t, lat, lon, dbm, flags, _ = struct.unpack_from('<IiihBB', data, off)
        if t == 0xFFFFFFFF:
            break
        if flags & FLAG_SESSION:
            session += 1
        points.append({
            'utc': datetime.datetime.fromtimestamp(t, datetime.timezone.utc) if t else None,
            'lat': lat / 1e6, 'lon': lon / 1e6, 'dbm': dbm,
            'signal': bool(flags & FLAG_CARRIER), 'mode': 'DMR' if flags & FLAG_DMR else 'FM',
            'session': max(session, 1),
        })
    return points


def colour(dbm):
    # KML colours are aabbggrr
    if dbm >= -100:
        return 'ff00c800', 'good'
    if dbm >= -110:
        return 'ff00e6ff', 'fair'
    if dbm >= -118:
        return 'ff0080ff', 'weak'
    return 'ff0000ff', 'none'


def write_kml(path, points):
    styles = {}
    for name, c in (('good', 'ff00c800'), ('fair', 'ff00e6ff'), ('weak', 'ff0080ff'), ('none', 'ff0000ff')):
        styles[name] = (f'<Style id="{name}"><IconStyle><color>{c}</color><scale>0.6</scale>'
                        '<Icon><href>http://maps.google.com/mapfiles/kml/shapes/shaded_dot.png</href></Icon>'
                        '</IconStyle><LabelStyle><scale>0</scale></LabelStyle></Style>')
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<kml xmlns="http://www.opengis.net/kml/2.2"><Document><name>SP7TEAM coverage survey</name>']
    out += styles.values()
    for s in sorted({p['session'] for p in points}):
        out.append(f'<Folder><name>Session {s}</name>')
        for p in (q for q in points if q['session'] == s):
            _, style = colour(p['dbm'])
            when = p['utc'].strftime('%Y-%m-%d %H:%M:%SZ') if p['utc'] else ''
            out.append(f'<Placemark><name>{p["dbm"]} dBm</name><styleUrl>#{style}</styleUrl>'
                       f'<description>{p["dbm"]} dBm {p["mode"]} {"signal" if p["signal"] else "no signal"} {when}</description>'
                       f'<Point><coordinates>{p["lon"]:.6f},{p["lat"]:.6f},0</coordinates></Point></Placemark>')
        out.append('</Folder>')
    out.append('</Document></kml>')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))


def write_gpx(path, points):
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<gpx version="1.1" creator="SP7TEAM OpenGD77" xmlns="http://www.topografix.com/GPX/1/1">']
    for s in sorted({p['session'] for p in points}):
        out.append(f'<trk><name>Coverage session {s}</name><trkseg>')
        for p in (q for q in points if q['session'] == s):
            t = f'<time>{p["utc"].strftime("%Y-%m-%dT%H:%M:%SZ")}</time>' if p['utc'] else ''
            out.append(f'<trkpt lat="{p["lat"]:.6f}" lon="{p["lon"]:.6f}">{t}<name>{p["dbm"]} dBm</name></trkpt>')
        out.append('</trkseg></trk>')
    out.append('</gpx>')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))


def main():
    ap = argparse.ArgumentParser(description='Download the coverage survey points from the radio.')
    ap.add_argument('--port', help='serial port (default: auto-detect)')
    ap.add_argument('--input', '-i', help='parse a 60 KB points dump instead of reading the radio')
    ap.add_argument('--dump', help='also save the raw points area to this file')
    ap.add_argument('--kml', help='write a coloured KML map')
    ap.add_argument('--gpx', help='write a GPX track (one per session)')
    ap.add_argument('--csv', help='write CSV')
    args = ap.parse_args()

    if args.input:
        data = open(args.input, 'rb').read()
    else:
        from opengd77_custom_data import find_port, open_port, read_flash
        with open_port(args.port or find_port()) as ser:
            data = read_flash(ser, POINTS_ADDR, POINTS_SIZE)
        if args.dump:
            open(args.dump, 'wb').write(data)

    points = parse(data)
    if not points:
        print('no points')
        return
    if args.kml:
        write_kml(args.kml, points)
        print(f'{len(points)} points -> {args.kml}')
    if args.gpx:
        write_gpx(args.gpx, points)
        print(f'{len(points)} points -> {args.gpx}')
    if args.csv:
        with open(args.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['session', 'utc', 'lat', 'lon', 'dbm', 'signal', 'mode'])
            for p in points:
                w.writerow([p['session'], p['utc'].isoformat() if p['utc'] else '', f'{p["lat"]:.6f}', f'{p["lon"]:.6f}',
                            p['dbm'], int(p['signal']), p['mode']])
        print(f'{len(points)} points -> {args.csv}')
    if not (args.kml or args.gpx or args.csv):
        for p in points:
            when = p['utc'].strftime('%H:%M:%S') if p['utc'] else '--:--:--'
            print(f'{p["session"]:3d}  {when}  {p["lat"]:10.6f} {p["lon"]:11.6f}  {p["dbm"]:5d} dBm  {p["mode"]}')
        print(f'{len(points)} points')


if __name__ == '__main__':
    main()
