#!/usr/bin/env python3
# README pictures of the RSSI, Last heard, APRS RX (frames, radar, messages) and event log
# screens in the EMCOM style, following the firmware layout. Example data.
import math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_screens as R
import render_menus as M

P = R.DARK


def footer(s, text, chip=None, chipc=None):
    s.fill(0, 117, 160, 11, P['bar']); s.text(3, 119, text, 1, P['barFg'])
    if chip:
        w = len(chip) * 6 + 4
        s.fill(158 - w, 118, w, 9, chipc); s.text(160 - w, 119, chip, '1b', P['bg'])


def chip(s, x, y, t, c):
    w = len(t) * 6 + 4
    s.fill(x, y - 1, w, 9, c); s.text(x + 2, y, t, '1b', P['bg']); return w


def rssi():
    s = R.Screen(P['bg']); M.title_bar(s, P, 'RSSI')
    s.fill(0, 14, 160, 11, P['bar']); s.text(3, 16, '145.5000 MHz FMN', 1, P['barFg'])
    chip(s, 136, 16, 'FOX', P['scan'])
    s.text(2, 24, '-71', 4, P['fg']); s.text(4 + 48, 42, 'dBm', 1, P['mut'])
    s.text(157, 32, 'S9+20', 3, P['scan'], True)
    for i in range(32):
        c = (P['rx'] if i < 20 else P['scan']) if i < 24 else P['bar']
        s.fill(i * 5, 58, 4, 12, c)
    s.fill(26 * 5 - 1, 55, 2, 18, P['fg'])
    for seg, lab in ((2, '1'), (7, '3'), (11, '5'), (16, '7'), (20, '9'), (24, '+20'), (28, '+40')):
        x = seg * 5; s.fill(x, 71, 1, 2, P['mut']); s.text(x - len(lab) * 3 + 1, 74, lab, 1, P['mut'])
    s.text(3, 90, 'MAX', 1, P['mut']); s.text(24, 90, '-62dBm', '1b', P['fg']); s.text(157, 90, '340m 072', 1, P['info'], True)
    footer(s, '* RAW  # FOX  0 RST  9 APRS')
    return s


def lastheard():
    s = R.Screen(P['bg']); M.title_bar(s, P, 'OSTATNIO AKTYWNE')
    rows = [('SP5ABC', 'TG260', 'TS1 0m'), ('SQ7XYZ', 'TG2601', 'TS1 2m'), ('SP7TQ', 'PRIVATE', 'TS2 5m'),
            ('DL1ABC', 'TG91', 'TS1 12m'), ('SP9DEF', 'TG260', 'TS1 1h'), ('OK1GHI', 'TG2602', 'TS2 3h'), ('SP3JKL', 'TG260', 'TS1 5h')]
    for k, (call, tg, age) in enumerate(rows):
        y = 16 + k * 16; on = k == 1
        if on:
            s.fill(0, y, 160, 16, P['rx']); s.fill(0, y + 3, 2, 10, P['bg'])
        else:
            s.fill(0, y + 15, 160, 1, P['line'])
        s.text(6, y, call, 3, P['bg'] if on else P['fg'])
        s.text(157, y, tg, '1b', P['bg'] if on else (P['scan'] if tg == 'PRIVATE' else P['info']), True)
        s.text(157, y + 8, age, 1, P['bg'] if on else P['mut'], True)
    return s


def aprs_strip(s, title, pos):
    M.title_bar(s, P, title, pos)
    s.fill(0, 14, 160, 11, P['bar'])
    x = 2 + chip(s, 2, 16, 'RX', P['rx']) + 4
    s.rect(x, 17, 22, 6, P['mut']); s.fill(x + 1, 18, 9, 4, P['rx']); x += 26
    s.text(x, 16, 'OK42 E3', 1, P['barFg'])
    chip(s, 158 - (len('MSG 1') * 6 + 4), 16, 'MSG 1', P['info'])
    s.fill(0, 25, 160, 1, P['line'])


def aprs_frames():
    s = R.Screen(P['bg']); aprs_strip(s, 'APRS FRAMES', '1/6')
    frames = [('SP7ABC-9>APDR16', '12s', '=5146.12N/01928.44E>Lodz mobile'), ('SR7NJ>APNU19', '1m', '!5145.30N/01929.10E#PHG5130 digi'),
              ('SQ7XYZ-7>T2SP0X', '3m', '`0Q6l!x[/>"4Q}QTH Zgierz')]
    for b, (hdr, age, info) in enumerate(frames):
        y = 27 + b * 25; on = b == 0
        if on:
            s.fill(0, y, 160, 8, P['rx']); s.fill(0, y + 2, 2, 4, P['bg'])
        s.text(4, y, hdr, '1b', P['bg'] if on else P['fg']); s.text(157, y, age, 1, P['bg'] if on else P['mut'], True)
        s.text(4, y + 8, info[:26], 1, P['info']); s.text(4, y + 16, info[26:52], 1, P['info'])
        s.fill(0, y + 24, 160, 1, P['line'])
    return s


