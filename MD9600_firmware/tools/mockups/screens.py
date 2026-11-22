import math, os, sys
from lcd import LCD, F1, F1B, F2, F3, W, H

OUT = os.path.join(os.path.dirname(__file__), 'out')
os.makedirs(OUT, exist_ok=True)
SCREENS = []


def screen(name):
    def deco(fn):
        SCREENS.append((name, fn))
        return fn
    return deco


# 7x7 header icons
SAT = ['..###..', '.#...#.', '#..#..#', '#.###.#', '#..#..#', '.#...#.', '..###..']
PLUG = ['.#.#...', '.#.#...', '#####..', '#####..', '.###...', '..#....', '..#....']
LOCK = ['.###...', '#...#..', '#...#..', '#####..', '##.##..', '#####..', '.......']


def status_header(d, left, gps='9', volt='13.8V', locked=False):
    d.fill(0, 0, W, 9)
    d.text(1, 1, left, F1B, inv=True)
    x = W - 1 - len(volt) * 6
    d.text(x, 1, volt, F1, inv=True)
    x -= 8
    d.icon(x, 1, [r.replace('#', 'o') for r in PLUG])
    if gps:
        x -= 6 + 1 + len(gps) * 6
        d.icon(x, 1, [r.replace('#', 'o') for r in SAT])
        d.text(x + 8, 1, gps, F1, inv=True)
    if locked:
        x -= 7
        d.icon(x, 1, [r.replace('#', 'o') for r in LOCK])


# ---------------------------------------------------------------- main screens
@screen('main_channel')
def main_channel(d):
    status_header(d, 'FM 25W A')
    d.text(0, 10, 'RAYNET LODZ', F1)
    d.text(0, 10, '3/16', F1, align='r')
    d.text(0, 19, 'RAYNET-1', F3)
    d.text(0, 36, '145.5000', F1B)
    d.text(54, 36, '-0.6', F1)
    d.text(0, 36, 'T88.5', F1, align='r')
    d.dotted(0, 45, W)
    d.text(0, 47, 'LH SP7ABC-9 4.2km 2m', F1)
    d.text(0, 56, '14:32Z', F1)
    d.text(0, 56, 'APRS 14  JO91RS', F1, align='r')


@screen('main_vfo')
def main_vfo(d):
    status_header(d, 'FM 5W VFO')
    d.text(0, 10, 'VFO A  12.5k', F1)
    d.text(0, 10, 'SQL 20', F1, align='r')
    d.text(0, 19, '145.5000', F3)
    d.text(66, 27, 'MHz', F1)
    d.text(0, 36, 'TX 145.5000', F1)
    d.text(0, 36, 'TOT 3m', F1, align='r')
    d.dotted(0, 45, W)
    d.text(0, 47, 'RXT 88.5  TXT 88.5', F1)
    d.text(0, 56, '14:32Z', F1)
    d.text(0, 56, 'APRS 14  JO91RS', F1, align='r')


@screen('main_dmr')
def main_dmr(d):
    status_header(d, 'DMR TS1 CC1')
    d.text(0, 10, 'SR7TM DMR', F1)
    d.text(0, 10, '5/16', F1, align='r')
    d.text(0, 19, 'TG 260', F3)
    d.text(58, 27, 'Polska', F1)
    d.text(0, 36, '438.5250', F1B)
    d.text(54, 36, '-7.6', F1)
    d.text(0, 36, 'TA ON', F1, align='r')
    d.dotted(0, 45, W)
    d.text(0, 47, 'LH SP7ABC Lodz 3m', F1)
    d.text(0, 56, '14:32Z', F1)
    d.text(0, 56, 'APRS 14  JO91RS', F1, align='r')


@screen('rx_dmr')
def rx_dmr(d):
    status_header(d, 'RX TS1 TG260')
    d.fill(0, 10, W, 20)
    d.text(0, 12, 'SP7ABC', F3, align='c', inv=True)
    d.text(0, 32, 'Marcin', F1B, align='c')
    d.text(0, 41, 'Lodz  4.2km 270', F1, align='c')
    d.text(0, 51, 'S9+10', F1)
    d.bar(34, 51, 94, 7, 0.82, ticks=6)


