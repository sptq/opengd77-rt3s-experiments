#!/usr/bin/env python3
#
# opengd77_event_log.py - download the EMCOM event log (station log) of an
# RT3S / MD-UV380 running the SP7TEAM OpenGD77 firmware, over the USB CPS link
# (radio on, USB connected, normal mode).
#
#   python3 opengd77_event_log.py                 # table on the screen
#   python3 opengd77_event_log.py --csv log.csv   # CSV for a logbook / report
#   python3 opengd77_event_log.py --input dump.bin  # offline, from a 64 KB dump
#
# Layout (functions/eventLog.h): 64 KB at 14 MB in the SPI flash, records of
# 32 bytes: seq (u32, 0xFFFFFFFF = empty), UTC time (u32, 0 = no clock),
# type (u8), 3 reserved bytes, 20 bytes of text.
#
# Requires: pyserial (pip install pyserial) when reading from the radio.

import argparse
import csv
import datetime
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LOG_ADDR = 14 * 1024 * 1024
LOG_SIZE = 64 * 1024
TYPES = ['SYS', 'TX', 'RX', 'DMR', 'APRS', 'MSG IN', 'MSG OUT', 'DTMF', 'SOS', 'SSTV']


def parse(data):
    out = []
    for off in range(0, len(data) - 31, 32):
        seq, t, typ = struct.unpack_from('<IIB', data, off)
        if seq == 0xFFFFFFFF:
            continue
        text = data[off + 12:off + 32].split(b'\x00')[0].decode('latin-1', 'replace')
        when = (datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%SZ') if t else '')
        out.append((seq, when, TYPES[typ] if typ < len(TYPES) else str(typ), text))
    out.sort()
    return out


def main():
    ap = argparse.ArgumentParser(description='Download the EMCOM event log from the radio.')
    ap.add_argument('--port', help='serial port (default: auto-detect)')
    ap.add_argument('--input', '-i', help='parse a 64 KB dump instead of reading the radio')
    ap.add_argument('--dump', help='also save the raw 64 KB region to this file')
    ap.add_argument('--csv', help='write CSV to this file')
    args = ap.parse_args()

    if args.input:
        data = open(args.input, 'rb').read()
    else:
        from opengd77_custom_data import find_port, open_port, read_flash
        with open_port(args.port or find_port()) as ser:
            data = read_flash(ser, LOG_ADDR, LOG_SIZE)
        if args.dump:
            open(args.dump, 'wb').write(data)

    rows = parse(data)
    if args.csv:
        with open(args.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['seq', 'utc', 'type', 'text'])
            w.writerows(rows)
        print(f"{len(rows)} entries -> {args.csv}")
    else:
        for seq, when, typ, text in rows:
            print(f"{seq:6d}  {when or '(no clock)':20s}  {typ:7s}  {text}")
        print(f"{len(rows)} entries")


if __name__ == '__main__':
    main()
