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


# palettes of uiEmcom.c (txFg: text on the TX colour; cues: state by shape; large: contrast style)
DARK = dict(bg=0x0B0E0C, fg=0xF2F5F0, mut=0x86928A, line=0x28302B, bar=0x18201B, barFg=0xE9EEE7,
            rx=0x3BD16F, tx=0xFF3B30, scan=0xFFB000, info=0x5AC8FA, seg=0x202823, txFg=0xFFFFFF)
NIGHT = dict(bg=0x000000, fg=0xD22A2A, mut=0x7A1414, line=0x3A0808, bar=0x120202, barFg=0xD22A2A,
             rx=0xE23A3A, tx=0xFF4A4A, scan=0xA51C1C, info=0xC02626, seg=0x250505, txFg=0xFFFFFF, night=True)
MONO = dict(bg=0x000000, fg=0xFFFFFF, mut=0x9A9A9A, line=0x4A4A4A, bar=0x1E1E1E, barFg=0xF0F0F0,
            rx=0xFFFFFF, tx=0xFFFFFF, scan=0xB4B4B4, info=0xD8D8D8, seg=0x343434, txFg=0x000000, cues=True)
MONO_NIGHT = dict(NIGHT, txFg=0x000000, cues=True)
CONTRAST = dict(bg=0x000000, fg=0xFFFFFF, mut=0xFFFF00, line=0x808080, bar=0x000000, barFg=0xFFFFFF,
                rx=0xFFFFFF, tx=0xFFFF00, scan=0xFFFF00, info=0xFFFFFF, seg=0x404040, txFg=0x000000, cues=True, large=True)
CONTRAST_NIGHT = dict(bg=0x000000, fg=0xFFA000, mut=0xFF4020, line=0x802000, bar=0x000000, barFg=0xFFA000,
                      rx=0xFFA000, tx=0xFFA000, scan=0xFF4020, info=0xFFA000, seg=0x401000, txFg=0x000000,
                      cues=True, large=True, night=True)


def f1(P):
    # the contrast style draws the small text in the bold font
    return '1b' if P.get('large') else 1


def inverse_chip(s, P, x, y, t, c, right=False, tc=None):
    w = len(t) * 6 + 4
    if right:
        x -= w
    s.fill(x, y - 1, w, 10, c)
    s.text(x + 2, y, t, '1b', P['bar'] if tc is None else tc)
    return w

# --- layout of uiEmcom.c -------------------------------------------------------------
STATUS_H, METER_Y, ZONE_Y, HERO_Y, NAME_Y, FREQ_Y = 11, 12, 17, 20, 24, 40
SMALL_ROW_PITCH = 10
DIV1_Y, PANEL_Y1, PANEL_Y2, PANEL_Y3 = 50, 54, 64, 74
CHIP_TOP_OFFSET, CHIP_HEIGHT = -1, 10
PANEL_LARGE_Y1 = DIV1_Y
FONT_SIZE_3_GLYPH_BOTTOM = 12
COMPACT_DIV2_Y = 75
COMPACT_ROW_Y1 = 79
COMPACT_ROW_Y2, COMPACT_ROW_Y3 = COMPACT_ROW_Y1 + SMALL_ROW_PITCH, COMPACT_ROW_Y1 + (2 * SMALL_ROW_PITCH)
EXPANDED_DIV2_Y = 83
EXPANDED_ROW_Y1 = 86
EXPANDED_ROW_Y2, EXPANDED_ROW_Y3 = EXPANDED_ROW_Y1 + SMALL_ROW_PITCH, EXPANDED_ROW_Y1 + (2 * SMALL_ROW_PITCH)
FONT_SIZE_1_HEIGHT, FONT_SIZE_3_HEIGHT = 8, 16
TX_TIMER_Y, TX_WARN_SECS = 46, 10
FOOTER_Y = 116


def assert_layout_boundaries():
    """Keep the mock renderer's geometry aligned with uiEmcom.c."""
    assert PANEL_Y2 - PANEL_Y1 == SMALL_ROW_PITCH
    assert PANEL_Y3 - PANEL_Y2 == SMALL_ROW_PITCH
    assert PANEL_LARGE_Y1 + FONT_SIZE_3_GLYPH_BOTTOM + 1 <= PANEL_Y2
    assert PANEL_Y3 + CHIP_TOP_OFFSET + CHIP_HEIGHT <= EXPANDED_DIV2_Y
    assert PANEL_LARGE_Y1 + FONT_SIZE_3_HEIGHT <= COMPACT_DIV2_Y
    assert COMPACT_ROW_Y3 + FONT_SIZE_1_HEIGHT + 1 <= FOOTER_Y
    assert EXPANDED_ROW_Y3 + FONT_SIZE_1_HEIGHT + 2 <= FOOTER_Y
    assert FOOTER_Y + 12 == H


