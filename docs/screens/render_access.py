#!/usr/bin/env python3
# Renders the accessibility display styles (Options > Display > Screen = EMCOM mono /
# EMCOM contr) as contact sheets for the README: idle, RX over S9, TX, scan with a low
# battery and no GPS fix, a menu and the night variant. Layout and colours follow
# uiEmcom.c (main screen) and emcomThemePreset() (menus).
#
#   python3 docs/screens/render_access.py      (writes emcom_mono.png, emcom_contrast.png)
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_screens as R  # noqa: E402

# menu theme items of emcomThemePreset(): selected block, option values, text, panels, accent
MENU = {
    'mono': dict(bg=0x000000, fg=0xFFFFFF, sel=0xFFFFFF, val=0xBEBEBE, bar=0x1E1E1E, name=0xF0F0F0, accent=0xDCDCDC, deco=0x8A8A8A),
    'contrast': dict(bg=0x000000, fg=0xFFFFFF, sel=0xFFFF00, val=0xFFFF00, bar=0x202020, name=0xFFFFFF, accent=0xFFFFFF, deco=0xFFFF00),
}

DISPLAY_OPTS = [('Brightness', '100%'), ('Min bright', '5%'), ('Contrast', '18'), ('Screen', None),
                ('Timeout', '30s'), ('Nite bright', '40%'), ('Order', 'CC/DB')]


def standby(P):
    return R.screen_standby(P)


def rx(P):
    s = R.Screen(P['bg']); R.status_bar(s, P, 'RX'); R.bar(s, P, 25); R.zone_row(s, P); R.hero(s, P); R.divs(s, P, R.EXPANDED_DIV2_Y)
    s.text(2, R.PANEL_LARGE_Y1, '-63dBm', 3, P['rx']); s.text(R.W - 2, R.PANEL_LARGE_Y1, 'S9+10', 3, P['rx'], True)
    R.kv(s, P, 2, R.PANEL_Y2, 'RXT', '88.5', P['rx'])
    R.kv(s, P, 2, R.PANEL_Y3, 'DIST', '12.3km', P['fg'])
    R.emcom_rows(s, P, unread=2, y1=R.EXPANDED_ROW_Y1, y2=R.EXPANDED_ROW_Y2, y3=R.EXPANDED_ROW_Y3)
    R.footer(s, P, right='RX', rc=P['rx']); return s


def tx(P):
    import render_refresh
    return render_refresh.tx_screen(P, elapsed=172)


def scan_low(P):
    s = R.Screen(P['bg']); R.status_bar(s, P, 'SCAN', tone='', batt=9, low=True, fix=False); R.bar(s, P, 0)
    R.zone_row(s, P, ch='CH 5/16'); R.hero(s, P, '05', 'PSP-LODZ', '149.1500 MHz'); R.divs(s, P); R.params_fm(s, P)
    R.emcom_rows(s, P); R.footer(s, P, 'ANY KEY: STOP', 'SCAN', P['scan']); return s


def menu(M, value, P):
    s = R.Screen(M['bg'])
    s.fill(0, 0, 160, 14, M['bar']); s.fill(0, 0, 3, 13, M['accent']); s.fill(0, 13, 160, 1, M['deco'])
    s.text(7, 3, 'DISPLAY OPTIONS', 2, M['name'])
    s.text(157, 4, '4/7', 1, M['deco'], True)
    sel = 3
    for i in range(-3, 4):
        k = sel + i
        y = 64 + i * 16
        label, v = DISPLAY_OPTS[k]
        v = v or value
        on = k == sel
        if on:
            s.fill(0, y, 160, 16, M['sel']); s.fill(0, y + 3, 2, 10, M['bg'])
        s.text(8, y, label, 3, M['bg'] if on else M['fg'])
        s.text(156, y, v, 3, M['bg'] if on else M['val'], True)
    return s


def night_menu(M):
    # emcomThemePreset(night): mono -> the grey ladder in reds, contrast -> amber / red-orange
    def mono(c):
        r = ((c & 0xFF) * 210) // 255
        return (r << 16) | ((r // 5) << 8) | (r // 5)

    def contrast(c):
        g = c & 0xFF
        return 0xFFA000 if c == 0xFFFFFF else (0xFF4020 if c == 0xFFFF00 else ((g << 16) | ((g // 4) << 8)))
    if M is MENU['mono']:
        return {k: mono(v) for k, v in M.items()}
    out = {k: contrast(v) for k, v in M.items()}
    out['sel'] = 0xFFA000  # the selected row stays an amber block
    return out


def sheet(path, panels, cols=3, gap=4, scale=2):
    rows = (len(panels) + cols - 1) // cols
    w = cols * R.W + (cols + 1) * gap
    h = rows * R.H + (rows + 1) * gap
    px = [[(0x60, 0x60, 0x60)] * w for _ in range(h)]
    for n, p in enumerate(panels):
        ox = gap + (n % cols) * (R.W + gap)
        oy = gap + (n // cols) * (R.H + gap)
        for y in range(R.H):
            px[oy + y][ox:ox + R.W] = p.px[y]
    raw = bytearray()
    for row in px:
        line = bytearray(b'\x00')
        for p in row:
            line += bytes(p) * scale
        for _ in range(scale):
            raw += line

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def main():
    for name, P, PN, M, value in (('emcom_mono.png', R.MONO, R.MONO_NIGHT, MENU['mono'], 'EMCOM mono'),
                                  ('emcom_contrast.png', R.CONTRAST, R.CONTRAST_NIGHT, MENU['contrast'], 'EMCOM contr')):
        sheet(os.path.join(HERE, name), [standby(P), rx(P), tx(P), scan_low(P), menu(M, value, P), standby(PN),
                                         tx(PN), rx(PN), menu(night_menu(M), value, PN)])
        print(name)


if __name__ == '__main__':
    main()
