# Platform cards: line chips that wrap, grouped by where the vehicle goes next, with an arrow that points
# the way it leaves on screen. One flow layout for every platform (track or stand).
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim import *

CARD_BG, CARD_EDGE, CARD_DIV = 0xD6EBF7, 0x395D7B, 0x8CC7E7
NUM_BG = 0x183C4A
CHIP = {'rail': (0x0B1F4B, 0xFFFFFF), 'bus': (0x1E64C8, 0xFFFFFF), 'tram': (0xC81E1E, 0xFFFFFF), 'more': (0xD6EBF7, 0x395D7B)}
PER_ROW = 6
ROW = 13           # chip row pitch
# screen direction of each map direction (isometric): W up-left, E down-right, N up-right, S down-left
ARROWS = {'E': (1, 1), 'W': (-1, -1), 'N': (1, -1), 'S': (-1, 1)}


def draw_dir(im, x, y, d, col):
    """7x7 diagonal arrow"""
    sx, sy = ARROWS[d]
    pts = [(i, i) for i in range(7)] + [(6, j) for j in range(2, 6)] + [(j, 6) for j in range(2, 6)]
    for (a, b) in pts:
        px = x + (a if sx > 0 else 6 - a)
        py = y + (b if sy > 0 else 6 - b)
        box(im, px, py, 1, 1, col)


def chip_w(t):
    return FONT.width(t) + 4


def cell_items(c):
    """rows of (kind, text, width): chips six to a row; with 'pairs' each run of chips is followed by its destination"""
    rows = [[]]
    def add(kind, t, w, brk):
        if brk and rows[-1]:
            rows.append([])
        rows[-1].append((kind, t, w))
    if c.get('pre'):
        add('pre', c['pre'], FONT.width(c['pre']) + 2, False)
    if c.get('pairs'):
        x = 0
        for lines, dest in c['pairs']:
            w = sum(chip_w(t) + 2 for t in lines) + FONT.width(dest) + 8
            brk = x > 0 and x + w > c.get('wrap', 300)
            if brk:
                x = 0
            for i, t in enumerate(lines):
                add('chip', t, chip_w(t), brk and i == 0)
            add('dest', dest, FONT.width(dest) + 6, False)
            x += w
    else:
        n = 0
        for t in c['lines']:
            add('chip', t, chip_w(t), n > 0 and n % PER_ROW == 0)
            n += 1
    if c.get('dir'):
        add('dir', c['dir'], 9, False)
    if c.get('name'):
        add('name', c['name'], FONT.width(c['name']) + 2, False)
    return rows


def cell_size(c):
    rows = cell_items(c)
    widths = [sum(w + 2 for k, t, w in r) - 2 if r else 0 for r in rows]
    return max(widths), len(rows) * ROW - 2, rows, widths


def measure(num, cells, maxw=420):
    x0 = 4 + (FONT.width(num) + 6 + 5 if num else 0)
    x, y, lh = x0, 3, 0
    pos = []
    W = 0
    for c in cells:
        w, h, rows, widths = cell_size(c)
        if x > x0 and x + w > maxw:
            x = x0
            y += lh + 4
            lh = 0
        pos.append((x, y, w, h, rows, widths))
        x += w + 11
        lh = max(lh, h)
        W = max(W, x - 11 + 4)
    return W, y + lh + 3, pos


def card(im, x, y, num, cells, kind='bus', maxw=420):
    W, H, pos = measure(num, cells, maxw)
    box(im, x, y, W, H, CARD_EDGE)
    box(im, x + 1, y + 1, W - 2, H - 2, CARD_BG)
    x0 = 4 + (FONT.width(num) + 6 + 5 if num else 0)
    if num:
        bw = FONT.width(num) + 6
        box(im, x + 2, y + 2, bw, 13, NUM_BG)
        FONT.draw(im, x + 5, y + 3, num, 0xFFFFFF)
    for c, (cx, cy, w, h, rows, widths) in zip(cells, pos):
        if cx > x0:
            box(im, x + cx - 6, y + cy, 1, h, CARD_DIV)
        k = c.get('kind', kind)
        for ri, r in enumerate(rows):
            xx = x + cx
            yy = y + cy + ri * ROW
            for kd, t, wd in r:
                if kd == 'chip':
                    bg, fg = CHIP['more' if t.startswith('+') else k]
                    box(im, xx, yy, wd, 11, bg)
                    FONT.draw(im, xx + 2, yy, t, fg)
                elif kd == 'dest':
                    FONT.draw(im, xx + 1, yy, t, 0x10284A)
                elif kd == 'pre':
                    FONT.draw(im, xx, yy, t, 0x10284A)
                elif kd == 'dir':
                    draw_dir(im, xx + 1, yy + 2, t, 0x000000)
                elif kd == 'name':
                    FONT.draw(im, xx, yy, t, 0x000000)
                xx += wd + 2
    return (x, y, W, H)


