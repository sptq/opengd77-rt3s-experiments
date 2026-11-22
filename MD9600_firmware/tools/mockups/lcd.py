"""128x64 mono LCD simulator using the real ST7567 fonts from the MD-9600 firmware."""
import os
import re
from PIL import Image

CHARSET = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'application', 'include', 'hardware', 'ST7567_charset.h')
W, H = 128, 64


def _load_fonts():
    src = open(CHARSET, encoding='latin-1').read()
    fonts = {}
    for name in ('font_6x8', 'font_6x8_bold', 'font_8x8', 'font_8x16'):
        m = re.search(r'const uint8_t ' + name + r'\[\] = \{(.*?)\};', src, re.S)
        body = re.sub(r'//[^\n]*', '', m.group(1))
        nums = [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', body)]
        fonts[name] = nums
    return fonts


FONTS = _load_fonts()
F1, F1B, F2, F3 = 'font_6x8', 'font_6x8_bold', 'font_8x8', 'font_8x16'


class LCD:
    def __init__(self):
        self.px = [[0] * W for _ in range(H)]

    def set(self, x, y, v=1):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = v

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < W and 0 <= y < H else 0

    def text(self, x, y, s, font=F1, align='l', inv=False, right=W):
        f = FONTS[font]
        start, cw, ch, bpc = f[2], f[4], f[5], f[7]
        width = cw * len(s)
        if align == 'c':
            x = (W - width) // 2
        elif align == 'r':
            x = right - width
        for i, c in enumerate(s):
            if c == ' ':
                continue
            o = 8 + (ord(c) - start) * bpc
            data = f[o:o + bpc]
            for row in range(ch // 8):
                for col in range(cw):
                    b = data[row * cw + col]
                    for bit in range(8):
                        on = (b >> bit) & 1
                        px, py = x + i * cw + col, y + row * 8 + bit
                        if on:
                            self.set(px, py, 0 if inv else 1)
        return x + width

    def fill(self, x, y, w, h, v=1):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, v)

    def rect(self, x, y, w, h, v=1):
        self.hline(x, y, w, v); self.hline(x, y + h - 1, w, v)
        self.vline(x, y, h, v); self.vline(x + w - 1, y, h, v)

    def hline(self, x, y, w, v=1):
        for xx in range(x, x + w):
            self.set(xx, y, v)

    def dotted(self, x, y, w):
        for xx in range(x, x + w, 2):
            self.set(xx, y)

    def vline(self, x, y, h, v=1):
        for yy in range(y, y + h):
            self.set(x, yy, v)

    def line(self, x0, y0, x1, y1):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.set(x0, y0)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy; x0 += sx
            if e2 <= dx:
                err += dx; y0 += sy

    def circle(self, cx, cy, r, dotted=False):
        x, y, d = r, 0, 1 - r
        n = 0
        while x >= y:
            for px, py in ((x, y), (y, x), (-y, x), (-x, y), (-x, -y), (-y, -x), (y, -x), (x, -y)):
                if not dotted or n % 2 == 0:
                    self.set(cx + px, cy + py)
            n += 1
            y += 1
            if d < 0:
                d += 2 * y + 1
            else:
                x -= 1; d += 2 * (y - x) + 1

    def invert(self, x, y, w, h):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, 1 - self.get(xx, yy))

    def icon(self, x, y, rows):
        for yy, r in enumerate(rows):
            for xx, c in enumerate(r):
                if c in '#X':
                    self.set(x + xx, y + yy, 1)
                elif c == 'o':
                    self.set(x + xx, y + yy, 0)

    # ---- shared EMCOM chrome -------------------------------------------------
    def header(self, left, right=''):
        self.fill(0, 0, W, 9)
        self.text(1, 1, left, F1B, inv=True)
        if right:
            self.text(0, 1, right, F1, align='r', inv=True, right=W - 1)

    def keybar(self, s):
        self.hline(0, 55, W)
        self.text(0, 56, s, F1)

    def chip(self, x, y, s, solid=True):
        w = len(s) * 6 + 3
        if solid:
            self.fill(x, y, w, 9)
            self.text(x + 2, y + 1, s, F1, inv=True)
        else:
            self.rect(x, y, w, 9)
            self.text(x + 2, y + 1, s, F1)
        return w

    def bar(self, x, y, w, h, frac, ticks=0):
        self.rect(x, y, w, h)
        self.fill(x + 1, y + 1, int((w - 2) * frac), h - 2)
        for t in range(1, ticks):
            tx = x + (w * t) // ticks
            self.set(tx, y - 1)

    def png(self, path, scale=4, night=False):
        bg, fg = ((196, 206, 190), (28, 34, 30)) if not night else ((10, 18, 14), (150, 232, 170))
        mid = tuple((a * 2 + b) // 3 for a, b in zip(fg, bg))
        img = Image.new('RGB', (W * scale + 2 * scale, H * scale + 2 * scale), bg)
        p = img.load()
        for y in range(H):
            for x in range(W):
                if self.px[y][x]:
                    for dy in range(scale):
                        for dx in range(scale):
                            edge = dx == scale - 1 or dy == scale - 1
                            p[(x + 1) * scale + dx, (y + 1) * scale + dy] = mid if edge else fg
        img.save(path)
