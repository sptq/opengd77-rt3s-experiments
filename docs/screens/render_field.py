#!/usr/bin/env python3
# Renders the field tool screens (PACE, Position, Coverage, Radio check) for the README,
# following menuPACE.c, menuPosition.c, menuCoverage.c and menuRadioCheck.c.
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_screens as R
import render_menus as M

P = R.DARK
FY = 128 - 11


def chip_title(s, x_right, text, c):
    w = len(text) * 6 + 4
    s.fill(x_right - w, 2, w, 10, c)
    s.text(x_right - w + 2, 3, text, '1b', P['bar'])


def keybar(s, text):
    s.fill(0, FY, 160, 11, P['bar'])
    s.text(3, FY + 2, text, 1, P['barFg'])


def pace():
    s = R.Screen(P['bg'])
    M.title_bar(s, P, 'PACE PLAN')
    rows = [('P', P['rx'], 'SR7TM AU Tomaszo', '438.6750 FM', True),
            ('A', P['info'], 'SR7VV AU+F Gora', '439.1125 FM', False),
            ('C', P['scan'], 'SIMPLEX 145.500', '145.5000 FM', False),
            ('E', P['tx'], None, None, False)]
    names = ['PRIMARY', 'ALTERNATE', 'CONTINGENCY', 'EMERGENCY']
    rh = (128 - 16 - 11 - 1) // 4
    for i, (l, c, name, freq, on) in enumerate(rows):
        y = 16 + i * rh
        s.fill(0, y, 20, rh - 2, c)
        s.text(6, y + (rh - 2 - 16) // 2, l, 3, P['bg'])
        if i == 0:
            s.rect(21, y, 139, rh - 2, P['fg'])
        if name:
            s.text(24, y + 1, name, '1b', P['fg'])
            s.text(24, y + 11, freq, 1, P['mut'])
            if on:
                s.fill(139, y + 2, 18, 9, c); s.text(142, y + 3, 'ON', '1b', P['bg'])
        else:
            s.text(24, y + 1, names[i], '1b', c)
            s.text(24, y + 11, 'empty - * stores', 1, P['mut'])
    keybar(s, '1-4 GO  * STORE  0 CLEAR')
    return s


def position():
    s = R.Screen(P['bg'])
    M.title_bar(s, P, 'POSITION')
    chip_title(s, 158, 'GPS', P['rx'])
    y = 17
    s.text(2, y, 'MGRS', 1, P['mut'])
    s.text(30, y - 1, '34U DB', 3, P['rx'])
    s.text(30, y + 15, '44348 94611', 3, P['fg'])
    y += 34; s.text(2, y, 'UTM 34U 444348E 5694611N', 1, P['fg'])
    y += 10; s.text(2, y, '51.40000N 20.20000E', 1, P['fg'])
    y += 10; s.text(2, y, 'LOC KO01CJ', 1, P['info']); s.text(158, y, 'ALT 187m', 1, P['fg'], True)
    y += 11; s.fill(0, y, 160, 1, P['mut']); y += 3
    s.text(2, y, 'FIX 3D  SAT 9  HDOP 0.9', 1, P['mut'])
    y += 10; s.text(2, y, 'SUN  07:02 - 15:41 LT', 1, P['scan'])
    y += 10; s.text(2, y, 'WPT 1250m  245 WSW', 1, P['info'])
    keybar(s, '* MARK WAYPOINT  RED EXIT')
    return s


def coverage():
    import math
    s = R.Screen(P['bg'])
    M.title_bar(s, P, 'COVERAGE')
    chip_title(s, 158, 'REC', P['tx'])
    y = 16
    s.text(2, y, '-97', 4, P['rx'])
    s.text(84, y + 2, 'S8', 3, P['fg'])
    s.text(84, y + 20, 'dBm  SIGNAL', 1, P['rx'])
    gt, gh = y + 34, 30
    s.rect(0, gt - 1, 160, gh + 2, P['mut'])
    gy = lambda d: gt + int((-50 - max(-130, min(-50, d))) * (gh - 1) / 80)
    for ref in (-60, -80, -100, -120):
        for x in range(2, 158, 4):
            s.fill(x, gy(ref), 1, 1, P['mut'])
    for i in range(78):
        v = int(-100 + 12 * math.sin(i / 7.0) - (i % 9) - (14 if 40 < i < 52 else 0))
        x = 160 - 2 - (78 - i) * 2
        c = P['rx'] if v >= -100 else (P['scan'] if v >= -115 else P['tx'])
        s.fill(x, gy(v), 2, gt + gh - gy(v), c)
    y = gt + gh + 3
    s.text(2, y, 'PTS 412/3840  EVERY 5s', 1, P['fg'])
    s.text(2, y + 10, 'SES 96  MIN -121  MAX -84', 1, P['info'])
    keybar(s, 'GRN STOP  # INT  RED EXIT')
    return s


def radio_check():
    s = R.Screen(P['bg'])
    M.title_bar(s, P, 'RADIO CHECK')
    chip_title(s, 158, '--', P['mut'])
    y = 16
    s.text(2, y, 'S1', 3, P['mut']); s.text(60, y + 4, '-131 dBm', 1, P['fg'])
    y += 20; s.fill(0, y, 160, 1, P['mut']); y += 3
    s.text(2, y, '5 BY 9', 4, P['rx'])
    s.text(2, y + 33, 'FIVE BY NINE', '1b', P['fg'])
    s.text(2, y + 43, 'FM 6.4s AVG -88 PK -84', 1, P['mut'])
    s.text(2, y + 55, '14:31  4 BY 6  -108 dBm', 1, P['info'])
    s.text(2, y + 65, '14:28  5 BY 9  -90 dBm', 1, P['info'])
    keybar(s, '0 CLEAR  RED EXIT')
    return s


if __name__ == '__main__':
    for name, fn in (('pace', pace), ('position', position), ('coverage', coverage), ('radio_check', radio_check)):
        fn().png(os.path.join(HERE, f'field_{name}.png'))
    M.launcher('MENU', M.MAIN, 6).png(os.path.join(HERE, 'menu_main_field.png'))
    print('ok')
