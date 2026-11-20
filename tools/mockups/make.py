# Pixel mockups of the platform features, drawn with the game's own font (cyr.bdf), 16-bit colours,
# and the departure board's layout code, over real screenshots of the fork (forktest snap/winsnap).
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim import *

SP = DATA
IMG = os.path.join(SP, 'img')
OUT = os.path.join(SP, 'out')
os.makedirs(OUT, exist_ok=True)

HALT = Image.open(os.path.join(IMG, 'zz_halt.png')).convert('RGB')
CNV = Image.open(os.path.join(IMG, 'zz_cnv0.png')).convert('RGB')
WORLD = Image.open(os.path.join(IMG, 'zz_n.png')).convert('RGB')

PAD = 4          # D_H_SPACE
VS = 4           # D_V_SPACE
M12 = FONT.width('M' * 12)
M18 = FONT.width('M' * 18)

# departure_board.cc colours
TRAIN_BG, TRAIN_STRIPE, TRAIN_HEAD_BG, TRAIN_HEAD = 0x0B1F4B, 0x0F2858, 0x16367A, 0xA9C4F0
TRAIN_TEXT, TRAIN_VIA, TRAIN_DELAY = 0xFFFFFF, 0x9FB8E0, 0xFFD030
OTHER_BG, OTHER_STRIPE, OTHER_HEAD_BG, OTHER_HEAD = 0x121212, 0x1A1A1A, 0x262626, 0x9A9A9A
OTHER_TEXT, OTHER_VIA, OTHER_DELAY = 0xFFB41E, 0xA77C2E, 0xFF5A3C
ROAD_BADGE = 0x1E64C8
# window skin (sampled)
WIN_BG = 0xD6EBF7
WIN_BORDER = 0x6392BD
TAB_LINE = 0x8CC7E7
TAB_ACTIVE_TEXT = 0x005D9C
BLACK = 0x000000


def save(im, name):
    im.save(os.path.join(OUT, name))


# ---------------------------------------------------------------------------------------------
# windows

def window_crop(src, x0, y0, x1, y1):
    return src.crop((x0, y0, x1, y1))


def grow_window(win, cut_y, extra):
    """make a window taller: rows below cut_y move down by extra, the gap is plain body"""
    w, h = win.size
    out = Image.new('RGB', (w, h + extra))
    out.paste(win.crop((0, 0, w, cut_y)), (0, 0))
    out.paste(win.crop((0, cut_y, w, h)), (0, cut_y + extra))
    # body between: copy one plain row (border, body, border)
    row = win.crop((0, cut_y, w, cut_y + 1))
    for y in range(cut_y, cut_y + extra):
        out.paste(row, (0, y))
    return out