@screen('rx_fm')
def rx_fm(d):
    status_header(d, 'RX FM 25W A')
    d.text(0, 10, 'RAYNET LODZ', F1)
    d.text(0, 10, '3/16', F1, align='r')
    d.text(0, 19, 'RAYNET-1', F3)
    d.text(0, 36, '145.5000', F1B)
    d.text(0, 36, 'CTC 88.5', F1, align='r')
    d.dotted(0, 45, W)
    d.text(0, 47, 'S7', F1B)
    d.bar(20, 47, 108, 7, 0.62, ticks=6)
    d.text(0, 56, 'RX 0:08', F1)
    d.text(0, 56, '-81dBm', F1, align='r')


@screen('tx')
def tx(d):
    d.fill(0, 0, W, 30)
    d.text(2, 3, 'TX', F3, inv=True)
    d.text(24, 3, '0:12', F3, inv=True)
    d.text(0, 3, '25W', F1B, align='r', inv=True, right=W - 2)
    d.text(0, 13, 'FM', F1, align='r', inv=True, right=W - 2)
    d.text(2, 21, 'RAYNET-1  145.5000', F1, inv=True)
    d.text(0, 34, 'TOT', F1)
    d.bar(22, 34, 106, 7, 12 / 180)
    d.text(0, 34, '', F1)
    d.text(0, 45, '2:48 left', F1)
    d.text(0, 45, 'T88.5', F1, align='r')
    d.dotted(0, 54, W)
    d.text(0, 56, 'MIC 13.6V  SWR ok', F1)


@screen('alert_card')
def alert_card(d):
    main_channel(d)
    d.fill(0, 9, W, 55, 0)
    d.rect(2, 11, 124, 51)
    d.rect(3, 12, 122, 49)
    d.fill(4, 13, 120, 11)
    d.text(0, 15, '! LOW SUPPLY', F1B, align='c', inv=True)
    d.text(0, 27, '11.2V', F3, align='c')
    d.text(0, 47, 'TX power cut to 5W', F1, align='c')


@screen('notification')
def notification(d):
    main_channel(d)
    d.fill(0, 44, W, 20, 0)
    d.hline(0, 44, W)
    d.fill(0, 46, W, 18)
    d.text(2, 47, 'MSG SP7ABC-9', F1B, inv=True)
    d.text(2, 56, 'Jestem na punkcie 3', F1, inv=True)


# ---------------------------------------------------------------- menu
ICONS = {
    'zones': ['................', '......####......', '...###....###...', '.##..........##.', '..###......###..', '.....######.....',
              '.##..........##.', '...###....###...', '......####......', '................', '................', '................',
              '................', '................', '................', '................'],
}


