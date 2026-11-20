#!/usr/bin/env python3
# Mockups of the APRS text screens (compose / edit a message, reply list in APRS RX,
# canned replies in Options > APRS), following uiTextEdit.c, menuAPRSRx.c and
# menuAPRSOptions.c. Example data.
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_screens as R
import render_menus as M
import render_more as X

P = R.DARK
FY = 128 - 10


def keybar(s, text):
    s.fill(0, FY, 160, 10, P['bar'])
    s.text(3, FY + 1, text, 1, P['barFg'])


def compose():
    # uiTextEditDraw(): title, length / limit, label, 8x16 text box (19 chars a line), cursor
    s = R.Screen(P['bg'])
    M.title_bar(s, P, 'APRS MESSAGE')
    text = 'Punkt 2 gotowy, QRV 145.500'
    s.text(157, 4, f'{len(text)}/67', 1, P['mut'], True)
    s.text(4, 16, 'TO SP7ABC-9', 1, P['info'])
    y = 26
    lines = (128 - 10 - 4 - y) // 16
    s.rect(0, y, 160, lines * 16 + 2, P['mut'])
    for l in range(lines):
        s.text(4, y + 1 + l * 16, text[l * 19:(l + 1) * 19], 3, P['fg'])
    pos = len(text)
    cx, cy = 4 + (pos % 19) * 8, y + 1 + (pos // 19) * 16
    s.fill(cx, cy + 14, 8, 2, P['info'])
    keybar(s, 'GRN OK  # DEL  RED BACK')
    return s


def reply_pick():
    s = R.Screen(P['bg'])
    X.aprs_strip(s, 'APRS MESSAGE', None)
    y = 27
    s.text(4, y, 'TO', 1, P['mut']); s.text(19, y, 'SP7ABC-9', 1, P['fg'])
    rows = ['> Type a message...', 'QSL', 'OK', '73', 'Roger', 'At the CP in 10 min', 'Yes', 'No', 'Call me']
    rh = 8
    n = (128 - 27 - 10 - 8 - 2) // rh
    for i, t in enumerate(rows[:n]):
        ry = y + (i + 1) * rh + 2
        on = i == 0
        if on:
            s.fill(0, ry, 160, 8, P['rx']); s.fill(0, ry + 2, 2, 4, P['bg'])
        s.text(4, ry, t, '1b', P['bg'] if on else P['fg'])
    keybar(s, 'GRN SEND  * TO  RED BACK')
    return s


def replies():
    s = R.Screen(P['bg'])
    M.title_bar(s, P, 'ODPOWIEDZI', '6/12')
    rows = [('QSL', False), ('OK', False), ('73', False), ('Roger', False), ('Thanks!', False),
            ('At the CP in 10 min', True), ('No', False), ('Call me', False), ('QRV?', False), ('QRX 5 min', False)]
    for i, (t, edited) in enumerate(rows[:(128 - 16 - 10) // 10]):
        y = 16 + i * 10
        on = i == 5
        if on:
            s.fill(0, y - 1, 160, 10, P['rx'])
        s.fill(2, y, 15, 8, P['info'] if edited else P['mut'])
        num = str(i + 1)
        s.text(2 + (15 - len(num) * 6) // 2, y, num, '1b', P['bg'])
        s.text(20, y, t[:23], '1b' if on else 1, P['bg'] if on else P['fg'])
    keybar(s, 'GRN EDIT 0 DFLT RED BACK')
    return s


if __name__ == '__main__':
    for name, fn in (('aprs_compose', compose), ('aprs_reply_pick', reply_pick), ('aprs_replies', replies)):
        fn().png(os.path.join(HERE, name + '.png'))
    print('ok')