def leader(im, x0, y0, x1, y1, col=CARD_EDGE):
    n = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(n + 1):
        box(im, x0 + (x1 - x0) * i // n, y0 + (y1 - y0) * i // n, 1, 1, col)
    box(im, x0 - 1, y0 - 1, 3, 3, 0xFFFFFF)
    box(im, x0, y0, 1, 1, col)


def card_at(im, gx, gy, side, num, cells, kind='bus', maxw=420, rects=None):
    """a card beside the stand at ground point gx,gy; side = screen offset of the card's nearest corner"""
    W, H, _ = measure(num, cells, maxw)
    ax, ay = gx + side[0], gy + side[1]
    x = ax if side[0] >= 0 else ax - W
    y = ay if side[1] >= 0 else ay - H
    if rects is not None:
        while any(not (x + W <= r[0] or r[0] + r[2] <= x or y + H <= r[1] or r[1] + r[3] <= y) for r in rects):
            y += (ROW if side[1] >= 0 else -ROW)
    leader(im, gx, gy, x if side[0] >= 0 else x + W - 1, y if side[1] >= 0 else y + H - 1)
    r = card(im, x, y, num, cells, kind, maxw)
    if rects is not None:
        rects.append(r)
    return r


def table_measure(num, rows, maxw=560):
    """combined card of a track: one row per next stop (arrow, name), then every line with where it ends"""
    x0 = 4 + (FONT.width(num) + 6 + 5 if num else 0)
    c1 = max(9 + 3 + FONT.width(n) for d, n, pairs in rows)
    out = []
    y = 3
    W = 0
    for d, n, pairs in rows:
        x = x0 + c1 + 10
        items = []
        for lines, dest in pairs:
            w = sum(chip_w(t) + 2 for t in lines) + FONT.width(dest) + 2
            if x > x0 + c1 + 10 and x + w > maxw:
                x = x0 + c1 + 10
                y += ROW
            items.append((x, y, lines, dest))
            x += w + 10
            W = max(W, x - 10 + 4)
        out.append((d, n, items, y))
        y += ROW
    return W, y + 1, x0, c1, out


def table_card(im, x, y, num, rows, kind='rail', maxw=560):
    W, H, x0, c1, out = table_measure(num, rows, maxw)
    box(im, x, y, W, H, CARD_EDGE)
    box(im, x + 1, y + 1, W - 2, H - 2, CARD_BG)
    if num:
        bw = FONT.width(num) + 6
        box(im, x + 2, y + 2, bw, 13, NUM_BG)
        FONT.draw(im, x + 5, y + 3, num, 0xFFFFFF)
    box(im, x + x0 + c1 + 4, y + 3, 1, H - 6, CARD_DIV)
    first = {}
    for d, n, items, ylast in out:
        y0 = items[0][1]
        draw_dir(im, x + x0 + 1, y + y0 + 2, d, 0x000000)
        FONT.draw(im, x + x0 + 12, y + y0, n, 0x000000)
        for (ix, iy, lines, dest) in items:
            xx = x + ix
            for t in lines:
                bg, fg = CHIP[kind]
                box(im, xx, y + iy, chip_w(t), 11, bg)
                FONT.draw(im, xx + 2, y + iy, t, fg)
                xx += chip_w(t) + 2
            FONT.draw(im, xx + 1, y + iy, dest, 0x10284A)
    return (x, y, W, H)