def tile_icon(d, x, y, kind):
    # simple 12x12 pictograms drawn with primitives
    if kind == 'contacts':
        d.circle(x + 6, y + 3, 3); d.fill(x + 1, y + 8, 11, 4)
    elif kind == 'zones':
        d.rect(x + 2, y + 1, 10, 4); d.rect(x + 1, y + 5, 10, 4); d.rect(x, y + 9, 10, 3)
    elif kind == 'gps':
        d.circle(x + 6, y + 6, 5); d.set(x + 6, y + 6); d.hline(x, y + 6, 3); d.hline(x + 10, y + 6, 3); d.vline(x + 6, y, 3); d.vline(x + 6, y + 10, 3)
    elif kind == 'aprs':
        d.vline(x + 6, y + 3, 9); d.circle(x + 6, y + 3, 2)
        for i in range(0, 13, 3):
            d.set(x + i, y + 11)
        d.line(x + 1, y + 1, x + 3, y + 5); d.line(x + 11, y + 1, x + 9, y + 5)
    elif kind == 'net':
        for cx in (2, 6, 10):
            d.circle(x + cx, y + 3, 1)
            d.fill(x + cx - 2, y + 7, 5, 4)
    elif kind == 'pace':
        for i, w in enumerate((12, 9, 6, 3)):
            d.fill(x, y + i * 3, w, 2)
    elif kind == 'log':
        d.rect(x + 1, y, 10, 12)
        for i in range(3):
            d.hline(x + 3, y + 3 + i * 3, 6)
    elif kind == 'sat':
        d.fill(x + 4, y + 4, 4, 4); d.rect(x, y + 5, 3, 3); d.rect(x + 9, y + 4, 3, 3); d.line(x + 2, y + 10, x + 9, y + 1)
    elif kind == 'check':
        d.rect(x, y + 1, 12, 9); d.line(x + 3, y + 5, x + 5, y + 7); d.line(x + 5, y + 7, x + 9, y + 3); d.line(x + 3, y + 10, x + 1, y + 12)
    elif kind == 'wpt':
        d.circle(x + 6, y + 4, 4); d.set(x + 6, y + 4); d.line(x + 2, y + 6, x + 6, y + 12); d.line(x + 10, y + 6, x + 6, y + 12)
    elif kind == 'sked':
        d.circle(x + 6, y + 6, 5); d.vline(x + 6, y + 3, 4); d.hline(x + 6, y + 6, 3)
    elif kind == 'cov':
        for i in range(4):
            d.fill(x + i * 3, y + 12 - (i + 1) * 3, 2, (i + 1) * 3)
    elif kind == 'settings':
        d.circle(x + 6, y + 6, 4); d.circle(x + 6, y + 6, 1)
        for a in range(0, 360, 45):
            d.set(x + 6 + round(6 * math.cos(math.radians(a))), y + 6 + round(6 * math.sin(math.radians(a))))
    elif kind == 'lh':
        d.vline(x + 1, y, 12); d.hline(x + 1, y + 11, 11)
        d.line(x + 2, y + 9, x + 5, y + 4); d.line(x + 5, y + 4, x + 8, y + 7); d.line(x + 8, y + 7, x + 11, y + 1)


MENU = [('1', 'Zones', 'zones'), ('2', 'Contacts', 'contacts'), ('3', 'Last heard', 'lh'), ('4', 'GPS', 'gps'),
        ('5', 'APRS', 'aprs'), ('6', 'Satellites', 'sat'), ('7', 'PACE plan', 'pace'), ('8', 'Net control', 'net'),
        ('9', 'Radio check', 'check'), ('0', 'Event log', 'log'), ('', 'Waypoints', 'wpt'), ('', 'Sked', 'sked')]


@screen('menu_tiles')
def menu_tiles(d):
    d.header('MENU', '7/18')
    sel = 6
    for i, (num, label, ic) in enumerate(MENU[:8]):
        col, row = i % 4, i // 4
        x, y = col * 32, 10 + row * 22
        if i == sel:
            d.fill(x, y, 31, 21)
        else:
            d.rect(x, y, 31, 21)
        if num:
            d.text(x + 2, y + 1, num, F1, inv=(i == sel))
        sub = LCD(); tile_icon(sub, 0, 0, ic)
        for yy in range(13):
            for xx in range(13):
                if sub.get(xx, yy):
                    d.set(x + 12 + xx, y + 5 + yy, 0 if i == sel else 1)
    d.text(0, 56, MENU[sel][1], F1B)
    d.text(0, 56, '1-0 jump', F1, align='r')


@screen('menu_list')
def menu_list(d):
    d.header('MENU', '7/18')
    sel = 2
    for i, (num, label, ic) in enumerate(MENU[4:9]):
        y = 10 + i * 9
        if i == sel:
            d.fill(0, y, W - 4, 9)
        d.text(2, y + 1, (num or ' ') + ' ' + label, F1, inv=(i == sel))
    # scrollbar
    d.vline(W - 2, 10, 45)
    d.fill(W - 3, 10 + int(45 * 4 / 18), 3, 12)
    d.dotted(0, 55, W - 4)
    d.text(0, 56, 'GRN open  RED back', F1)