def aprs_radar():
    s = R.Screen(P['bg']); aprs_strip(s, 'APRS RADAR', '3/6')
    cx, cy, r = 47, 27 + 9 + 44, 44
    def circle(rr, c):
        for a in range(0, 360, 2):
            s.fill(round(cx + math.cos(math.radians(a)) * rr), round(cy + math.sin(math.radians(a)) * rr), 1, 1, c)
    circle(r, P['mut']); circle(r // 2, P['line'])
    for d in range(-r + 2, r - 1, 3):
        s.fill(cx + d, cy, 1, 1, P['line']); s.fill(cx, cy + d, 1, 1, P['line'])
    s.text(cx - 2, cy - r - 8, 'N', '1b', P['fg'])
    for (dx, dy) in ((0, -4), (-1, -2), (1, -2), (-2, 0), (2, 0), (-3, 2), (3, 2)):
        pass
    s.fill(cx - 1, cy - 3, 3, 6, P['info'])
    for (b, d, sel) in ((60, 30, True), (200, 18, False), (300, 40, False), (120, 12, False)):
        x = round(cx + math.sin(math.radians(b)) * d); y = round(cy - math.cos(math.radians(b)) * d)
        for i in range(-2, 3):
            for j in range(-2, 3):
                if i * i + j * j <= 4: s.fill(x + i, y + j, 1, 1, P['rx'])
        if sel: 
            for a in range(0, 360, 10):
                s.fill(round(x + math.cos(math.radians(a)) * 5), round(y + math.sin(math.radians(a)) * 5), 1, 1, P['info'])
    s.rect(cx - 25, cy + 15, 7, 7, P['scan'])
    tx = cx + r + 6
    s.text(tx, 28, 'SP7ABC-9', '1b', P['fg']); s.fill(tx, 37, 160 - tx - 2, 1, P['line'])
    R.kv(s, P, tx, 41, 'DST', '4.2km'); R.kv(s, P, tx, 51, 'BRG', '060'); R.kv(s, P, tx, 61, 'AGE', '12s'); R.kv(s, P, tx, 71, 'KMH', '48')
    R.kv(s, P, tx, 119, 'R', '10km', P['info'])
    return s


def aprs_msgs():
    s = R.Screen(P['bg']); aprs_strip(s, 'APRS MESSAGES', '4/6')
    msgs = [('<', P['rx'], 'SP7ABC-9', None, None, '2m', 'Punkt 2 gotowy, QRV na 145.500'),
            ('>', P['info'], 'SP7ABC-9', 'ACK', P['rx'], '1m', 'QSL'),
            ('B', P['scan'], 'SR7NJ', None, None, '9m', 'Net RAYNET 20:00 kanal 3')]
    for b, (d, dc, peer, st, sc, age, text) in enumerate(msgs):
        y = 27 + b * 25; on = b == 0
        if on:
            s.fill(0, y, 160, 8, P['rx']); s.fill(0, y + 2, 2, 4, P['bg'])
        chip(s, 3, y, d, dc)
        s.text(16, y, peer, '1b', P['bg'] if on else P['fg']); s.text(157, y, age, 1, P['bg'] if on else P['mut'], True)
        if st: chip(s, 157 - len(age) * 6 - 4 - (len(st) * 6 + 4), y, st, sc)
        s.text(4, y + 8, text[:26], 1, P['fg']); s.text(4, y + 16, text[26:52], 1, P['fg'])
        s.fill(0, y + 24, 160, 1, P['line'])
    return s


def eventlog():
    s = R.Screen(P['bg']); M.title_bar(s, P, 'DZIENNIK'); s.text(157, 4, '128', 1, P['mut'], True)
    rows = [('14:41', 'TX', P['tx'], '145.5000 12s'), ('14:40', 'RX', P['rx'], '145.5000 8s -71'), ('14:38', 'MS<', P['rx'], 'SP7ABC-9:Punkt 2'),
            ('14:35', 'DMR', P['rx'], 'SP5ABC TG260'), ('14:32', 'APR', P['info'], 'SP7ABC-9 4.2km'), ('14:30', 'DTM', P['scan'], '1234#'),
            ('14:21', 'SST', P['info'], 'PD 120 120 lines'), ('14:02', 'SYS', P['mut'], 'power on'), ('13:55', 'SOS', P['tx'], 'SOS OFF')]
    for k, (t, ty, c, text) in enumerate(rows):
        y = 16 + k * 10; on = k == 2
        if on: s.fill(0, y - 1, 160, 10, P['rx'])
        s.text(2, y, t, 1, P['bg'] if on else P['mut'])
        s.fill(34, y - 1, 22, 9, c); s.text(36, y, ty, '1b', P['bg'])
        s.text(60, y, text[:16], 1, P['bg'] if on else P['fg'])
    s.fill(0, 108, 160, 20, P['bar'])
    s.text(3, 109, 'SP7ABC-9:Punkt 2 got', 1, P['barFg']); s.text(3, 119, '2026-11-19 14:38:05Z', 1, P['mut'])
    return s


if __name__ == '__main__':
    for name, fn in (('rssi', rssi), ('lastheard', lastheard), ('aprs_frames', aprs_frames), ('aprs_radar', aprs_radar),
                     ('aprs_messages', aprs_msgs), ('eventlog', eventlog)):
        fn().png(os.path.join(HERE, name + '.png'))
    print('ok')
