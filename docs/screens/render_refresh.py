#!/usr/bin/env python3
# UI refresh, stage 1: mock-ups of the TX view, message boxes / notifications and the APRS
# messages (list and read view) in the four EMCOM styles (day, night, mono, contrast),
# following uiEmcom.c (drawTxPanel / drawTxRows / emcomDrawAlert / emcomDrawLevelAlert) and
# menuAPRSRx.c (aprsMonDrawMessagesEmcom / aprsMonDrawMessageRead). Example data.
#
#   python3 docs/screens/render_refresh.py
#   (writes tx_styles.png, alerts_styles.png, aprs_msg_styles.png, aprs_message_read.png)
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_screens as R  # noqa: E402
import render_access as A  # noqa: E402

W, H = R.W, R.H
STYLES = (('day', R.DARK), ('night', R.NIGHT), ('mono', R.MONO), ('contrast', R.CONTRAST))


# --- drawing primitives missing in render_screens -------------------------------------------------
def tri(s, p0, p1, p2, c):
    xs = [p[0] for p in (p0, p1, p2)]
    ys = [p[1] for p in (p0, p1, p2)]

    def side(a, b, x, y):
        return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])
    for y in range(min(ys), max(ys) + 1):
        for x in range(min(xs), max(xs) + 1):
            d = (side(p0, p1, x, y), side(p1, p2, x, y), side(p2, p0, x, y))
            if all(v >= 0 for v in d) or all(v <= 0 for v in d):
                s.fill(x, y, 1, 1, c)


def disc(s, cx, cy, r, c):
    for y in range(-r, r + 1):
        for x in range(-r, r + 1):
            if x * x + y * y <= r * r + r:
                s.fill(cx + x, cy + y, 1, 1, c)


def f1(P):
    return R.f1(P)


def mmss(t):
    t = max(0, t)
    return f'{t // 3600}:{(t // 60) % 60:02d}:{t % 60:02d}' if t >= 3600 else f'{t // 60}:{t % 60:02d}'


# --- TX view (uiEmcom.c drawTxPanel / drawTxRows) -------------------------------------------------
def tx_screen(P, elapsed=12, total=180, digital=False, pwr='5W'):
    remaining = None if total is None else total - elapsed
    s = R.Screen(P['bg'])
    R.status_bar(s, P, 'TX', mode='DMR' if digital else 'FMN', tone='TS1' if digital else 'CTR')
    R.bar(s, P, 17, True)
    if digital:
        R.zone_row(s, P, 'DMR SR7', 'CH 7/16'); R.hero(s, P, '07', 'SR7L TG260', '439.4250 MHz TX', tx=True)
    else:
        R.zone_row(s, P); R.hero(s, P, freq='144.9000 MHz TX', tx=True)
    s.fill(0, R.DIV1_Y, W, 1, P['line'])
    # panel: elapsed time, power
    s.text(1, R.TX_TIMER_Y, mmss(elapsed), 4 if elapsed < 3600 else 3, P['tx'])
    s.text(W - 2, R.PANEL_Y1 - 1, 'PWR', f1(P), P['mut'], True)
    s.text(W - 2, R.PANEL_Y1 + 6, pwr, 3, P['fg'], True)
    s.fill(0, R.EXPANDED_DIV2_Y, W, 1, P['line'])
    # TOT bar
    y1, y2, y3 = R.EXPANDED_ROW_Y1, R.EXPANDED_ROW_Y2, R.EXPANDED_ROW_Y3
    s.text(2, y1, 'TOT', f1(P), P['mut'])
    bx, bw = 26, 86
    bh = 9 if P.get('large') else 7
    by = y1 - (1 if P.get('large') else 0)
    if remaining is None:
        s.text(bx, y1, 'off', f1(P), P['mut'])
    else:
        last = remaining <= R.TX_WARN_SECS
        span = min(R.TX_WARN_SECS, total) if last else total  # last seconds: a countdown of these only
        fw = (bw - 4) * max(0, min(remaining, span)) // span
        s.rect(bx, by, bw, bh, warn(P) if last else P['mut'])
        if last:
            X, Y = bx + 2, by + 2
            for j in range(bh - 4):
                for i in range(((Y + j) + X) % 3, fw, 3):
                    s.fill(X + i, Y + j, 1, 1, warn(P))
        else:
            s.fill(bx + 2, by + 2, fw, bh - 4, P['tx'])
        t = '-' + mmss(remaining)
        if last:
            if P.get('cues'):
                R.inverse_chip(s, P, W - 1, y1, t, warn(P), True, P['bg'])
            else:
                s.text(W - 2, y1, t, '1b', warn(P), True)
        else:
            s.text(W - 2, y1, t, f1(P), P['fg'], True)
    if digital:
        R.kv(s, P, 2, y2, 'TG', '260'); R.kv(s, P, 66, y2, 'CC', '1'); R.kv(s, P, 120, y2, 'TS', '1')
        s.text(2, y3, 'Poland', f1(P), P['fg']); s.text(W - 2, y3, 'SK1 LH', f1(P), P['mut'], True)
    else:
        R.kv(s, P, 2, y2, 'TXT', '88.5'); R.kv(s, P, 66, y2, 'BW', '12.5k')
        s.text(2, y3, 'SK2 1750  0-9 DTMF', f1(P), P['mut'])
    R.footer(s, P, right='TX', rc=P['tx'])
    return s