# ---------------------------------------------------------------- GPS / position
@screen('gps_status')
def gps_status(d):
    d.header('GPS', '3D FIX')
    rows = [('SAT', '9/14   HDOP 0.9'), ('LAT', '51.75920 N'), ('LON', '19.45603 E'), ('ALT', '212m   0 km/h'), ('UTC', '14:32:05 TTFF34s')]
    for i, (k, v) in enumerate(rows):
        d.text(0, 10 + i * 9, k, F1B)
        d.text(24, 10 + i * 9, v, F1)
    d.keybar('* sky   # signal')


@screen('gps_signal')
def gps_signal(d):
    d.header('GPS SIGNAL', '9 used')
    sats = [(5, 42, 1), (7, 38, 1), (9, 45, 1), (13, 31, 1), (16, 22, 0), (20, 40, 1), (27, 35, 1), (30, 18, 0)]
    for i, (prn, snr, used) in enumerate(sats):
        x = i * 16
        h = int(snr * 34 / 50)
        if used:
            d.fill(x + 2, 46 - h, 11, h)
        else:
            d.rect(x + 2, 46 - h, 11, h)
        d.text(x + 2, 48, '%02d' % prn, F1)
    d.hline(0, 46, W)
    d.text(0, 56, 'solid = in fix', F1)
    d.text(0, 56, 'dB 50', F1, align='r')