def text_c(im, x, y, w, t, col):
    FONT.draw(im, x + (w - FONT.width(t)) // 2, y, t, col)


def tab(im, x, top, t, active):
    """a tab of the stop window: 1 px border, text 4 px in, inactive tabs 2 px lower"""
    w = FONT.width(t) + 7
    y0 = top if active else top + 2
    box(im, x, y0, w, 1, TAB_LINE)
    box(im, x, y0, 1, 205 - 40 - y0 + 1, TAB_LINE) if False else None
    return w


def draw_tabbar(im, ox, oy, x, tabs, active_i, line_y, top_y):
    """tabs in window coordinates (ox,oy = window origin on screen 60,40)"""
    for i, t in enumerate(tabs):
        w = FONT.width(t) + 7
        act = i == active_i
        y0 = top_y if act else top_y + 2
        # clear the column range above the line
        box(im, x, top_y, w + 1, line_y - top_y, WIN_BG)
        x += w
    x = tabs_x0 = None


# ---------------------------------------------------------------------------------------------
# train board with the track column

def badge_w(t):
    return FONT.width(t) + 2 * PAD if t else 0


def draw_badge(im, x, ty, t, bg, fg):
    w = badge_w(t)
    box(im, x, ty - 1, w, LS + 2, bg)
    FONT.draw(im, x + PAD, ty, t, fg)
    return w


def strike(im, x, ty, t, col):
    FONT.draw(im, x, ty, t, col)
    box(im, x - 1, ty + 5, FONT.width(t) + 2, 1, col)


def train_board(im, X, Y, W, rows, frame, with_track=True, hover=-1):
    heads = ['Departure', 'Train', 'Line', 'Destination', 'Track', 'Delay']
    if not with_track:
        heads[4] = None
    cw = [FONT.width(h) if h else 0 for h in heads]
    any_via = any(r.get('via') for r in rows)
    for r in rows:
        cw[0] = max(cw[0], FONT.width(r['time']))
        cw[1] = max(cw[1], FONT.width(r.get('train') or r.get('cat', '')) + (PAD + FONT.width('2/2') if r.get('pair') else 0))
        cw[2] = max(cw[2], badge_w(r.get('line', '')))
        cw[3] = max(cw[3], FONT.width(r['dest']), FONT.width(r.get('via', '')))
        if with_track:
            cw[4] = max(cw[4], badge_w(r.get('track', '')))
        if r.get('delay'):
            cw[5] = max(cw[5], FONT.width('+%d min' % r['delay']))
    cw[1] = min(cw[1], M12)
    cw[3] = min(max(cw[3], M12), M18)
    hh = LS + VS
    rh = (2 * LS if any_via else LS) + VS
    H = hh + max(1, len(rows)) * rh + VS
    fixed = PAD + sum(c + 2 * PAD for i, c in enumerate(cw) if i != 3 and c > 0)
    cw[3] = max(cw[3], W - fixed - 2 * PAD)
    cx = []
    x = X + PAD
    for c in cw:
        cx.append(x + PAD)
        if c > 0:
            x += c + 2 * PAD
    box(im, X, Y, W, H, TRAIN_BG)
    box(im, X, Y, W, hh, TRAIN_HEAD_BG)
    hy = Y + VS // 2
    for i in range(4):
        FONT.draw(im, cx[i], hy, heads[i], TRAIN_HEAD)
    if with_track:
        text_c(im, cx[4], hy, cw[4], 'Track', TRAIN_HEAD)
    FONT.draw(im, cx[5] + cw[5] - FONT.width('Delay'), hy, 'Delay', TRAIN_HEAD)
    for i, r in enumerate(rows):
        ry = Y + hh + i * rh
        ty = ry + VS // 2
        if i == hover:
            box(im, X, ry, W, rh, 0x1E4590)
        elif i & 1:
            box(im, X, ry, W, rh, TRAIN_STRIPE)
        FONT.draw(im, cx[0], ty, r['time'], TRAIN_TEXT)
        if r.get('delay'):
            t = '+%d min' % r['delay']
            FONT.draw(im, cx[5] + cw[5] - FONT.width(t), ty, t, TRAIN_DELAY)
        if r.get('line'):
            draw_badge(im, cx[2], ty, r['line'], TRAIN_TEXT, TRAIN_BG)
        tr = r.get('train')
        tw = FONT.ellipsis(im, cx[1], ty, cw[1], tr if tr else r.get('cat', ''), TRAIN_TEXT if tr else TRAIN_VIA)
        if r.get('pair'):
            FONT.draw(im, cx[1] + tw + PAD, ty, r['pair'], TRAIN_VIA)
        FONT.ellipsis(im, cx[3], ty, cw[3], r['dest'], TRAIN_TEXT)
        if r.get('via'):
            FONT.ellipsis(im, cx[3], ty + LS, cw[3], r['via'], TRAIN_VIA)
        if with_track and r.get('track'):
            t = r['track']
            st = r.get('tstate', 'confirmed')
            bx = cx[4] + (cw[4] - badge_w(t)) // 2
            if st == 'confirmed':
                FONT.draw(im, bx + PAD, ty, t, TRAIN_TEXT)
            elif st == 'planned':
                FONT.draw(im, bx + PAD, ty, t, TRAIN_VIA)
            elif st == 'changed':
                if frame == 0:
                    draw_badge(im, bx, ty, t, TRAIN_DELAY, TRAIN_BG)
                else:
                    FONT.draw(im, bx + PAD, ty, t, TRAIN_DELAY)
                if any_via and r.get('was'):
                    w0 = FONT.width(r['was'])
                    strike(im, cx[4] + (cw[4] - w0) // 2, ty + LS, r['was'], TRAIN_VIA)
    return H, cx, cw


KOLIN_ROWS = [
    dict(time='00:50', train='Os 2513', line='S1', dest='Praha Masarykovo n.', via='Pečky, Český Brod, Úvaly', track='3', tstate='confirmed'),
    dict(time='00:52', train='R 981 Vysočina', line='R9', dest='Brno hl.n.', via='Kutná Hora, Havlíčkův Brod', track='1', tstate='confirmed'),
    dict(time='00:55', train='Os 5003', cat='Os', dest='Pardubice hl.n.', via='Velký Osek, Přelouč', track='2', tstate='confirmed'),
    dict(time='00:58', train='Os 9105', line='S1', dest='Praha Masarykovo n.', via='Pečky, Úvaly', track='4', tstate='confirmed'),
    dict(time='01:05', train='R 983 Vysočina', line='R9', dest='Brno hl.n.', via='Kutná Hora, Havlíčkův Brod', track='3', tstate='changed', was='1', delay=4),
    dict(time='01:20', cat='Os', line='S1', dest='Praha Masarykovo n.', via='Pečky, Český Brod, Úvaly', track='3', tstate='planned'),
    dict(time='01:22', train='Os 5005', cat='Os', dest='Pardubice hl.n.', via='Velký Osek, Přelouč', track='2', tstate='planned'),
]


def halt_window_board(frame, rows, with_track=True, title=None):
    win = window_crop(HALT, 60, 40, 512, 376)
    extra = 120
    win = grow_window(win, 320, extra)
    # clear the old board (window coords: board x 10, y 198)
    box(win, 1, 198, 450, 140 + extra - 12, WIN_BG)
    train_board(win, 10, 198, 432, rows, frame, with_track)
    return win


# ---------------------------------------------------------------------------------------------
# tiny LED font for the in-world displays (3x5 caps, accents in two rows above)

LED = {
    'A': ['.#.', '#.#', '###', '#.#', '#.#'], 'B': ['##.', '#.#', '##.', '#.#', '##.'],
    'C': ['.##', '#..', '#..', '#..', '.##'], 'D': ['##.', '#.#', '#.#', '#.#', '##.'],
    'E': ['###', '#..', '##.', '#..', '###'], 'F': ['###', '#..', '##.', '#..', '#..'],
    'G': ['.##', '#..', '#.#', '#.#', '.##'], 'H': ['#.#', '#.#', '###', '#.#', '#.#'],
    'I': ['#', '#', '#', '#', '#'], 'J': ['..#', '..#', '..#', '#.#', '.#.'],
    'K': ['#.#', '#.#', '##.', '#.#', '#.#'], 'L': ['#..', '#..', '#..', '#..', '###'],
    'M': ['#...#', '##.##', '#.#.#', '#...#', '#...#'], 'N': ['#..#', '##.#', '#.##', '#..#', '#..#'],
    'O': ['.#.', '#.#', '#.#', '#.#', '.#.'], 'P': ['##.', '#.#', '##.', '#..', '#..'],
    'R': ['##.', '#.#', '##.', '#.#', '#.#'], 'S': ['.##', '#..', '.#.', '..#', '##.'],
    'T': ['###', '.#.', '.#.', '.#.', '.#.'], 'U': ['#.#', '#.#', '#.#', '#.#', '###'],
    'V': ['#.#', '#.#', '#.#', '#.#', '.#.'], 'Y': ['#.#', '#.#', '.#.', '.#.', '.#.'],
    'Z': ['###', '..#', '.#.', '#..', '###'], 'W': ['#...#', '#...#', '#.#.#', '##.##', '#...#'],
    'X': ['#.#', '#.#', '.#.', '#.#', '#.#'], 'Q': ['.#.', '#.#', '#.#', '.#.', '..#'],
    '0': ['###', '#.#', '#.#', '#.#', '###'], '1': ['.#', '##', '.#', '.#', '.#'],
    '2': ['##.', '..#', '.#.', '#..', '###'], '3': ['##.', '..#', '.#.', '..#', '##.'],
    '4': ['#.#', '#.#', '###', '..#', '..#'], '5': ['###', '#..', '##.', '..#', '##.'],
    '6': ['.##', '#..', '###', '#.#', '###'], '7': ['###', '..#', '.#.', '.#.', '.#.'],
    '8': ['###', '#.#', '###', '#.#', '###'], '9': ['###', '#.#', '###', '..#', '##.'],
    ':': ['.', '#', '.', '#', '.'], '.': ['.', '.', '.', '.', '#'], ',': ['.', '.', '.', '#', '#'],
    '+': ['...', '.#.', '###', '.#.', '...'], '-': ['..', '..', '##', '..', '..'],
    '/': ['..#', '..#', '.#.', '#..', '#..'],
}
ACC = {'Á': ('A', 'acute'), 'Č': ('C', 'caron'), 'Ď': ('D', 'caron'), 'É': ('E', 'acute'), 'Ě': ('E', 'caron'),
       'Í': ('I', 'acute'), 'Ň': ('N', 'caron'), 'Ó': ('O', 'acute'), 'Ř': ('R', 'caron'), 'Š': ('S', 'caron'),
       'Ť': ('T', 'caron'), 'Ú': ('U', 'acute'), 'Ů': ('U', 'ring'), 'Ý': ('Y', 'acute'), 'Ž': ('Z', 'caron')}


def led_glyph(ch):
    if ch == ' ':
        return 2, []
    base, acc = ACC.get(ch, (ch, None))
    g = LED[base]
    w = len(g[0])
    px = [(x, y) for y in range(5) for x in range(w) if g[y][x] == '#']
    m = w // 2
    if acc == 'acute':
        px += [(min(w - 1, m + 1), -2), (m, -1)] if w > 1 else [(0, -2)]
    elif acc == 'caron':
        px += [(max(0, m - 1), -2), (min(w - 1, m + 1), -2), (m, -1)] if w > 1 else [(0, -1)]
    elif acc == 'ring':
        px += [(m, -2)]
    return w + 1, px


def led_width(t):
    return sum(led_glyph(c)[0] for c in t) - 1


def led_draw(im, x, y, t, col, sx0):
    """text stepping down along a 2:1 iso edge: each glyph starts (its column - sx0)//2 lower"""
    px = im.load()
    c = q(col)
    for ch in t:
        adv, pts = led_glyph(ch)
        dy = (x - sx0) // 2
        for (gx, gy) in pts:
            px[x + gx, y + dy + gy] = c
        x += adv
    return x


# ---------------------------------------------------------------------------------------------
# in-world objects (screen coordinates at the neutral zoom, 128 px tiles)

def tile_origin(tx, ty):
    # snap 39,20 put the tile image at 512,336
    return 512 + (tx - 39) * 64 - (ty - 20) * 64, 336 + (tx - 39) * 32 + (ty - 20) * 32


def ground_point(tx, ty):
    """screen point of a (fractional) tile position on flat ground: diamond centre is +64,+96"""
    x, y = tile_origin(tx, ty)
    return int(round(x + 64)), int(round(y + 96))


PANEL_FRAME = 0x2A2A2A
PANEL_EDGE = 0x4A4A4A
POST_D = 0x5A5A5A
POST_L = 0x9A9A9A
SZ_BLUE = 0x0057A8


def para(im, x, y, w, h, col):
    """parallelogram along a track running down-right: column c is (c//2) lower"""
    for c in range(w):
        box(im, x + c, y + c // 2, 1, h, col)


def post(im, x, top, bottom):
    box(im, x, top, 1, bottom - top, POST_L)
    box(im, x + 1, top, 1, bottom - top, POST_D)


def led_panel(im, gx, gy, w, lines, height_above=26):
    """a display hung on two posts, its face along the track. gx,gy = ground point of the left post.
    lines: list of segments per text line: (kind, text) kind = num|text|dim|badge|delay"""
    n = len(lines)
    h = 3 + 8 * n
    x0 = gx
    y0 = gy - height_above - h
    # posts (behind), from the panel down to the ground at both ends
    post(im, x0 + 2, y0 + h, gy)
    post(im, x0 + w - 4, y0 + h + (w - 4) // 2, gy + (w - 4) // 2)
    # housing: dark frame, screen inside
    para(im, x0, y0, w, h, PANEL_FRAME)
    para(im, x0 + 1, y0 + 1, w - 2, h - 2, TRAIN_BG)
    box(im, x0, y0, 1, h, PANEL_EDGE)
    # roof edge: one lighter row on top
    for c in range(w):
        box(im, x0 + c, y0 + c // 2 - 1, 1, 1, PANEL_EDGE)
    for li, segs in enumerate(lines):
        x = x0 + 3
        ty = y0 + 3 + 8 * li + 2
        for kind, t in segs:
            if kind == 'num':      # track number inverted
                dy = (x - x0) // 2
                bw = led_width(t) + 2
                for c in range(bw):
                    box(im, x + c, ty - 1 + (x + c - x0) // 2, 1, 7, TRAIN_TEXT)
                led_draw(im, x + 1, ty, t, TRAIN_BG, x0)
                x += bw + 3
            elif kind == 'badge':
                bw = led_width(t) + 2
                for c in range(bw):
                    box(im, x + c, ty - 1 + (x + c - x0) // 2, 1, 7, TRAIN_TEXT)
                led_draw(im, x + 1, ty, t, TRAIN_BG, x0)
                x += bw + 3
            else:
                col = {'text': TRAIN_TEXT, 'dim': TRAIN_VIA, 'delay': TRAIN_DELAY}[kind]
                x = led_draw(im, x, ty, t, col, x0) + 2
    return (x0, y0 - 2, w, h + (w // 2) + 4)


def track_sign(im, gx, gy, number, lines, height_above=20):
    """SZ-blue plate with the track number, a dark strip with the line badges after it"""
    nw = led_width(number) + 6
    h = 11
    x0 = gx
    y0 = gy - height_above - h
    post(im, x0 + 3, y0 + h, gy)
    para(im, x0, y0, nw, h, 0xFFFFFF)
    para(im, x0 + 1, y0 + 1, nw - 2, h - 2, SZ_BLUE)
    # bigger digits: the LED glyphs doubled vertically? keep 3x5 doubled to 6x10 would not fit; use bold 2x
    xx = x0 + 3
    for ch in number:
        adv, pts = led_glyph(ch)
        dy = (xx - x0) // 2
        for (px_, py_) in pts:
            box(im, xx + px_, y0 + 3 + dy + py_, 1, 1, 0xFFFFFF)
        xx += adv
    if lines:
        sx = x0 + nw
        sw = sum(led_width(l) + 2 for l in lines) + 3 * len(lines) + 1
        para2_y = y0 + nw // 2
        for c in range(sw):
            box(im, sx + c, y0 + (nw + c) // 2 + 1, 1, h - 2, PANEL_FRAME)
        x = sx + 2
        for l in lines:
            bw = led_width(l) + 2
            for c in range(bw):
                box(im, x + c, y0 + (x + c - x0) // 2 + 2, 1, 7, 0xFFFFFF)
            led_draw(im, x + 1, y0 + 3, l, TRAIN_BG, x0)
            x += bw + 3


def world(frame):
    im = WORLD.copy()
    # track numbers: y18 -> 3, y19 -> 1, y20 -> 2, y21 -> 4 (Czech: main tracks 1 and 2, sidings outward)
    a = frame == 0
    # track 3: platform behind it (north side)
    gx, gy = ground_point(36.0, 17.62)
    led_panel(im, gx, gy, 92, [[('num', '3'), ('text', '00:50'), ('badge', 'S1'), ('text', 'PRAHA MAS.N.' if a else 'VIA PEČKY')]])
    gx, gy = ground_point(33.4, 17.62)
    track_sign(im, gx, gy, '3', ['S1'])
    # island between 1 and 2: one display for both tracks, a sign for each track on one post
    gx, gy = ground_point(35.4, 19.5)
    led_panel(im, gx, gy, 100, [[('num', '1'), ('text', '00:52'), ('badge', 'R9'), ('text', 'BRNO HL.N.' if a else 'VIA KUTNÁ HORA')],
                                [('num', '2'), ('text', '00:55'), ('dim', 'OS'), ('text', 'PARDUBICE' if a else 'VIA PŘELOUČ')]], 22)
    gx, gy = ground_point(38.7, 19.5)
    track_sign(im, gx, gy, '1', ['R9'], 30)
    track_sign(im, gx, gy, '2', ['OS'], 16)
    # track 4: platform in front (south side)
    gx, gy = ground_point(34.9, 21.45)
    led_panel(im, gx, gy, 92, [[('num', '4'), ('text', '00:58'), ('badge', 'S1'), ('text', 'PRAHA MAS.N.' if a else 'VIA PEČKY')]])
    gx, gy = ground_point(37.8, 21.45)
    track_sign(im, gx, gy, '4', ['S1'])
    return im


def main():
    for f in (0, 1):
        save(halt_window_board(f, KOLIN_ROWS), 'board_%d.png' % f)
        w = world(f)
        save(w.crop((240, 140, 900, 560)), 'world_%d.png' % f)
    print('ok')


if __name__ == '__main__':
    main()


# ---------------------------------------------------------------------------------------------
# more windows

def widen(win, extra, cut_top=445, cut_low=420, split_y=150):
    w, h = win.size
    out = Image.new('RGB', (w + extra, h))
    for y in range(h):
        cut = cut_top if y < split_y else cut_low
        out.paste(win.crop((0, y, cut, y + 1)), (0, y))
        c = win.getpixel((cut, y))
        out.paste(c, (cut, y, cut + extra, y + 1))
        out.paste(win.crop((cut, y, w, y + 1)), (cut + extra, y))
    return out


def board_window(frame, rows, extra_w=100):
    win = window_crop(HALT, 60, 40, 512, 376)
    win = grow_window(win, 320, 120)
    win = widen(win, extra_w)
    box(win, 1, 198, win.size[0] - 2, 130 + 120 - 12, WIN_BG)
    train_board(win, 10, 198, 432 + extra_w, rows, frame, True)
    return win


def field(im, x, y, w, h, t=None):
    box(im, x, y, w, h, 0x6392BD)
    box(im, x + 1, y + 1, w - 2, h - 2, 0xFFFBFF)
    if t:
        FONT.draw(im, x + 4, y + (h - LS) // 2, t, BLACK)


def button(im, x, y, w, t):
    h = 16
    box(im, x, y, w, h, 0x73B2DE)
    box(im, x, y, w, 1, 0x94B6C6); box(im, x, y + h - 1, w, 1, 0x94B6C6)
    box(im, x + 1, y + 1, w - 2, 1, 0x6BA2CE); box(im, x + 1, y + h - 2, w - 2, 1, 0x6BA2CE)
    box(im, x, y + 1, 1, h - 2, 0x6392BD); box(im, x + w - 1, y + 1, 1, h - 2, 0x6392BD)
    text_c(im, x, y + 3, w, t, 0xFFFBFF)


def window_frame(w, h, title):
    """an empty window in the pakset skin: title bar from the real window, body, borders, gripper"""
    win = window_crop(HALT, 60, 40, 512, 376)
    out = Image.new('RGB', (w, h), q(WIN_BG))
    # title bar rows 0..21: left part (icons) and the plain bar, right end column from the original
    out.paste(win.crop((0, 0, 56, 22)), (0, 0))
    for x in range(56, w - 1):
        out.paste(win.crop((300, 0, 301, 22)), (x, 0))
    out.paste(win.crop((451, 0, 452, 22)), (w - 1, 0))
    # body borders
    box(out, 0, 22, 1, h - 22, WIN_BORDER)
    box(out, w - 1, 22, 1, h - 22, WIN_BORDER)
    box(out, 0, h - 1, w, 1, WIN_BORDER)
    # gripper
    out.paste(win.crop((436, 322, 451, 335)), (w - 16, h - 14))
    FONT.draw(out, 56, 6, title, 0xFFFFFF)
    return out


def draw2x(im, x, y, t, col):
    """the game font doubled (a display_text_scaled helper would do the same with fillboxes)"""
    c = q(col)
    for ch in t:
        adv, rows = FONT.g.get(ord(ch), (8, []))
        for (bx, by) in rows:
            box(im, x + 2 * bx, y + 2 * by, 2, 2, col)
        x += 2 * adv + 2
    return x


def display_window(frame):
    W, H = 400, 150
    win = window_frame(W, H, 'Kolín <011,230> - Track 1')
    X, Y, PW = 8, 30, W - 16
    PH = 66
    box(win, X, Y, PW, PH, TRAIN_BG)
    # big track number
    box(win, X + 6, Y + 6, 30, 30, TRAIN_TEXT)
    draw2x(win, X + 6 + (30 - 2 * FONT.width('1')) // 2, Y + 6 + 4, '1', TRAIN_BG)
    tx = X + 46
    ty = Y + 6
    x = FONT.draw(win, tx, ty, '00:52', TRAIN_TEXT) + 2 * PAD
    x = FONT.draw(win, x, ty, 'R 981 Vysočina', TRAIN_TEXT) + 2 * PAD
    draw_badge(win, x, ty, 'R9', TRAIN_TEXT, TRAIN_BG)
    on = 'On time' if frame == 0 else 'Leaves in 7 min'
    FONT.draw(win, X + PW - PAD - 2 - FONT.width(on), ty, on, TRAIN_VIA)
    draw2x(win, tx, ty + LS + 2, 'Brno hl.n.', TRAIN_TEXT)
    FONT.draw(win, tx, ty + 3 * LS + 4, 'via Kutná Hora, Havlíčkův Brod', TRAIN_VIA)
    # below the panel: what the track serves, in the window's own style
    by = Y + PH + 8
    x = FONT.draw(win, X, by + 2, 'Lines from this track:', BLACK) + PAD
    for ln in ('R9',):
        x += draw_badge(win, x, by + 2, ln, TRAIN_BG, TRAIN_TEXT) + PAD
    FONT.draw(win, x + PAD, by + 2, '(set by the schedules)', 0x5A5A5A)
    button(win, X, by + 18, 120, 'Show on the map')
    button(win, X + 124, by + 18, 120, 'Stop window')
    return win


def tracks_window():
    win = window_crop(HALT, 60, 40, 512, 376)
    win = widen(win, 100)
    W = win.size[0]
    # tab bar: Goods... | Departure time | Tracks | Connections | Chart, Tracks active
    box(win, 195, 146, W - 196, 19, WIN_BG)
    x = 195
    for t, act in (('Departure time', False), ('Tracks', True), ('Connections', False), ('Chart', False)):
        w = FONT.width(t) + 8
        top = 148 if act else 150
        box(win, x + 1, top, w - 2, 1, TAB_LINE)
        box(win, x, top + 1, 1, 165 - top - 1, TAB_LINE)
        box(win, x + w - 1, top + 1, 1, 165 - top - 1, TAB_LINE)
        FONT.draw(win, x + 4, 151, t, TAB_ACTIVE_TEXT if act else BLACK)
        if not act:
            box(win, x, 165, w, 1, TAB_LINE)
        else:
            box(win, x + 1, 165, w - 2, 1, WIN_BG)
        x += w
    box(win, x, 165, W - 1 - x, 1, TAB_LINE)
    # body
    box(win, 1, 166, W - 2, 335 - 166, WIN_BG)
    y = 172
    FONT.draw(win, 10, y, 'Numbers show on the boards, the displays and the track signs.', BLACK)
    FONT.draw(win, 10, y + LS + 2, 'Lines come from the schedules. Point at a row to see the track.', 0x5A5A5A)
    y += 2 * LS + 10
    cols = [10, 62, 112, 214]
    heads = ['Track', 'Tiles', 'Lines', 'Next departure']
    for cx_, h in zip(cols, heads):
        FONT.draw(win, cx_, y, h, TAB_ACTIVE_TEXT)
    y += LS + 3
    box(win, 8, y, W - 16, 1, TAB_LINE)
    y += 3
    data = [('1', '12', ['R9'], '00:52  R 981 Vysočina'),
            ('2', '12', ['Os'], '00:55  Os 5003'),
            ('3', '12', ['S1'], '00:50  Os 2513'),
            ('4', '12', ['S1'], '00:58  Os 9105')]
    for i, (tr, tl, lines, nx) in enumerate(data):
        ry = y + i * 20
        if i == 1:
            box(win, 8, ry - 1, W - 16, 20, 0xC6E3F7)
        field(win, cols[0], ry, 40, 18, tr)
        FONT.draw(win, cols[1], ry + 4, tl, BLACK)
        x = cols[2]
        for ln in lines:
            x += draw_badge(win, x, ry + 4, ln, TRAIN_BG, TRAIN_TEXT) + PAD
        FONT.draw(win, cols[3], ry + 4, nx, BLACK)
    y += 4 * 20 + 6
    button(win, 10, y, 130, 'Number automatically')
    button(win, 146, y, 130, 'Number tracks tool')
    return win


def convoy_window(frame):
    win = window_crop(CNV, 60, 40, 597, 438)
    # Departure row: window coords (10,127)
    box(win, 10, 126, 380, LS + 1, WIN_BG)
    x = FONT.draw(win, 10, 127, 'Departure: 00:50 (in 5 min), track 3', BLACK)
    # the destination sign under the picture gets the track at its right end
    sx0, sy0, sw, sh = 398, 167, 128, 18
    tx = sx0 + sw - PAD - FONT.width('3') - 1
    box(win, tx - PAD, sy0 + 1, 1, sh - 2, TRAIN_HEAD_BG)
    FONT.draw(win, tx, sy0 + (sh - LS) // 2, '3', TRAIN_TEXT)
    return win


def convoy_states():
    W, H = 560, 3 * 26 + 10
    im = Image.new('RGB', (W, H), q(WIN_BG))
    states = [('Departure: 01:05 (in 20 min), track 1', '1', 'planned', 'planned: the schedule\'s track, nothing reserved yet'),
              ('Departure: 00:50 (in 5 min), track 3', '3', 'confirmed', 'confirmed: standing there, or its way in is reserved'),
              ('Departure: 01:05 (in 20 min), track 3 instead of 1', '3', 'changed', 'changed: a choose signal or P sent it elsewhere')]
    for i, (t, n, st, note) in enumerate(states):
        y = 6 + i * 26
        FONT.draw(im, 8, y, t, BLACK)
        FONT.draw(im, 8, y + LS + 1, note, 0x5A5A5A)
        sx, sw, sh = 412, 140, 18
        box(im, sx, y, sw, sh, TRAIN_HEAD_BG)
        box(im, sx + 1, y + 1, sw - 2, sh - 2, TRAIN_BG)
        ty = y + (sh - LS) // 2
        w = draw_badge(im, sx + PAD, ty, 'R9' if n != '3' or st == 'changed' else 'S1', TRAIN_TEXT, TRAIN_BG)
        FONT.draw(im, sx + PAD + w + PAD, ty, 'Brno hl.n.' if (n != '3' or st == 'changed') else 'Praha Mas.n.', TRAIN_TEXT)
        tx = sx + sw - PAD - FONT.width(n) - 1
        if st == 'changed':
            bw = badge_w(n)
            draw_badge(im, sx + sw - 2 - bw, ty, n, TRAIN_DELAY, TRAIN_BG)
        else:
            box(im, tx - PAD, y + 1, 1, sh - 2, TRAIN_HEAD_BG)
            FONT.draw(im, tx, ty, n, TRAIN_TEXT if st == 'confirmed' else TRAIN_VIA)
    return im


def bus_board(frame):
    rows = [('1', 'Zálabí', 'Nemocnice', 'A', '2 min', 0), ('3', 'Štítary', 'Masarykova', 'B', '6 min', 0),
            ('254', 'Kutná Hora, aut.st.', 'Nová Ves I', 'D', '9 min', 3), ('1', 'Zálabí', 'Nemocnice', 'A', '14 min', 0),
            ('255', 'Ratboř', 'Polepy', 'E', '18 min', 0), ('382', 'Český Brod, žel.st.', 'Pečky', 'C', '24 min', 0)]
    W = 470
    hh = LS + VS
    rh = LS + VS
    H = hh + len(rows) * rh + VS
    im = Image.new('RGB', (W + 2, H + 2), q(WIN_BORDER))
    X, Y = 1, 1
    box(im, X, Y, W, H, OTHER_BG)
    box(im, X, Y, W, hh, OTHER_HEAD_BG)
    lw = max(FONT.width('Line'), max(badge_w(r[0]) for r in rows))
    sw_ = max(FONT.width('Stand'), max(badge_w(r[3]) for r in rows))
    dw = max(FONT.width('Departure'), max(FONT.width(r[4]) for r in rows))
    yw = max(FONT.width('Delay'), FONT.width('+3'))
    c0 = X + 2 * PAD
    c1 = c0 + lw + 2 * PAD
    c4 = X + W - 2 * PAD - yw
    c3 = c4 - 2 * PAD - dw
    c2 = c3 - 2 * PAD - sw_
    hy = Y + 2
    FONT.draw(im, c0, hy, 'Line', OTHER_HEAD)
    FONT.draw(im, c1, hy, 'Destination', OTHER_HEAD)
    text_c(im, c2, hy, sw_, 'Stand', OTHER_HEAD)
    FONT.draw(im, c3 + dw - FONT.width('Departure'), hy, 'Departure', OTHER_HEAD)
    FONT.draw(im, c4 + yw - FONT.width('Delay'), hy, 'Delay', OTHER_HEAD)
    for i, (ln, d, via, stand, t, dl) in enumerate(rows):
        ry = Y + hh + i * rh
        ty = ry + 2
        if i & 1:
            box(im, X, ry, W, rh, OTHER_STRIPE)
        draw_badge(im, c0, ty, ln, ROAD_BADGE, 0xFFFFFF)
        dwid = FONT.ellipsis(im, c1, ty, c2 - c1 - 2 * PAD, d, OTHER_TEXT)
        if dwid + PAD < c2 - c1 - 2 * PAD:
            FONT.ellipsis(im, c1 + dwid + PAD, ty, c2 - c1 - 2 * PAD - dwid - PAD, via, OTHER_VIA)
        bw = badge_w(stand)
        sx = c2 + (sw_ - bw) // 2
        box(im, sx, ty - 1, bw, LS + 2, OTHER_HEAD_BG)
        FONT.draw(im, sx + PAD, ty, stand, OTHER_TEXT)
        FONT.draw(im, c3 + dw - FONT.width(t), ty, t, OTHER_TEXT)
        if dl:
            FONT.draw(im, c4 + yw - FONT.width('+%d' % dl), ty, '+%d' % dl, OTHER_DELAY)
    return im


def main2():
    for f in (0, 1):
        save(board_window(f, KOLIN_ROWS), 'board_%d.png' % f)
        save(display_window(f), 'display_%d.png' % f)
        w = world(f)
        save(w.crop((300, 110, 940, 500)), 'world_%d.png' % f)
        save(w.crop((356, 214, 656, 424)), 'detail_%d.png' % f)
    save(tracks_window(), 'tracks.png')
    save(convoy_window(0), 'convoy.png')
    save(convoy_states(), 'states.png')
    save(bus_board(0), 'bus.png')
    # the board as it is today, for comparison
    save(window_crop(HALT, 60, 40, 512, 376), 'board_today.png')
    print('ok2')


if __name__ == "__main__":
    main2()
