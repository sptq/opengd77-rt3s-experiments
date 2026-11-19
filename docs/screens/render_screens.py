#!/usr/bin/env python3
#
# Renders the README pictures of the RT3S EMCOM screens (160 x 128, scaled),
# with the bitmap fonts of the firmware (HX8353E_charset.h) and the layout of
# application/source/user_interface/uiEmcom.c. The data on the screens are
# examples. Real screenshots can be taken from a radio with
# MDUV380_firmware/tools/opengd77_screenshot.py.
#
#   python3 docs/screens/render_screens.py        (writes docs/screens/*.png)

import os
import re
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
CHARSET = os.path.join(HERE, '..', '..', 'MDUV380_firmware', 'application', 'include', 'hardware', 'HX8353E_charset.h')
W, H, SCALE = 160, 128, 3


def load_fonts():
    src = open(CHARSET, encoding='latin-1').read()
    fonts = {}
    for name, key in (('font_6x8', 1), ('font_6x8_bold', '1b'), ('font_8x8', 2), ('font_8x16', 3), ('font_16x32', 4)):
        m = re.search(r'const uint8_t ' + name + r'\[\] = \{(.*?)\};', src, re.S)
        body = re.sub(r'//[^\n]*', '', m.group(1))
        vals = [int(v, 16) if v.lower().startswith('0x') else int(v) for v in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', body)]
        first, w, h, bpc = vals[2], vals[4], vals[5], vals[7]
        fonts[key] = (first, w, h, bpc, vals[8:])
    return fonts


FONTS = load_fonts()


def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


class Screen:
    def __init__(self, bg):
        self.px = [[rgb(bg)] * W for _ in range(H)]

    def fill(self, x, y, w, h, c):
        c = rgb(c)
        for j in range(max(0, y), min(H, y + h)):
            row = self.px[j]
            for i in range(max(0, x), min(W, x + w)):
                row[i] = c

    def rect(self, x, y, w, h, c):
        self.fill(x, y, w, 1, c); self.fill(x, y + h - 1, w, 1, c)
        self.fill(x, y, 1, h, c); self.fill(x + w - 1, y, 1, h, c)

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0))
        for k in range(n + 1):
            self.fill(round(x0 + (x1 - x0) * k / n), round(y0 + (y1 - y0) * k / n), 1, 1, c)

    def text(self, x, y, s, font, c, right=False):
        first, fw, fh, bpc, data = FONTS[font]
        w = len(s) * fw
        if right:
            x -= w
        x = max(0, x)
        col = rgb(c)
        for k, ch in enumerate(s):
            if ch == ' ':
                continue
            o = (ord(ch) - first) * bpc
            g = data[o:o + bpc]
            for cx in range(fw):
                for cy in range(fh):
                    if (g[cx + (cy // 8) * fw] >> (cy % 8)) & 1:
                        px, py = x + k * fw + cx, y + cy
                        if 0 <= px < W and 0 <= py < H:
                            self.px[py][px] = col
        return w

    def png(self, path, scale=SCALE):
        raw = bytearray()
        for row in self.px:
            line = bytearray(b'\x00')
            for p in row:
                line += bytes(p) * scale
            for _ in range(scale):
                raw += line

        def chunk(tag, data):
            return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
        with open(path, 'wb') as f:
            f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W * scale, H * scale, 8, 2, 0, 0, 0)) +
                    chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


DARK = dict(bg=0x0B0E0C, fg=0xF2F5F0, mut=0x86928A, line=0x28302B, bar=0x18201B, barFg=0xE9EEE7,
            rx=0x3BD16F, tx=0xFF3B30, scan=0xFFB000, info=0x5AC8FA, seg=0x202823)
NIGHT = dict(bg=0x000000, fg=0xD22A2A, mut=0x7A1414, line=0x3A0808, bar=0x120202, barFg=0xD22A2A,
             rx=0xE23A3A, tx=0xFF4A4A, scan=0xA51C1C, info=0xC02626, seg=0x250505)

# --- layout of uiEmcom.c -------------------------------------------------------------
STATUS_H, METER_Y, ZONE_Y, HERO_Y, NAME_Y, FREQ_Y = 11, 12, 17, 20, 24, 40
DIV1_Y, PANEL_Y1, PANEL_Y2, DIV2_Y = 50, 54, 64, 75
EMCOM_Y1, EMCOM_Y2, EMCOM_Y3, FOOTER_Y = 79, 89, 99, 116


def kv(s, P, x, y, k, v, c=None):
    w = s.text(x, y, k, 1, P['mut']) + 4
    return w + s.text(x + w, y, v, 1, c if c is not None else P['fg'])


def status_bar(s, P, state='STBY', mode='FMN', pwr='5W', tone='CTR', aprs=True, gps=True, vol=3, batt=78, sos_phase=None):
    is_tx = state == 'TX'
    fg = 0xFFFFFF if is_tx else P['barFg']
    s.fill(0, 0, W, STATUS_H, P['tx'] if is_tx else P['bar'])
    if sos_phase is not None:
        s.fill(0, 0, W, STATUS_H, P['tx'] if sos_phase else P['bar'])
        s.text((W - 13 * 6) // 2, 2, 'EMERGENCY SOS', '1b', 0xFFFFFF if sos_phase else P['tx'])
        return
    chip = {'RX': 'RX', 'TX': 'TX', 'SCAN': 'SCAN'}.get(state, mode)
    chip_c = {'RX': P['rx'], 'TX': 0xFFFFFF, 'SCAN': P['scan']}.get(state, P['barFg'])
    cw = len(chip) * 6 + 4
    s.fill(1, 1, cw, 9, chip_c)
    s.text(3, 2, chip, '1b', P['tx'] if is_tx else P['bar'])
    x = cw + 4
    if state != 'STBY':
        x += s.text(x, 2, mode, 1, fg) + 4
    x += s.text(x, 2, pwr, 1, fg) + 4
    if tone:
        x += s.text(x, 2, tone, 1, fg) + 4
    if aprs:
        s.text(x, 2, 'a', '1b', fg if is_tx else P['rx'])
    r = W - 2
    s.rect(r - 13, 2, 12, 7, fg); s.fill(r - 1, 4, 1, 3, fg); s.fill(r - 12, 3, (batt * 10 + 50) // 100, 5, fg)
    r -= 16
    r -= s.text(r, 2, f'{batt}%', 1, fg, True) + 4
    for b in range(4):
        h = 2 * (b + 1)
        bx = r - 8 + b * 2
        if b < vol:
            s.fill(bx, 9 - h, 1, h, fg)
        else:
            s.fill(bx, 8, 1, 1, fg)
    r -= 12
    if gps:
        s.text(r, 2, 'GPS', '1b', fg if is_tx else P['rx'], True)


def bar(s, P, lit, tx=False):
    for i in range(32):
        c = P['seg']
        if i < lit:
            c = P['tx'] if tx else (P['rx'] if i < 20 else P['scan'])
        s.fill(i * 5, METER_Y, 4, 3, c)


def zone_row(s, P, zone='RAYNET LODZ', ch='CH 3/16'):
    s.text(2, ZONE_Y, zone, 1, P['mut'])
    s.text(W - 2, ZONE_Y, ch, 1, P['mut'], True)


def hero(s, P, num='03', name='RAYNET-1', freq='145.5000 MHz', tx=False):
    w = s.text(1, HERO_Y, num, 4, P['fg'])
    s.text(w + 6, NAME_Y, name, 3, P['fg'])
    s.text(w + 6, FREQ_Y, freq, 1, P['tx'] if tx else P['info'])


def divs(s, P):
    s.fill(0, DIV1_Y, W, 1, P['line']); s.fill(0, DIV2_Y, W, 1, P['line'])


def params_fm(s, P):
    kv(s, P, 2, PANEL_Y1, 'OFS', '-0.600'); kv(s, P, 66, PANEL_Y1, 'RXT', '88.5'); kv(s, P, 120, PANEL_Y1, 'SQL', '20')
    kv(s, P, 2, PANEL_Y2, 'BW', '12.5k'); kv(s, P, 66, PANEL_Y2, 'TXT', '88.5'); kv(s, P, 120, PANEL_Y2, 'TOT', '3m')


def emcom_rows(s, P, unread=0, lh='LH', lhv='SP7ABC-9 4.2km 2m'):
    kv(s, P, 2, EMCOM_Y1, 'APRS', '14 stn')
    if unread:
        t = f'MSG {unread}'
        s.fill(72, EMCOM_Y1 - 1, len(t) * 6 + 4, 10, P['info']); s.text(74, EMCOM_Y1, t, '1b', P['bg'])
    else:
        kv(s, P, 72, EMCOM_Y1, 'MSG', '0', P['mut'])
    s.text(W - 2, EMCOM_Y1, 'JO91RS', 1, P['rx'], True)
    kv(s, P, 2, EMCOM_Y2, 'POS', '51.7592N 19.4560E')
    kv(s, P, 2, EMCOM_Y3, lh, lhv)


def footer(s, P, left='14:32Z 19NOV', right=None, rc=None):
    s.fill(0, FOOTER_Y, W, H - FOOTER_Y, P['bar'])
    s.text(2, FOOTER_Y + 2, left, 1, P['barFg'])
    if right:
        s.text(W - 2, FOOTER_Y + 2, right, '1b', rc or P['barFg'], True)


def screen_standby(P=DARK):
    s = Screen(P['bg']); status_bar(s, P); bar(s, P, 0); zone_row(s, P); hero(s, P); divs(s, P)
    params_fm(s, P); emcom_rows(s, P); footer(s, P); return s


def screen_rx(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, 'RX'); bar(s, P, 23); zone_row(s, P); hero(s, P); divs(s, P)
    s.text(2, PANEL_Y1 - 2, '-75dBm', 3, P['rx']); s.text(W - 2, PANEL_Y1 - 2, 'S9+10', 3, P['rx'], True)
    kv(s, P, 2, PANEL_Y2 + 2, 'RXT', '88.5', P['rx'])
    emcom_rows(s, P); footer(s, P); return s


def screen_tx(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, 'TX'); bar(s, P, 17, True); zone_row(s, P)
    hero(s, P, freq='144.9000 MHz TX', tx=True); divs(s, P)
    s.text(1, PANEL_Y1 - 10, '2:48', 4, P['tx'])
    kv(s, P, 84, PANEL_Y1, 'TOT', '3m'); kv(s, P, 84, PANEL_Y2, 'TXT', '88.5')
    emcom_rows(s, P); footer(s, P, right='TX', rc=P['tx']); return s


def screen_dmr(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, 'RX', mode='DMR', tone='TS1', aprs=False); bar(s, P, 19)
    zone_row(s, P, 'DMR SR7', 'CH 7/16'); hero(s, P, '07', 'SR7L TG260', '439.4250 MHz'); divs(s, P)
    s.text(2, PANEL_Y1 - 2, 'SP5ABC', 3, P['rx']); kv(s, P, W - 2 - 6 * 6, PANEL_Y1, 'TG', '260')
    s.text(2, PANEL_Y2 + 2, 'Jan Warszawa', 1, P['fg'])
    emcom_rows(s, P, lhv='SP5ABC 0s'); footer(s, P, '14:35Z 19NOV'); return s


def screen_vfo(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, tone=''); bar(s, P, 0)
    s.text(2, ZONE_Y, 'VFO A', 1, P['mut']); s.text(W - 2, ZONE_Y, 'TX -0.600', 1, P['mut'], True)
    s.text(1, HERO_Y, '145.5000', 4, P['fg']); s.text(W - 2, FREQ_Y, 'MHz', 1, P['mut'], True); divs(s, P)
    kv(s, P, 2, PANEL_Y1, 'OFS', '-0.600'); kv(s, P, 66, PANEL_Y1, 'RXT', '-'); kv(s, P, 120, PANEL_Y1, 'SQL', '20')
    kv(s, P, 2, PANEL_Y2, 'BW', '12.5k'); kv(s, P, 66, PANEL_Y2, 'TXT', '-'); kv(s, P, 120, PANEL_Y2, 'TOT', 'off')
    emcom_rows(s, P, unread=1); footer(s, P, '14:40Z 19NOV'); return s


def screen_sos(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, sos_phase=True); bar(s, P, 0); zone_row(s, P); hero(s, P); divs(s, P)
    params_fm(s, P); emcom_rows(s, P); footer(s, P, right='SOS', rc=P['tx']); return s


def screen_scan(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, 'SCAN', tone=''); bar(s, P, 0); zone_row(s, P, ch='CH 5/16')
    hero(s, P, '05', 'PSP-LODZ', '149.1500 MHz'); divs(s, P); params_fm(s, P); emcom_rows(s, P)
    footer(s, P, 'ANY KEY: STOP', 'SCAN', P['scan']); return s


def main():
    shots = {
        'emcom_standby.png': screen_standby(),
        'emcom_rx.png': screen_rx(),
        'emcom_tx.png': screen_tx(),
        'emcom_dmr.png': screen_dmr(),
        'emcom_vfo.png': screen_vfo(),
        'emcom_scan.png': screen_scan(),
        'emcom_sos.png': screen_sos(),
        'emcom_night.png': screen_standby(NIGHT),
    }
    for name, s in shots.items():
        s.png(os.path.join(HERE, name))
        print(name)


if __name__ == '__main__':
    main()