@screen('gps_sky')
def gps_sky(d):
    d.header('GPS SKY', '3D')
    cx, cy, r = 27, 36, 26
    d.circle(cx, cy, r)
    d.circle(cx, cy, r // 2, dotted=True)
    d.set(cx, cy)
    d.text(cx - 2, 10, 'N', F1) if False else None
    sats = [(40, 60, 1), (110, 30, 1), (200, 70, 1), (250, 20, 0), (300, 45, 1), (10, 15, 1), (160, 50, 1)]
    for az, el, used in sats:
        rr = r * (90 - el) / 90
        x = cx + round(rr * math.sin(math.radians(az)))
        y = cy - round(rr * math.cos(math.radians(az)))
        if used:
            d.fill(x - 1, y - 1, 3, 3)
        else:
            d.rect(x - 1, y - 1, 3, 3); d.set(x, y, 0)
    d.fill(cx - 1, cy - r - 1, 3, 3, 0)
    d.text(cx - 2, cy - r - 1, 'N', F1) if False else None
    d.text(60, 12, 'N up', F1)
    d.text(60, 22, 'used  7', F1)
    d.text(60, 31, 'seen 14', F1)
    d.text(60, 40, 'HDOP 0.9', F1)
    d.text(60, 49, 'TTFF 34s', F1)


@screen('position')
def position(d):
    d.header('POSITION', 'MGRS')
    d.text(0, 11, '34U CC', F3)
    d.text(0, 28, '0712 4583', F3)
    d.dotted(0, 46, W)
    d.text(0, 48, 'JO91RS  51.7592N', F1)
    d.text(0, 56, '        19.4560E', F1)
    d.text(0, 56, '+-4m', F1)


@screen('waypoint_nav')
def waypoint_nav(d):
    d.header('WPT 3/8', 'BASE')
    d.text(0, 12, '4.2', F3)
    d.text(26, 20, 'km', F1)
    d.text(0, 30, 'BRG 270 W', F1B)
    d.text(0, 39, 'HDG 245', F1)
    d.text(0, 48, 'ETA 6m @40', F1)
    cx, cy, r = 104, 33, 21
    d.circle(cx, cy, r)
    d.text(cx - 2, cy - r + 2, 'N', F1)
    a = math.radians(270 - 245)
    tipx, tipy = cx + round((r - 4) * math.sin(a)), cy - round((r - 4) * math.cos(a))
    d.line(cx, cy, tipx, tipy)
    for s in (-0.4, 0.4):
        d.line(tipx, tipy, tipx - round(7 * math.sin(a + s)), tipy + round(7 * math.cos(a + s)))
    d.keybar('GRN save  1-8 pick')


# ---------------------------------------------------------------- APRS
@screen('aprs_list')
def aprs_list(d):
    d.header('APRS', '14 stn')
    rows = [('SP7ABC-9', '4.2', '2m', '>'), ('SR7TM', '11', '5m', 'R'), ('SP7XYZ-7', '0.8', '6m', '>'), ('SQ7KLM-5', '23', '12m', '-'), ('SP5ABC-15', '41', '16m', '>')]
    for i, (c, km, age, sym) in enumerate(rows):
        y = 10 + i * 9
        inv = i == 1
        if inv:
            d.fill(0, y, W, 9)
        d.text(1, y + 1, c, F1, inv=inv)
        d.text(66, y + 1, km.rjust(3), F1, inv=inv)
        d.text(0, y + 1, age, F1, align='r', inv=inv, right=W - 1)
    d.keybar('GRN info * msgs # map')


@screen('aprs_msg')
def aprs_msg(d):
    d.header('MSG 2/5', '14:28')
    d.text(0, 10, 'SP7ABC-9 > SP7TQ', F1B)
    for i, line in enumerate(['Jestem na punkcie 3,', 'potrzebna woda i', 'apteczka. Odbior.']):
        d.text(0, 20 + i * 9, line, F1)
    d.text(0, 47, 'ack sent', F1)
    d.keybar('GRN reply  * canned')


# ---------------------------------------------------------------- field tools
@screen('net_control')
def net_control(d):
    d.header('NET', 'RUN 0:42 Q10')
    rows = [('SP7ABC', '14:02', '1m', 'IN', True), ('SP7XYZ-9', '14:03', '12m', 'QUIET', False),
            ('2601234', '14:05', '40s', 'IN', True), ('FM#1', '14:06', '3m', 'IN', True), ('SP5ABC', '14:08', '16m', 'QUIET', False)]
    for i, (c, t, age, st, solid) in enumerate(rows):
        y = 10 + i * 9
        d.text(0, y + 1, c, F1B if i == 1 else F1)
        d.text(56, y + 1, age.rjust(3), F1)
        w = len(st) * 6 + 3
        if solid:
            d.fill(W - w, y + 1, w, 8); d.text(W - w + 2, y + 1, st, F1, inv=True)
        else:
            d.rect(W - w, y + 1, w, 8); d.text(W - w + 2, y + 1, st, F1)
    d.rect(0, 19, 52, 9) if False else None
    d.keybar('GRN QSO *FM #Q 0END')


@screen('pace')
def pace(d):
    d.header('PACE', 'active P')
    rows = [('P', 'RAYNET-1  145.500', True), ('A', 'TG 260 DMR', False), ('C', 'APRS msg SR7TM', False), ('E', 'Phone 112', False)]
    for i, (k, v, act) in enumerate(rows):
        y = 10 + i * 11
        if act:
            d.fill(0, y, 11, 10)
            d.text(3, y + 1, k, F1B, inv=True)
        else:
            d.rect(0, y, 11, 10)
            d.text(3, y + 1, k, F1B)
        d.text(15, y + 1, v, F1)
    d.keybar('GRN go  -> next step')


@screen('radio_check')
def radio_check(d):
    d.header('RADIO CHECK')
    d.text(0, 11, '5/9', F3)
    d.text(34, 19, 'loud+clear', F1)
    d.text(0, 30, 'SP7ABC-9  14:32', F1B)
    d.text(0, 39, 'S9 -73dBm  4.2km', F1)
    d.bar(0, 48, W, 6, 0.9, ticks=6)
    d.keybar('1-5 R  6-9 S  GRN log')


@screen('event_log')
def event_log(d):
    d.header('EVENT LOG', '12/48')
    rows = [('14:32', 'QSO SP7ABC-9 59'), ('14:30', 'NET start Q10'), ('14:21', 'GPS fix 34s'), ('14:05', 'TX 145.500 0:42'), ('13:58', 'SUPPLY 11.2V')]
    for i, (t, e) in enumerate(rows):
        y = 10 + i * 9
        d.text(0, y + 1, t, F1B if i == 0 else F1)
        d.text(34, y + 1, e, F1)
    d.keybar('GRN mark  # export')


@screen('coverage')
def coverage(d):
    d.header('COVERAGE', '23 pts')
    # sparkline of S-level along the route
    vals = [3, 4, 5, 5, 6, 7, 7, 8, 9, 9, 8, 7, 6, 6, 5, 4, 4, 5, 7, 8, 9, 9, 9]
    for i, v in enumerate(vals):
        x = i * 5 + 4
        h = v * 3
        d.fill(x, 40 - h, 4, h)
    d.hline(0, 41, W)
    d.text(0, 43, 'now S9  best S9+10', F1)
    d.keybar('GRN point  # export')


@screen('sked')
def sked(d):
    d.header('SKED', '14:47Z')
    d.text(0, 11, '12:47', F3)
    d.text(44, 19, 'to 15:00Z', F1)
    d.text(0, 29, 'RAYNET-1 net', F1B)
    d.dotted(0, 38, W)
    d.text(0, 40, '18:00 SR7TM DMR', F1)
    d.text(0, 48, '21:00 HF 3.710', F1)
    d.keybar('GRN edit  * beep')


@screen('supply')
def supply(d):
    d.header('SUPPLY', '60 min')
    d.text(0, 11, '13.8', F3)
    d.text(34, 19, 'V', F1B)
    d.text(0, 11, 'min 12.1', F1, align='r')
    d.text(0, 20, 'max 14.2', F1, align='r')
    pts = [13.6, 13.7, 13.8, 13.8, 13.2, 12.4, 12.1, 12.6, 13.4, 13.8, 13.9, 14.1, 14.2, 13.9, 13.8, 13.8]
    lo, hi = 11.0, 14.5
    prev = None
    for i, v in enumerate(pts):
        x = i * 8 + 4
        y = 52 - round((v - lo) / (hi - lo) * 20)
        if prev:
            d.line(prev[0], prev[1], x, y)
        prev = (x, y)
    y_alarm = 52 - round((11.5 - lo) / (hi - lo) * 20)
    d.dotted(0, y_alarm, W)
    d.text(0, 56, 'TX 13.1V  alarm 11.5', F1)


@screen('sos')
def sos(d):
    d.fill(0, 0, W, 30)
    d.text(4, 3, 'SOS', F3, inv=True)
    d.text(0, 3, 'TX in 0:48', F1, align='r', inv=True, right=W - 3)
    d.text(0, 12, 'every 60s', F1, align='r', inv=True, right=W - 3)
    d.text(4, 21, 'RAYNET-1 + APRS', F1, inv=True)
    d.text(0, 33, '34U CC 0712 4583', F1B)
    d.text(0, 42, 'JO91RS  +-4m', F1)
    d.keybar('hold ALM 3s = stop')


@screen('blackout')
def blackout(d):
    d.fill(30, 22, 68, 20, 0)
    d.rect(30, 22, 68, 20)
    d.text(0, 25, 'BLACKOUT', F1B, align='c')
    d.text(0, 33, 'P1+5 exit', F1, align='c')


@screen('satellites')
def satellites(d):
    d.header('SATELLITES', '14:32Z')
    rows = [('ISS', 'AOS 14:52', '62'), ('SO-50', 'AOS 15:31', '18'), ('AO-91', 'LOS 14:39', '24'), ('RS-44', 'AOS 16:05', '41')]
    for i, (n, t, el) in enumerate(rows):
        y = 10 + i * 9
        inv = i == 0
        if inv:
            d.fill(0, y, W, 9)
        d.text(1, y + 1, n, F1, inv=inv)
        d.text(40, y + 1, t, F1, inv=inv)
        d.text(0, y + 1, el + 'd', F1, align='r', inv=inv, right=W - 1)
    d.keybar('GRN pass  # TLE age')


@screen('sat_pass')
def sat_pass(d):
    d.header('ISS', 'AOS 20:11')
    d.text(0, 11, 'AZ 214', F1B)
    d.text(0, 20, 'EL  37', F1B)
    d.text(0, 30, 'RX 437.8023', F1)
    d.text(0, 39, 'TX 145.9897', F1)
    d.text(0, 48, 'Dopp -2.3k', F1)
    cx, cy, r = 101, 33, 22
    d.circle(cx, cy, r)
    d.circle(cx, cy, r // 2, dotted=True)
    prev = None
    for t in range(0, 11):
        az = math.radians(150 + t * 14)
        el = 62 * math.sin(math.pi * t / 10)
        rr = r * (90 - el) / 90
        x, y = cx + round(rr * math.sin(az)), cy - round(rr * math.cos(az))
        if prev:
            d.line(prev[0], prev[1], x, y)
        prev = (x, y)
    az = math.radians(214); rr = r * (90 - 37) / 90
    x, y = cx + round(rr * math.sin(az)), cy - round(rr * math.cos(az))
    d.fill(x - 2, y - 2, 5, 5)
    d.keybar('GRN tune  * beacon')


@screen('last_heard')
def last_heard(d):
    d.header('LAST HEARD', '6')
    rows = [('SP7ABC', 'TG260', '1m'), ('SQ7KLM', 'TG2602', '4m'), ('SP7XYZ', 'FM', '9m'), ('SP5ABC', 'TG260', '21m'), ('SR7TM', 'TG91', '1h')]
    for i, (c, tg, age) in enumerate(rows):
        y = 10 + i * 9
        inv = i == 0
        if inv:
            d.fill(0, y, W, 9)
        d.text(1, y + 1, c, F1, inv=inv)
        d.text(48, y + 1, tg, F1, inv=inv)
        d.text(0, y + 1, age, F1, align='r', inv=inv, right=W - 1)
    d.keybar('GRN call  * QSO log')


@screen('quickkeys')
def quickkeys(d):
    d.header('FRONT KEYS')
    rows = [('P1', 'Blackout'), ('P2', 'Position'), ('P3', 'Radio check'), ('P4', 'PACE next'), ('ALM', 'SOS / alert')]
    for i, (k, v) in enumerate(rows):
        y = 10 + i * 9
        d.text(0, y + 1, k, F1B)
        d.text(26, y + 1, v, F1)
    d.keybar('GRN change  RED back')


@screen('display_style')
def display_style(d):
    d.header('DISPLAY', '')
    rows = [('Style', 'EMCOM'), ('Contrast', '12'), ('Backlight', '70%'), ('Night', 'auto'), ('Big text', 'off')]
    for i, (k, v) in enumerate(rows):
        y = 10 + i * 9
        inv = i == 0
        if inv:
            d.fill(0, y, W, 9)
        d.text(1, y + 1, k, F1, inv=inv)
        d.text(0, y + 1, v, F1B if inv else F1, align='r', inv=inv, right=W - 2)
    d.keybar('<> change  GRN ok')


@screen('boot')
def boot(d):
    d.rect(0, 0, W, H)
    d.text(0, 8, 'SP7TEAM', F3, align='c')
    d.text(0, 26, 'EMCOM', F1B, align='c')
    d.hline(30, 36, 68)
    d.text(0, 40, 'MD-9600 V5', F1, align='c')
    d.text(0, 50, 'R20260131-SP7TQ.6', F1, align='c')


def main():
    for name, fn in SCREENS:
        d = LCD(); fn(d)
        d.png(os.path.join(OUT, name + '.png'))
        if name in ('main_channel', 'rx_dmr', 'menu_tiles'):
            d.png(os.path.join(OUT, name + '_night.png'), night=True)
    print(len(SCREENS), 'screens')


if __name__ == '__main__':
    main()