def aprs_tx_banner(P):
    s = tx_screen(P)
    s.fill(0, R.DIV1_Y + 1, W, R.EXPANDED_DIV2_Y - R.DIV1_Y - 1, P['tx'])
    t = 'APRS Tx'
    s.text((W - len(t) * 16) // 2, R.TX_TIMER_Y, t, 4, P['txFg'])
    return s


# --- alerts (uiEmcom.c emcomDrawAlert / emcomDrawLevelAlert) ---------------------------------------
AX, AW, STRIP, GLYPH = 2, W - 4, 12, 15
TEXT_X = AX + GLYPH + 8
TEXT_W = (AX + AW - 3) - TEXT_X
CHARS = TEXT_W // 8
WORDS = {'info': 'INFO', 'warning': 'WARNING', 'error': 'ERROR', 'question': 'CONFIRM'}


def warn(P):
    # uiEmcom.c warnColour(): at night the brighter TX red (except the contrast style)
    return P['tx'] if (P in (R.NIGHT, R.MONO_NIGHT)) else P['scan']


def kind_colour(P, kind):
    return {'warning': warn(P), 'error': P['tx'], 'question': P['fg']}.get(kind, P['info'])


def glyph(s, P, x, y, kind, c):
    bg = P['bg']
    if kind == 'warning':
        tri(s, (x + 7, y), (x, y + 14), (x + 14, y + 14), c)
        s.fill(x + 6, y + 5, 3, 5, bg); s.fill(x + 6, y + 11, 3, 2, bg)
    elif kind == 'error':
        s.fill(x + 1, y, 13, 15, c); s.fill(x, y + 1, 15, 13, c)
        for d in range(2):
            s.line(x + 4 + d, y + 4, x + 10, y + 10 - d, bg); s.line(x + 4, y + 10 - d, x + 10 - d, y + 4, bg)
    elif kind == 'question':
        tri(s, (x + 7, y), (x, y + 7), (x + 14, y + 7), c); tri(s, (x, y + 7), (x + 14, y + 7), (x + 7, y + 14), c)
        s.text(x + 5, y + 4, '?', '1b', bg)
    else:
        disc(s, x + 7, y + 7, 7, c)
        s.fill(x + 6, y + 3, 3, 2, bg); s.fill(x + 6, y + 6, 3, 6, bg)


MAIN_ROWS = (12, 17, R.DIV1_Y, R.COMPACT_DIV2_Y, R.COMPACT_ROW_Y2 - 1,
             R.COMPACT_ROW_Y3 - 1, R.COMPACT_ROW_Y3 + R.FONT_SIZE_1_HEIGHT + 1, R.FOOTER_Y)
EXPANDED_ROWS = (12, 17, R.DIV1_Y, R.EXPANDED_DIV2_Y, R.EXPANDED_ROW_Y2 - 1,
                 R.EXPANDED_ROW_Y3 - 1, R.EXPANDED_ROW_Y3 + R.FONT_SIZE_1_HEIGHT + 1, R.FOOTER_Y)


def assert_refresh_boundaries():
    assert MAIN_ROWS == tuple(sorted(MAIN_ROWS))
    assert EXPANDED_ROWS == tuple(sorted(EXPANDED_ROWS))
    assert R.EXPANDED_ROW_Y1 + R.CHIP_TOP_OFFSET + R.CHIP_HEIGHT <= R.EXPANDED_ROW_Y2
    assert R.EXPANDED_ROW_Y1 - 1 + 9 <= R.EXPANDED_ROW_Y2
    assert R.EXPANDED_ROW_Y3 + R.FONT_SIZE_1_HEIGHT + 1 <= R.FOOTER_Y


assert_refresh_boundaries()


def band(s, P, h, rows=MAIN_ROWS):
    # uiEmcom.c alertBand(): clear whole text rows of the main screen around the card, centre the card
    y = (H - h) // 2
    top = max([r for r in rows if r <= y - 2] or [0])
    bottom = min([r for r in rows if r >= y + h + 2] or [H])
    s.fill(0, top, W, bottom - top, P['bg'])
    return top + ((bottom - top) - h) // 2


def card(s, P, kind, title, y, h):
    c = kind_colour(P, kind)
    t = 2 if (P.get('large') or (P.get('cues') and kind in ('warning', 'error'))) else 1
    s.fill(AX - 2, y - 2, AW + 4, h + 4, P['bg']); s.fill(AX, y, AW, STRIP, c)
    s.fill(AX, y, t, h, c); s.fill(AX + AW - t, y, t, h, c); s.fill(AX, y + h - t, AW, t, c)
    s.text(AX + 4, y + 2, (title or WORDS[kind]).upper(), '1b', P['bg'])
    return c


def wrap(msg):
    lines = []
    for para in msg.split('\n'):
        p = para.lstrip(' ')
        while True:
            if len(p) <= CHARS:
                lines.append(p); break
            cut = p.rfind(' ', 0, CHARS + 1)
            cut = CHARS if cut <= 0 else cut
            lines.append(p[:cut]); p = p[cut:].lstrip(' ')
    return lines[:4]


def alert(P, kind, message, title=None, keys=None, full=False, under=None, rows=None):
    s = under if under is not None else R.Screen(P['bg'])
    lines = wrap(message)
    if full:
        bottom = H - (11 if keys else 0)
        s.fill(0, 0, W, H, P['bg'])
        y, h = 2, bottom - 4
        if keys:
            s.fill(0, bottom, W, 11, P['bar']); s.text(3, bottom + 2, keys, f1(P), P['barFg'])
    else:
        h = STRIP + max(1, len(lines)) * 16 + 6
        y = band(s, P, h, MAIN_ROWS if rows is None else rows)
    c = card(s, P, kind, title, y, h)
    by, bh = y + STRIP, h - STRIP - 2
    glyph(s, P, AX + 5, by + (bh - GLYPH) // 2, kind, c)
    ly = by + (bh - len(lines) * 16) // 2
    for ln in lines:
        s.text(TEXT_X + (TEXT_W - len(ln) * 8) // 2, ly, ln, 3, P['fg'])
        ly += 16
    return s


def level(P, title, value, percent, under, rows=None):
    s = under
    h = STRIP + 16 + 8
    y = band(s, P, h, MAIN_ROWS if rows is None else rows)
    ty = y + STRIP + 3
    card(s, P, 'info', title, y, h)
    if percent < 0:
        s.text((W - len(value) * 8) // 2, ty, value, 3, P['fg'])
    else:
        bx, bw, bh, by = AX + 6, 96, 12, ty + 2
        s.rect(bx, by, bw, bh, P['fg']); s.fill(bx + 2, by + 2, (bw - 4) * percent // 100, bh - 4, P['info'])
        s.text(AX + AW - 5, ty, value, 3, P['fg'], True)
    return s


def standby(P):
    return A.standby(P)


def standby_compact(P):
    # VFO RX remains a compact layout and is the compact notification underlay.
    return R.screen_vfo(P)


def standby_expanded(P):
    return R.screen_standby(P, distance=True)


# --- menu theme items (emcomThemePreset) for the APRS RX screen ------------------------------------
THEME = {
    'day': dict(fg=0xF2F5F0, bg=0x0B0E0C, deco=0x86928A, sel=0x3BD16F, green=0x3BD16F, amber=0xFFB000, red=0xFF3B30,
                blue=0x5AC8FA, hbg=0x18201B, hfg=0xE9EEE7),
    'mono': dict(fg=0xFFFFFF, bg=0x000000, deco=0x8A8A8A, sel=0xFFFFFF, green=0xDCDCDC, amber=0xA0A0A0, red=0xFFFFFF,
                 blue=0xBEBEBE, hbg=0x1E1E1E, hfg=0xF0F0F0),
    'contrast': dict(fg=0xFFFFFF, bg=0x000000, deco=0xFFFF00, sel=0xFFFF00, green=0xFFFFFF, amber=0xFFFF00, red=0xFFFF00,
                     blue=0xFFFFFF, hbg=0x202020, hfg=0xFFFFFF),
}
THEME['night'] = dict(fg=0xD22A2A, bg=0x000000, deco=0x7A1414, sel=0x8A1818, green=0xD22A2A, amber=0xD22A2A, red=0xD22A2A,
                      blue=0xD22A2A, hbg=0x120202, hfg=0xD22A2A)


def title(s, T, text, pos=None):
    s.fill(0, 0, W, 14, T['hbg']); s.fill(0, 0, 3, 13, T['green']); s.fill(0, 13, W, 1, T['deco'])
    s.text(7, 3, text.upper(), 2, T['hfg'])
    if pos:
        s.text(W - 3, 4, pos, 1, T['deco'], True)


def chipT(s, T, x, y, t, c):
    w = len(t) * 6 + 4
    s.fill(x, y - 1, w, 9, c); s.text(x + 2, y, t, '1b', T['bg'])
    return w


def strip(s, T, unread=0):
    s.fill(0, 14, W, 11, T['hbg'])
    x = 2 + chipT(s, T, 2, 16, 'RX', T['green']) + 4
    s.rect(x, 17, 22, 6, T['deco']); s.fill(x + 1, 18, 9, 4, T['green']); x += 26
    s.text(x, 16, 'OK42 E3', 1, T['hfg'])
    if unread:
        t = f'MSG {unread}'
        chipT(s, T, W - 2 - (len(t) * 6 + 4), 16, t, T['blue'])
    else:
        s.text(W - 2, 16, '-118dBm', 1, T['deco'], True)
    s.fill(0, 25, W, 1, T['deco'])


def keybar(s, T, text):
    s.fill(0, H - 10, W, 10, T['hbg']); s.text(3, H - 9, text, 1, T['hfg'])


MSGS = [  # kind, peer, dist, time, text, (state word, symbol, colour key)
    ('in', 'SP7ABC-9', '4.2km', '14:32', 'Punkt 2 gotowy, QRV na 145.500, prosze o potwierdzenie', None),
    ('out', 'SP7ABC-9', '4.2km', '14:30', 'QSL, czekam na punkcie 2', ('ACK', 'check', 'green')),
    ('out', 'SR7EOP', None, '14:21', 'Status punktu 3?', ('WAIT 2', 'wait', 'amber')),
    ('bln', 'SR7NJ', '12.3km', '14:05', 'Net RAYNET 20:00 kanal 3', None),
    ('out', 'SP7XYZ', None, '13:50', 'Test', ('NO ACK', 'cross', 'red')),
]


def symbol(s, x, y, sym, c):
    if sym == 'check':
        for d in range(2):
            s.line(x, y + 3 + d, x + 2, y + 5 + d, c); s.line(x + 2, y + 5 + d, x + 6, y + d, c)
    elif sym == 'wait':
        s.fill(x, y, 7, 1, c); s.fill(x, y + 6, 7, 1, c)
        tri(s, (x + 1, y + 1), (x + 5, y + 1), (x + 3, y + 3), c); tri(s, (x + 3, y + 3), (x + 1, y + 5), (x + 5, y + 5), c)
    elif sym == 'cross':
        for d in range(2):
            s.line(x + d, y, x + 6, y + 6 - d, c); s.line(x, y + 6 - d, x + 6 - d, y, c)


def dir_chip(s, T, x, y, kind, selected):
    word = {'in': 'IN', 'out': 'OUT', 'bln': 'BLN'}[kind]
    c = {'in': T['green'], 'out': T['blue'], 'bln': T['amber']}[kind]
    tx = x + (22 - len(word) * 6) // 2
    if selected:
        s.rect(x, y - 1, 22, 9, T['bg']); s.text(tx, y, word, '1b', T['bg'])
    else:
        s.fill(x, y - 1, 22, 9, c); s.text(tx, y, word, '1b', T['bg'])


def aprs_list(T, sel=0):
    s = R.Screen(T['bg'])
    title(s, T, 'APRS messages', '4/6'); strip(s, T)
    for r in range(4):
        kind, peer, dist, tm, text, st = MSGS[r]
        y = 27 + r * 22
        on = r == sel
        tc = T['bg'] if on else T['fg']
        mc = T['bg'] if on else T['deco']
        if on:
            s.fill(0, y, W, 21, T['sel'])
        dir_chip(s, T, 2, y + 3, kind, on)
        s.text(27, y + 3, peer[:9], '1b', tc)
        right = W - 2 - len(tm) * 6
        s.text(right, y + 3, tm, 1, tc)
        if dist:
            right -= len(dist) * 6 + 6
            s.text(right, y + 3, dist, 1, mc)
        x = 4
        if st:
            word, sym, ck = st
            c = T['bg'] if on else T[ck]
            symbol(s, x, y + 13, sym, c); x += 9
            s.text(x, y + 12, word, '1b', c); x += len(word) * 6 + 5
        mx = (W - 2 - x) // 6
        t = text if len(text) <= mx else text[:mx - 2] + '..'
        s.text(x, y + 12, t, 1, tc)
        if not on:
            s.fill(0, y + 21, W, 1, T['deco'])
    keybar(s, T, '* READ  GRN REPLY  1 NEW')
    return s


def wrap_words(text, per):
    out, p = [], text
    while p:
        if len(p) <= per:
            out.append(p); break
        cut = p.rfind(' ', 0, per + 1)
        cut = per if cut <= 0 else cut
        out.append(p[:cut]); p = p[cut:].lstrip(' ')
    return out


def aprs_read(T, idx=0, contrast=False):
    kind, peer, dist, tm, text, st = MSGS[idx]
    s = R.Screen(T['bg'])
    title(s, T, 'APRS message', f'{idx + 1}/5')
    hy = 16
    dir_chip(s, T, 2, hy + 4, kind, False)
    s.text(28, hy - 1, peer[:9], 3, T['fg'])
    if st:
        word, sym, ck = st
        w = len(word) * 6
        s.text(W - 2 - w, hy + 4, word, '1b', T[ck]); symbol(s, W - 2 - w - 9, hy + 5, sym, T[ck])
    iy = hy + 16
    x = 2
    x += s.text(x, iy, 'AT', 1, T['deco']) + 3; x += s.text(x, iy, tm + 'Z', 1, T['fg']) + 8
    if dist:
        x += s.text(x, iy, 'DST', 1, T['deco']) + 3; s.text(x, iy, f'{dist} 045', 1, T['fg'])
    s.fill(0, iy + 10, W, 1, T['deco'])
    ty = iy + 13
    room = (H - 10 - 1) - ty
    big = wrap_words(text, (W - 6) // 8)
    if len(big) <= room // 16:
        for i, ln in enumerate(big):
            s.text(4, ty + i * 16, ln, 3, T['fg'])
    else:
        for i, ln in enumerate(wrap_words(text, (W - 6) // 6)[:room // 9]):
            s.text(4, ty + i * 9, ln, '1b' if contrast else 1, T['fg'])
    keybar(s, T, 'UP/DN NEXT  RED BACK' if kind == 'bln' else 'GRN REPLY UP/DN  RED BACK')
    return s


def main():
    tx_panels, alert_panels, aprs_panels, notification_panels = [], [], [], []
    for name, P in STYLES + (('mono night', R.MONO_NIGHT), ('contrast night', R.CONTRAST_NIGHT)):
        if name.endswith(' night'):
            alert_panels += [alert(P, 'warning', 'APRS msg: no ACK', under=standby(P), rows=EXPANDED_ROWS), level(P, 'Squelch', '45%', 45, standby(P), rows=EXPANDED_ROWS),
                             alert(P, 'error', 'Out of band', title='TX blocked', full=True),
                             alert(P, 'info', 'SP7ABC-9:Punkt 2 gotowy, QRV 145.500', under=standby(P), rows=EXPANDED_ROWS), tx_screen(P, elapsed=172)]
            continue
        tx_panels += [tx_screen(P), tx_screen(P, elapsed=172), tx_screen(P, elapsed=754, total=None, digital=True)]
        alert_panels += [
            alert(P, 'question', 'Private call\nAccept call?\nSP5ABC Jan', keys='GRN YES     RED NO', full=True),
            alert(P, 'warning', 'APRS msg: no ACK', under=standby(P), rows=EXPANDED_ROWS),
            alert(P, 'error', 'Out of band', title='TX blocked', full=True),
            level(P, 'Squelch', '45%', 45, standby(P), rows=EXPANDED_ROWS),
            alert(P, 'info', 'SP7ABC-9:Punkt 2 gotowy, QRV 145.500', under=standby(P), rows=EXPANDED_ROWS),
        ]
        notification_panels += [
            alert(P, 'warning', 'APRS msg: no ACK', under=standby_compact(P), rows=MAIN_ROWS),
            alert(P, 'warning', 'APRS msg: no ACK', under=standby_expanded(P), rows=EXPANDED_ROWS),
        ]
    for name, _ in STYLES:
        T = THEME[name]
        aprs_panels += [aprs_list(T), aprs_read(T, 0, name == 'contrast'), aprs_read(T, 2, name == 'contrast')]
    A.sheet(os.path.join(HERE, 'tx_styles.png'), tx_panels, cols=3)
    A.sheet(os.path.join(HERE, 'alerts_styles.png'), alert_panels, cols=5)
    A.sheet(os.path.join(HERE, 'notifications_layouts.png'), notification_panels, cols=4)
    A.sheet(os.path.join(HERE, 'aprs_msg_styles.png'), aprs_panels, cols=3)
    aprs_read(THEME['day']).png(os.path.join(HERE, 'aprs_message_read.png'))
    tx_screen(R.DARK, elapsed=12, digital=False).png(os.path.join(HERE, 'emcom_tx_fm.png'))
    tx_screen(R.DARK, elapsed=172, total=180, digital=False).png(os.path.join(HERE, 'emcom_tot_warning.png'))
    tx_screen(R.DARK, elapsed=754, total=None, digital=True).png(os.path.join(HERE, 'emcom_tx_dmr.png'))
    aprs_tx_banner(R.DARK).png(os.path.join(HERE, 'emcom_aprs_tx.png'))
    print('tx_styles.png alerts_styles.png notifications_layouts.png aprs_msg_styles.png aprs_message_read.png '
          'emcom_tx_fm.png emcom_tot_warning.png emcom_tx_dmr.png emcom_aprs_tx.png')


if __name__ == '__main__':
    main()