assert_layout_boundaries()


def kv(s, P, x, y, k, v, c=None):
    w = s.text(x, y, k, f1(P), P['mut']) + 4
    return w + s.text(x + w, y, v, f1(P), c if c is not None else P['fg'])


def status_bar(s, P, state='STBY', mode='FMN', pwr='5W', tone='CTR', aprs=True, gps=True, vol=3, batt=78, sos_phase=None,
               low=False, fix=True, time_header=False, battery_voltage=False, force_battery=False, show_volume=True):
    is_tx = state == 'TX'
    cues = P.get('cues', False)
    colour_state_header = (not P.get('night', False)) and (not cues)
    colour_state_rx_header = colour_state_header and state == 'RX'
    display_time = time_header and (not force_battery) and (not low)
    fg = P['txFg'] if is_tx else (P['bar'] if colour_state_rx_header else P['barFg'])
    s.fill(0, 0, W, STATUS_H, P['tx'] if is_tx else (P['rx'] if colour_state_rx_header else P['bar']))
    if sos_phase is not None:
        s.fill(0, 0, W, STATUS_H, P['tx'] if sos_phase else P['bar'])
        s.text((W - 13 * 6) // 2, 2, 'EMERGENCY SOS', '1b', P['txFg'] if sos_phase else P['tx'])
        return
    chip = {'TX': 'TX', 'SCAN': 'SCAN'}.get(state, mode)
    chip_text = P['tx'] if is_tx else P['bar']
    chip_c = {'TX': P['txFg'], 'SCAN': P['scan']}.get(state, P['barFg'])
    colour_state_chip = colour_state_header
    if colour_state_chip and state == 'TX':
        chip_c, chip_text = P['tx'], P['txFg']
    elif colour_state_chip and state == 'RX':
        chip_c, chip_text = P['rx'], P['bar']
    cw = len(chip) * 6 + 4
    if cues and state == 'STBY':
        s.rect(1, 1, cw, 9, chip_c)
        s.text(3, 2, chip, '1b', chip_c)
    else:
        s.fill(1, 1, cw, 9, chip_c)
        s.text(3, 2, chip, '1b', chip_text)
    x = cw + 4
    if state not in ('STBY', 'RX'):
        x += s.text(x, 2, mode, f1(P), fg) + 4
    x += s.text(x, 2, pwr, f1(P), fg) + 4
    if tone:
        x += s.text(x, 2, tone, f1(P), fg) + 4
    if aprs:
        s.text(x, 2, 'a', '1b', fg if (is_tx or colour_state_rx_header) else P['rx'])
    r = W - 2
    bc = P['tx'] if (low and not cues) else fg
    if display_time:
        r -= s.text(r, 2, '14:32', f1(P), fg, True) + 4
    else:
        s.rect(r - 13, 2, 12, 7, bc); s.fill(r - 1, 4, 1, 3, bc); s.fill(r - 12, 3, (batt * 10 + 50) // 100, 5, bc)
        r -= 16
        if cues and low:
            r -= inverse_chip(s, P, r + 2, 2, f'!{batt}%', fg, True, P['tx'] if is_tx else P['bar']) + 2
        else:
            battery_text = '12.3V' if battery_voltage else f'{batt}%'
            r -= s.text(r, 2, battery_text, f1(P), bc, True) + 4
    if show_volume:
        for b in range(4):
            h = 2 * (b + 1)
            bx = r - 8 + b * 2
            if b < vol:
                s.fill(bx, 9 - h, 1, h, fg)
            else:
                s.fill(bx, 8, 1, 1, fg)
        r -= 12
    if gps:
        if fix:
            s.text(r, 2, 'GPS', '1b', fg if (is_tx or colour_state_rx_header) else P['rx'], True)
        else:
            s.text(r, 2, 'GPS', 1 if cues else '1b', P['mut'], True)


def bar(s, P, lit, tx=False):
    h = 4 if P.get('large') else 3
    for i in range(32):
        c = P['seg']
        if i < lit:
            c = P['tx'] if tx else (P['rx'] if i < 20 else P['scan'])
        if P.get('cues') and i < lit and not tx and i >= 20:
            s.fill(i * 5, METER_Y, 4, h, P['seg']); s.rect(i * 5, METER_Y, 4, h, c)
        else:
            s.fill(i * 5, METER_Y, 4, h, c)


def zone_row(s, P, zone='RAYNET LODZ', ch='CH 3/16'):
    s.text(2, ZONE_Y, zone, f1(P), P['mut'])
    s.text(W - 2, ZONE_Y, ch, f1(P), P['mut'], True)


def hero(s, P, num='03', name='RAYNET-1', freq='145.5000 MHz', tx=False):
    w = s.text(1, HERO_Y, num, 4, P['fg'])
    c = P['tx'] if tx else P['info']
    if P.get('large'):
        # as drawHero(): 8x16 frequency, without the unit when it does not fit
        mx = (W - (w + 6)) // 8
        s.text(w + 6, 23, name[:mx], 3, P['fg'])
        for f in (freq, freq.replace(' MHz', '')):
            if len(f) <= mx:
                s.text(w + 6, 36, f, 3, c)
                return
        s.text(w + 6, FREQ_Y, freq, f1(P), c)
        return
    s.text(w + 6, NAME_Y, name, 3, P['fg'])
    s.text(w + 6, FREQ_Y, freq, 1, c)


def divs(s, P, div2=COMPACT_DIV2_Y):
    s.fill(0, DIV1_Y, W, 1, P['line']); s.fill(0, div2, W, 1, P['line'])


def params_fm(s, P):
    kv(s, P, 2, PANEL_Y1, 'OFS', '-0.600'); kv(s, P, 66, PANEL_Y1, 'RXT', '88.5'); kv(s, P, 120, PANEL_Y1, 'SQL', '20')
    kv(s, P, 2, PANEL_Y2, 'BW', '12.5k'); kv(s, P, 66, PANEL_Y2, 'TXT', '88.5'); kv(s, P, 120, PANEL_Y2, 'TOT', '3m')


def emcom_rows(s, P, unread=0, lh='LH', lhv='SP7ABC-9 4.2km 2m', y1=COMPACT_ROW_Y1, y2=COMPACT_ROW_Y2, y3=COMPACT_ROW_Y3, show_lh=True):
    kv(s, P, 2, y1, 'APRS', '14 stn')
    if unread:
        t = f'MSG {unread}'
        s.fill(72, y1 - 1, len(t) * 6 + 4, 10, P['info']); s.text(74, y1, t, '1b', P['bg'])
    else:
        kv(s, P, 72, y1, 'MSG', '0', P['mut'])
    s.text(W - 2, y1, 'JO91RS', f1(P), P['rx'], True)
    kv(s, P, 2, y2, 'POS', '51.7592N 19.4560E')
    if show_lh:
        kv(s, P, 2, y3, lh, lhv)


def footer(s, P, left='14:32Z 19NOV', right=None, rc=None, est=None, blk=False, local_zulu=False):
    # uiEmcom.c drawFooter(): the right item (SOS, SCAN, TX OFF, locks, TX), else the battery
    # time left; the BLK chip (blackout) left of it
    if local_zulu:
        left = '06:32/14:32Z 19NOV'
    s.fill(0, FOOTER_Y, W, H - FOOTER_Y, P['bar'])
    s.text(2, FOOTER_Y + 2, left, f1(P), P['barFg'])
    r = W - 2
    if right:
        if P.get('cues') and right in ('SOS', 'TX OFF', 'TX'):
            r = W - 1 - inverse_chip(s, P, W - 1, FOOTER_Y + 2, right, rc or P['barFg'], True) - 3
        else:
            r -= s.text(W - 2, FOOTER_Y + 2, right, '1b', rc or P['barFg'], True) + 4
    elif est:
        r -= s.text(r, FOOTER_Y + 2, est, f1(P), P['barFg'], True) + 4
    if blk:
        inverse_chip(s, P, r + 1, FOOTER_Y + 2, 'BLK', P['barFg'], True)


def screen_standby(P=DARK, distance=True, distance_text='12.3km'):
    # The standby/FM receive panel always reserves the DIST row. When the
    # setting is off the reserved row is blank; an unavailable value is shown
    # only when distance display is enabled.
    rows = (EXPANDED_ROW_Y1, EXPANDED_ROW_Y2, EXPANDED_ROW_Y3)
    s = Screen(P['bg']); status_bar(s, P); bar(s, P, 0); zone_row(s, P); hero(s, P); divs(s, P, EXPANDED_DIV2_Y)
    params_fm(s, P)
    if distance:
        kv(s, P, 2, PANEL_Y3, 'DIST', distance_text, P['mut'] if distance_text == '--' else P['fg'])
    emcom_rows(s, P, y1=rows[0], y2=rows[1], y3=rows[2]); footer(s, P); return s


def screen_rx(P=DARK, distance=True, distance_text='12.3km'):
    rows = (EXPANDED_ROW_Y1, EXPANDED_ROW_Y2, EXPANDED_ROW_Y3)
    s = Screen(P['bg']); status_bar(s, P, 'RX'); bar(s, P, 23); zone_row(s, P); hero(s, P); divs(s, P, EXPANDED_DIV2_Y)
    s.text(2, PANEL_LARGE_Y1, '-75dBm', 3, P['rx']); s.text(W - 2, PANEL_LARGE_Y1, 'S9+10', 3, P['rx'], True)
    kv(s, P, 2, PANEL_Y2, 'RXT', '88.5', P['rx'])
    if distance:
        kv(s, P, 2, PANEL_Y3, 'DIST', distance_text, P['mut'] if distance_text == '--' else P['fg'])
    emcom_rows(s, P, y1=rows[0], y2=rows[1], y3=rows[2]); footer(s, P, right='RX', rc=P['rx']); return s


def screen_tx(P=DARK):
    # TX view: elapsed time, power, TOT bar (render_refresh.py, uiEmcom.c drawTxPanel / drawTxRows)
    import render_refresh
    return render_refresh.tx_screen(P, elapsed=12)


def screen_dmr(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, 'RX', mode='DMR', tone='TS1', aprs=False); bar(s, P, 19)
    zone_row(s, P, 'DMR SR7', 'CH 7/16'); hero(s, P, '07', 'SR7L TG260', '439.4250 MHz'); divs(s, P, EXPANDED_DIV2_Y)
    s.text(2, PANEL_LARGE_Y1, 'SP5ABC', 3, P['rx']); kv(s, P, W - 2 - 6 * 6, PANEL_Y1, 'TG', '260')
    s.text(2, PANEL_Y2, 'Jan Warszawa', f1(P), P['fg'])
    s.text(2, PANEL_Y3, 'RAYNET LODZ', f1(P), P['fg'])
    emcom_rows(s, P, lhv='SP5ABC 0s', y1=EXPANDED_ROW_Y1, y2=EXPANDED_ROW_Y2, y3=EXPANDED_ROW_Y3)
    footer(s, P, '14:35Z 19NOV', right='RX', rc=P['rx']); return s


def screen_dmr_standby(P=DARK, distance=True):
    """DMR channel idle immediately after a call, including the stable DIST row."""
    s = Screen(P['bg']); status_bar(s, P, mode='DMR', tone='TS1', aprs=False); bar(s, P, 0)
    zone_row(s, P, 'DMR SR7', 'CH 7/16'); hero(s, P, '07', 'SR7L TG260', '439.4250 MHz')
    divs(s, P, EXPANDED_DIV2_Y)
    kv(s, P, 2, PANEL_Y1, 'TG', '260'); kv(s, P, 66, PANEL_Y1, 'CC', '1'); kv(s, P, 120, PANEL_Y1, 'TS', '1')
    s.text(2, PANEL_Y2, 'SR7L TG260', f1(P), P['fg'])
    if distance:
        kv(s, P, 2, PANEL_Y3, 'DIST', '12.3km', P['fg'])
    emcom_rows(s, P, y1=EXPANDED_ROW_Y1, y2=EXPANDED_ROW_Y2, y3=EXPANDED_ROW_Y3)
    footer(s, P); return s


def screen_dmr_short(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, 'RX', mode='DMR', tone='TS1', aprs=False); bar(s, P, 19)
    zone_row(s, P, 'DMR SR7', 'CH 7/16'); hero(s, P, '07', 'SR7L TG260', '439.4250 MHz')
    divs(s, P, EXPANDED_DIV2_Y)
    s.text(2, PANEL_LARGE_Y1, 'SP5ABC', 3, P['rx']); kv(s, P, W - 2 - 6 * 6, PANEL_Y1, 'TG', '260')
    s.text(2, PANEL_Y2, 'Jan', f1(P), P['fg'])
    emcom_rows(s, P, lhv='SP5ABC 0s', y1=EXPANDED_ROW_Y1, y2=EXPANDED_ROW_Y2, y3=EXPANDED_ROW_Y3)
    footer(s, P, '14:35Z 19NOV', right='RX', rc=P['rx']); return s


def screen_details(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, 'RX'); bar(s, P, 23); zone_row(s, P); hero(s, P); divs(s, P)
    s.fill(0, PANEL_Y1 - 1, 2, (PANEL_Y2 + 9) - (PANEL_Y1 - 1), P['info'])
    kv(s, P, 6, PANEL_Y1, 'RX', '145.5000'); s.text(W - 2, PANEL_Y1, '88.5', f1(P), P['info'], True)
    kv(s, P, 6, PANEL_Y2, 'TX', '144.9000'); s.text(W - 2, PANEL_Y2, '88.5', f1(P), P['info'], True)
    emcom_rows(s, P); footer(s, P, right='RX', rc=P['rx']); return s


def screen_vfo(P=DARK):
    # FM VFO RX: the RX panel remains compact and has no repeater distance row.
    s = Screen(P['bg']); status_bar(s, P, 'RX', tone=''); bar(s, P, 0)
    s.text(2, ZONE_Y, 'VFO A', f1(P), P['mut']); s.text(W - 2, ZONE_Y, 'TX -0.600', f1(P), P['mut'], True)
    s.text(1, HERO_Y, '145.5000', 4, P['fg']); s.text(W - 2, FREQ_Y, 'MHz', f1(P), P['mut'], True); divs(s, P)
    s.text(2, PANEL_LARGE_Y1, '-75dBm', 3, P['rx']); s.text(W - 2, PANEL_LARGE_Y1, 'S9+10', 3, P['rx'], True)
    s.text(2, PANEL_Y2, 'CARRIER', f1(P), P['mut'])
    emcom_rows(s, P, unread=1); footer(s, P, '14:40Z 19NOV', right='RX', rc=P['rx']); return s


def screen_sos(P=DARK):
    s = Screen(P['bg']); status_bar(s, P, sos_phase=True); bar(s, P, 0); zone_row(s, P); hero(s, P); divs(s, P)
    params_fm(s, P); emcom_rows(s, P); footer(s, P, right='SOS', rc=P['tx']); return s


def screen_scan(P=DARK, distance=True, distance_text='12.3km'):
    s = Screen(P['bg']); status_bar(s, P, 'SCAN', tone=''); bar(s, P, 0); zone_row(s, P, ch='CH 5/16')
    hero(s, P, '05', 'PSP-LODZ', '149.1500 MHz'); divs(s, P, EXPANDED_DIV2_Y); params_fm(s, P)
    if distance:
        kv(s, P, 2, PANEL_Y3, 'DIST', distance_text, P['mut'] if distance_text == '--' else P['fg'])
    emcom_rows(s, P, y1=EXPANDED_ROW_Y1, y2=EXPANDED_ROW_Y2, y3=EXPANDED_ROW_Y3)
    footer(s, P, 'ANY KEY: STOP', 'SCAN', P['scan']); return s


def main():
    shots = {
        'emcom_standby.png': screen_standby(distance=True),
        'emcom_rx.png': screen_rx(distance=True),
        'emcom_tx.png': screen_tx(),
        'emcom_dmr.png': screen_dmr(),
        'emcom_dmr_standby.png': screen_dmr_standby(),
        'emcom_vfo.png': screen_vfo(),
        'emcom_scan.png': screen_scan(distance=True),
        'emcom_scan_no_distance.png': screen_scan(distance=False),
        'emcom_sos.png': screen_sos(),
        'emcom_night.png': screen_standby(NIGHT),
        'emcom_standby_no_distance.png': screen_standby(distance=False),
        'emcom_rx_no_distance.png': screen_rx(distance=False),
        'emcom_rx_unavailable.png': screen_rx(distance=True, distance_text='--'),
        'emcom_dmr_short.png': screen_dmr_short(),
        'emcom_details.png': screen_details(),
    }
    for name, s in shots.items():
        s.png(os.path.join(HERE, name))
        print(name)


if __name__ == '__main__':
    main()
