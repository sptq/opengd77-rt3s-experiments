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
    s.text(7, 3, title, 2, P['barFg'])
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


MAIN = [('ZONE', 'Zone'), ('CONTACTS', 'Contacts'), ('CHANNEL', 'Channel details'), ('RSSI', 'RSSI'), ('OPTIONS', 'Options'),
        ('LASTHEARD', 'Last heard'), ('PACE', 'PACE plan'), ('POSITION', 'Position'), ('COVERAGE', 'Coverage'),
        ('RADIOCHECK', 'Radio check'), ('APRS', 'APRS RX'), ('GPS', 'GPS'), ('SSTV', 'SSTV'), ('LOG', 'Event log'),
        ('RADIOINFO', 'Radio infos'), ('SATELLITE', 'Satellite'), ('INFO', 'Firmware info')]
OPTS = [('GENERAL', 'General'), ('RADIO', 'Radio'), ('DISPLAY', 'Display'), ('SOUND', 'Sound'), ('LANGUAGE', 'Language'),
        ('CALIBRATION', 'Calibration'), ('THEME', 'Theme'), ('APRSCFG', 'APRS'), ('RESET', 'Reset config'), ('QUICKKEYS', 'Quickkeys')]

def _main_extra():
    option_list('OPCJE EKRANU', DISPLAY_OPTS, 3).png(os.path.join(HERE, 'options_display.png'))
    lock_screen().png(os.path.join(HERE, 'lock.png'))
    tg_entry().png(os.path.join(HERE, 'tg_entry.png'))


def title_bar(s, P, title, pos=None):
    s.fill(0, 0, 160, 14, P['bar']); s.fill(0, 0, 3, 13, P['rx']); s.fill(0, 13, 160, 1, P['line'])
    s.text(7, 3, title, 2, P['barFg'])
    if pos:
        s.text(157, 4, pos, 1, P['mut'], True)


def option_list(title, rows, sel, P=R.DARK):
    s = R.Screen(P['bg'])
    title_bar(s, P, title, f'{sel + 1}/{len(rows)}')
    for i in range(-3, 4):
        k = sel + i
        if not 0 <= k < len(rows):
            continue
        y = 64 + i * 16
        label, value = rows[k]
        on = k == sel
        if on:
            s.fill(0, y, 160, 16, P['rx']); s.fill(0, y + 3, 2, 10, P['bg'])
        s.text(8, y, label, 3, P['bg'] if on else P['fg'])
        s.text(156, y, value, 3, P['bg'] if on else P['info'], True)
    return s


def lock_screen(P=R.DARK):
    s = R.Screen(P['bg'])
    title_bar(s, P, 'ZABLOKOWANA')
    for r, row in enumerate(ICONS['LOCK']):
        for b in range(16):
            if row & (1 << (15 - b)):
                s.fill(56 + b * 3, 22 + r * 3, 3, 3, P['scan'])
    t = 'Klawiatura & PTT'
    s.text((160 - len(t) * 8) // 2, 76, t, 3, P['fg'])
    s.fill(0, 110, 160, 18, P['bar'])
    for y, t in ((111, 'SK2 + *'), (119, 'aby odblokowa\x82')):
        s.text((160 - len(t) * 6) // 2, y, t, 1, P['barFg'])
    return s


def tg_entry(P=R.DARK):
    s = R.Screen(P['bg'])
    title_bar(s, P, 'WPISZ NUMER TG', '1/5')
    x = 2
    for t, on in (('TG', True), ('PC', False), ('CON', False), ('ID', False)):
        w = len(t) * 6 + 6
        s.fill(x, 18, w, 12, P['rx'] if on else P['bar'])
        s.text(x + 3, 20, t, '1b', P['bg'] if on else P['mut'])
        x += w + 3
    s.fill(2, 38, 156, 38, P['bar']); s.fill(2, 38, 3, 38, P['rx'])
    s.text(8, 38, '2601', 4, P['fg'])
    s.fill(8 + 4 * 16, 64, 14, 2, P['rx'])
    s.fill(0, 117, 160, 11, P['bar'])
    s.text(3, 119, '^v MODE  GRN OK  RED BACK', 1, P['barFg'])
    return s


DISPLAY_OPTS = [('Jasno\x8d\x82', '100%'), ('Mini jasno\x8d\x82', '5%'), ('Kontrast', '18'), ('Screen', 'EMCOM'),
                ('\x8cwiecenie', '30s'), ('Nite bright', '40%'), ('Kolejno\x8d\x82', 'CC/DB')]


if __name__ == '__main__':
    _main_extra()
    launcher('MENU', MAIN, 10).png(os.path.join(HERE, 'menu_main.png'))
    launcher('OPTIONS', OPTS, 2).png(os.path.join(HERE, 'menu_options.png'))
    launcher('MENU', MAIN, 5, R.NIGHT).png(os.path.join(HERE, 'menu_night.png'))
    print('ok')
