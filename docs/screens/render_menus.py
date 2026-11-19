#!/usr/bin/env python3
# Renders the EMCOM launcher menus (main menu, options) for the README, with the
# firmware icons (uiIcons.h) and fonts, following menuDisplayMenuList.c.
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_screens as R

src = open(os.path.join(HERE, '..', '..', 'MDUV380_firmware', 'application', 'include', 'user_interface', 'uiIcons.h')).read()
names = re.findall(r'UI_ICON_(\w+),', src)
vals = re.findall(r'\{ ((?:0x[0-9A-F]{4}, ){15}0x[0-9A-F]{4}) \}', src)
ICONS = {n: [int(v, 16) for v in vs.split(', ')] for n, vs in zip(names, vals)}


def icon(s, x, y, n, c):
    for r, row in enumerate(ICONS[n]):
        for b in range(16):
            if row & (1 << (15 - b)):
                s.fill(x + b, y + r, 1, 1, c)


def launcher(title, items, sel, P=R.DARK):
    s = R.Screen(P['bg'])
    s.fill(0, 0, 160, 14, P['bar']); s.fill(0, 0, 3, 13, P['rx']); s.fill(0, 13, 160, 1, P['line'])
    s.text(7, 3, title, 1, P['barFg'])
    s.text(157, 4, f'{sel + 1}/{len(items)}', 1, P['mut'], True)
    top = max(0, sel // 4 - 2)
    for k, (ic, _) in enumerate(items):
        r, c = k // 4 - top, k % 4
        if not 0 <= r <= 2:
            continue
        x, y = 2 + c * 39, 16 + r * 31
        on = k == sel
        s.fill(x, y, 38, 30, P['rx'] if on else P['bar'])
        icon(s, x + 11, y + 8, ic, P['bg'] if on else P['fg'])
        if k < 10:
            s.text(x + 2, y + 1, '0' if k == 9 else str(k + 1), 1, P['bg'] if on else P['mut'])
    s.fill(0, 110, 160, 18, P['bar'])
    s.text(4, 111, items[sel][1], 3, P['barFg'])
    return s


MAIN = [('ZONE', 'Zone'), ('CONTACTS', 'Contacts'), ('CHANNEL', 'Channel details'), ('RSSI', 'RSSI'), ('INFO', 'Firmware info'),
        ('OPTIONS', 'Options'), ('LASTHEARD', 'Last heard'), ('RADIOINFO', 'Radio infos'), ('SATELLITE', 'Satellite'), ('GPS', 'GPS'),
        ('APRS', 'APRS RX'), ('LOG', 'Event log'), ('SSTV', 'SSTV')]
OPTS = [('GENERAL', 'General'), ('RADIO', 'Radio'), ('DISPLAY', 'Display'), ('SOUND', 'Sound'), ('LANGUAGE', 'Language'),
        ('CALIBRATION', 'Calibration'), ('THEME', 'Theme'), ('APRSCFG', 'APRS'), ('RESET', 'Reset config'), ('QUICKKEYS', 'Quickkeys')]

if __name__ == '__main__':
    launcher('MENU', MAIN, 10).png(os.path.join(HERE, 'menu_main.png'))
    launcher('OPTIONS', OPTS, 2).png(os.path.join(HERE, 'menu_options.png'))
    launcher('MENU', MAIN, 5, R.NIGHT).png(os.path.join(HERE, 'menu_night.png'))
    print('ok')
