# Mockups on the owner's real Hranice na Moravě (save of 2026-11-08, game clock 10 May 1931 11:20).
# Bases: forktest snap/winsnap screenshots of the loaded save. Everything drawn here uses sim.py
# (the game's cyr.bdf, 16-bit colours) and the board code of make.py, checked against the real board.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim import *
import make as M
from hrdata import TRAIN_ROWS
import cards as C

SP = DATA
HR = os.path.join(SP, 'hr')
OUT = os.path.join(SP, 'out2')
os.makedirs(OUT, exist_ok=True)


def load(n):
    return Image.open(os.path.join(HR, n)).convert('RGB')


def save(im, n):
    im.save(os.path.join(OUT, n), optimize=True)


# ---------------------------------------------------------------------------------------------
# map labels: the game's marker tag (display_ddd_proportional_clip), plus a number box and an arrow

TAG_BG, TAG_LIGHT, TAG_DARK = 0x7396BD, 0x84AAB5, 0x4A6D7B
NUM_BG = 0x183C4A
NAME_COL = 0x10284A
SEP_COL = 0xD6EBF7
ARROW = '\x01'   # drawn, not a font glyph


def seg_width(segs):
    w = 0
    for kind, t in segs:
        if kind == 'num':
            w += FONT.width(t) + 6 + 4
        elif kind == 'arrow':
            w += 9
        elif kind == 'sep':
            w += 9
        else:
            w += FONT.width(t)
    return w


def draw_arrow(im, x, y, col):
    # 4x7 triangle pointing right, centred on the text's x-height
    for i, n in enumerate((1, 2, 3, 4, 3, 2, 1)):
        box(im, x, y + 2 + i, n, 1, col)


def tag(im, x, cy, segs, bg=TAG_BG, light=TAG_LIGHT, dark=TAG_DARK):
    """left edge x, centre line cy; returns the rectangle"""
    w = seg_width(segs) + 7
    box(im, x, cy - 7, w, 14, bg)
    box(im, x + 1, cy - 7, w - 2, 1, light)
    box(im, x + 1, cy + 6, w - 2, 1, dark)
    box(im, x, cy - 7, 1, 14, light)
    box(im, x + w - 1, cy - 7, 1, 14, dark)
    tx = x + 4
    ty = cy - 5
    for kind, t in segs:
        if kind == 'num':
            bw = FONT.width(t) + 6
            box(im, tx - 2, cy - 6, bw, 12, NUM_BG)
            FONT.draw(im, tx + 1, ty, t, 0xFFFFFF)
            tx += bw + 4
        elif kind == 'arrow':
            draw_arrow(im, tx + 2, ty, NAME_COL)
            tx += 9
        elif kind == 'sep':
            box(im, tx + 4, cy - 5, 1, 10, SEP_COL)
            tx += 9
        elif kind == 'name':
            tx = FONT.draw(im, tx, ty, t, NAME_COL)
        elif kind == 'dim':
            tx = FONT.draw(im, tx, ty, t, 0x31496B)
        else:
            tx = FONT.draw(im, tx, ty, t, 0x000000)
    return (x, cy - 7, w, 14)


def label_segs(num, groups, arrivals=None, full=False):
    """groups: list of (lines, next stop) or, full, (next stop, [(line, destination)])"""
    segs = []
    if num:
        segs.append(('num', num))
    first = True
    for g in groups:
        if not first:
            segs.append(('sep', ''))
        first = False
        if full:
            nxt, pairs = g
            segs += [('arrow', ''), ('name', nxt + ': ')]
            segs.append(('text', ', '.join('%s %s' % (l, d) for l, d in pairs)))
        else:
            lines, nxt = g
            segs.append(('text', ', '.join(lines)))
            if nxt:
                segs += [('text', ' '), ('arrow', ''), ('name', nxt)]
    if arrivals:
        if not first:
            segs.append(('sep', ''))
        segs += [('name', 'arr. '), ('text', ', '.join(arrivals))]
    return segs


class Placer:
    """labels of one view: a label that would cover an earlier one moves down a line"""
    def __init__(self, im):
        self.im = im
        self.rects = []

    def put(self, x, cy, segs):
        w = seg_width(segs) + 7
        while any(not (x + w <= r[0] or r[0] + r[2] <= x or cy + 7 <= r[1] or r[1] + r[3] <= cy - 7) for r in self.rects):
            cy += 15
        r = tag(self.im, x, cy, segs)
        self.rects.append(r)
        return r


# ---------------------------------------------------------------------------------------------
# the station: tracks 7, 6, 3, 2 and the bus stands, numbers as on the owner's own markers

ST_ORG = (5562, 2855, 896, 544)


def origin(org, x, y):
    return org[2] + ((x - org[0]) - (y - org[1])) * 64, org[3] + ((x - org[0]) + (y - org[1])) * 32


TRACKS = [
    ('7', (5557, 2849), [(['Ex1', 'RJ'], 'Olomouc hlavní nádraží'), (['Ex4', 'LE'], 'Přerov'), (['R8'], 'Lipník nad Bečvou'), (['S3'], 'Bělotín')]),
    ('6', (5557, 2850), [(['Ex1', 'Ex4', 'RJ'], 'Ostrava-Svinov'), (['LE', 'R8'], 'Suchdol nad Odrou')]),
    ('3', (5557, 2853), [(['Ex2'], 'Lipník nad Bečvou'), (['M7'], 'Hranice na Moravě město')]),
    ('2', (5557, 2854), [(['Ex2'], 'Valašské Meziříčí')]),
]
STANDS = [
    ('11', (5557, 2856), [(['930'], 'Šromotovo náměstí'), (['930'], 'Bělotín, střed'), (['949'], 'Slávie'), (['949'], 'pošta')]),
    ('8', (5560, 2856), [(['535'], 'Slávie'), (['928'], 'ZZN')]),
    ('6', (5561, 2856), [(['532', '927'], 'Slávie'), (['538'], 'pošta'), (['927'], 'Starý Jičín, Dub, restaurace')]),
    ('5', (5562, 2856), [(['NAD'], 'Drahotuše, náměstí')]),
    ('12', (5556, 2857), [(['531'], 'třída 1.máje'), (['534'], 'ZZN'), (['539'], 'Slávie')]),
    ('13', (5557, 2858), [(['536'], 'Slávie'), (['537'], 'pošta')]),
    ('9', (5559, 2858), [(['929'], 'CTP zadní brána')]),
    ('7', (5560, 2858), [(['533'], 'ZZN'), (['950'], 'Šromotovo náměstí')]),
    ('1', (5563, 2858), [(['345'], 'Šromotovo náměstí'), (['521'], 'Slávie')]),
    ('15', (5562, 2860), [(['1'], 'Nová ulice'), (['1', '13'], 'Slávie'), (['1', '3', '7'], 'Kunz')]),
    ('14', (5563, 2860), [(['2', '5'], 'ZZN'), (['2', '3', '4', '6', '7'], 'Slávie'), (['8'], 'Máchova'), (['8', '13'], 'Nová ulice')]),
]
STATION_CROP = (250, 150, 1650, 850)


TRACK_DIR = {'Olomouc hlavní nádraží': 'W', 'Přerov': 'W', 'Lipník nad Bečvou': 'W', 'Bělotín': 'E', 'Ostrava-Svinov': 'E',
             'Suchdol nad Odrou': 'E', 'Hranice na Moravě město': 'E', 'Valašské Meziříčí': 'E'}


def cells_of(groups, dirs, default):
    return [dict(lines=l, name=n, dir=dirs.get(n, default)) for l, n in groups]


def station_labels():
    im = load('c_station.png')
    rects = [(683, 408, 556, 16)]   # the stop's own name tag: cards keep clear of it
    for num, (x, y), groups in TRACKS:
        ox, oy = origin(ST_ORG, x, y)
        W, H, _ = C.measure(num, cells_of(groups, TRACK_DIR, 'E'), 560)
        cx, cy = ox + 56, oy + 57
        while any(not (cx + W <= r[0] or r[0] + r[2] <= cx or cy + H <= r[1] or r[1] + r[3] <= cy) for r in rects):
            cy += 15
        rects.append(C.card(im, cx, cy, num, cells_of(groups, TRACK_DIR, 'E'), 'rail', 560))
    for num, (x, y), groups in STANDS:
        ox, oy = origin(ST_ORG, x, y)
        d = 'W' if y == 2860 else 'S'
        cells = cells_of(groups, {}, d) if len(groups) > 1 else [dict(lines=groups[0][0])]
        W, H, _ = C.measure(num, cells, 420)
        cx, cy = ox + 56, oy + 57
        while any(not (cx + W <= r[0] or r[0] + r[2] <= cx or cy + H <= r[1] or r[1] + r[3] <= cy) for r in rects):
            cy += 15
        rects.append(C.card(im, cx, cy, num, cells, 'bus', 420))
    return im.crop(STATION_CROP)



# next stop + destination (the stop's second form); destinations as on the owner's markers and boards
T_FULL = {
    '7': [([(['Ex1'], 'Praha hl.n.'), (['RJ'], 'Praha-Zahradní město')], 'Olomouc hlavní nádraží'), ([(['Ex4'], 'Wien Hbf, Budapest-Nyugati pu'), (['LE'], 'Praha hl.n.')], 'Přerov'),
          ([(['R8'], 'Brno-Královo Pole')], 'Lipník nad Bečvou'), ([(['S3'], 'Studénka')], 'Bělotín')],
    '6': [([(['Ex1'], 'Warszawa Wschodnia, Čadca'), (['Ex4'], 'Kraków Główny, Warszawa Wschodnia'), (['RJ'], 'Havířov, Čadca')], 'Ostrava-Svinov'),
          ([(['LE'], 'Bohumín, Warszawa Wschodnia'), (['R8'], 'Bohumín')], 'Suchdol nad Odrou')],
    '3': [([(['Ex2'], 'Praha hl.n.')], 'Lipník nad Bečvou'), ([(['M7'], 'Vsetín')], 'Hranice na Moravě město')],
    '2': [([(['Ex2'], 'Púchov')], 'Valašské Meziříčí')],
}
S_FULL = {
    '11': [([(['930'], 'Přerov')], 'Šromotovo náměstí'), ([(['930'], 'Opava, východ')], 'Bělotín, střed'), ([(['949'], 'Kelč')], 'Slávie'), ([(['949'], 'Dolní Těšice')], 'pošta')],
    '8': [([(['535'], 'Hustopeče n.B., Poruba')], 'Slávie'), ([(['928'], 'Spálov')], 'ZZN')],
    '6': [([(['532'], 'Radíkov'), (['927'], 'Kopřivnice, Alšova')], 'Slávie'), ([(['538'], 'Horní Újezd, točna')], 'pošta'), ([(['927'], 'Nový Jičín, aut.nádraží')], 'Starý Jičín, Dub, restaurace')],
    '5': [([(['NAD'], 'Lipník nad Bečvou')], None)],
    '12': [([(['531'], 'Středolesí')], 'třída 1.máje'), ([(['534'], 'Jindřichov')], 'ZZN'), ([(['539'], 'Soběchleby')], 'Slávie')],
    '13': [([(['536'], 'Hustopeče n.B., Vysoká')], 'Slávie'), ([(['537'], 'Všechovice')], 'pošta')],
    '9': [([(['929'], 'Odry')], None)],
    '7': [([(['533'], 'Potštát')], 'ZZN'), ([(['950'], 'Bystřice pod Hostýnem')], 'Šromotovo náměstí')],
    '1': [([(['345'], 'Olomouc, aut.nádraží')], 'Šromotovo náměstí'), ([(['521'], 'Lipník n.B.')], 'Slávie')],
    '15': [([(['1'], 'sport. areál')], 'Nová ulice'), ([(['1', '13'], 'Kaufland')], 'Slávie'), ([(['1'], 'Masarykovo nám.'), (['3', '7'], 'Kunz')], 'Kunz')],
    '14': [([(['2'], 'Teplice n.B.'), (['5'], 'Lhotka')], 'ZZN'), ([(['2'], 'Lhotka'), (['3'], 'Slavíč, škola'), (['4'], 'Albert hyp.'), (['6'], 'dům seniorů'), (['7'], 'Kunz')], 'Slávie'),
           ([(['8'], 'dům seniorů')], 'Máchova'), ([(['8'], 'Studentská'), (['13'], 'Slavíč, škola')], 'Nová ulice')],
}


def full_cells(groups, dirs, default, wrap=300):
    return [dict(pairs=p, name=n, dir=(dirs.get(n, default) if n else None), wrap=wrap) for p, n in groups]


def station_full():
    im = load('c_station.png')
    rects = [(683, 408, 556, 16)]
    def put(x, y, num, cells, kind, maxw):
        ox, oy = origin(ST_ORG, x, y)
        W, H, _ = C.measure(num, cells, maxw)
        cx, cy = ox + 56, oy + 57
        while any(not (cx + W <= r[0] or r[0] + r[2] <= cx or cy + H <= r[1] or r[1] + r[3] <= cy) for r in rects):
            cy += 15
        rects.append(C.card(im, cx, cy, num, cells, kind, maxw))
    for num, (x, y), groups in TRACKS:
        put(x, y, num, full_cells(T_FULL[num], TRACK_DIR, 'E', 470), 'rail', 640)
    for num, (x, y), groups in STANDS:
        put(x, y, num, full_cells(S_FULL[num], {}, 'W' if y == 2860 else 'S'), 'bus', 520)
    return im.crop(STATION_CROP)


def table_rows(num):
    return [(TRACK_DIR.get(n, 'E'), n, p) for p, n in T_FULL[num]]


def station_combined():
    """the default look: tracks combined (next stop rows, every line with where it ends), stands with the next stop"""
    im = load('c_station.png')
    rects = [(683, 408, 556, 16)]
    def free(cx, cy, W, H):
        while any(not (cx + W <= r[0] or r[0] + r[2] <= cx or cy + H <= r[1] or r[1] + r[3] <= cy) for r in rects):
            cy += 15
        return cy
    for num, (x, y), groups in TRACKS:
        ox, oy = origin(ST_ORG, x, y)
        W, H = C.table_measure(num, table_rows(num), 600)[:2]
        cx = ox + 56
        cy = free(cx, oy + 57, W, H)
        rects.append(C.table_card(im, cx, cy, num, table_rows(num), 'rail', 600))
    for num, (x, y), groups in STANDS:
        ox, oy = origin(ST_ORG, x, y)
        d = 'W' if y == 2860 else 'S'
        cells = cells_of(groups, {}, d) if len(groups) > 1 else [dict(lines=groups[0][0])]
        W, H, _ = C.measure(num, cells, 420)
        cx = ox + 56
        cy = free(cx, oy + 57, W, H)
        rects.append(C.card(im, cx, cy, num, cells, 'bus', 420))
    return im.crop(STATION_CROP)


def station_today():
    return load('t_station.png').crop(STATION_CROP)


# ---------------------------------------------------------------------------------------------
# city stops south-west of the station

CITY_ORG = (5552, 2860, 896, 496)
CITY = [
    # tile, height offset in px (the stops lie lower than the centre tile), rows of (number, groups)
    ((5554, 2859), [('', [(['1', '2', '3', '4', '5', '6', '8', '13', '521', '532', '533', '534', '535', '536', '539', '927', '928', '949'], 'aut.st.')])]),
    ((5551, 2861), [('', [(['2', '3', '4', '6', '7', '521', '532', '535', '536', '539', '927'], 'třída 1.máje'), (['536', '949'], 'pošta'), (['1', '13'], 'Kaufland')])]),
    ((5552, 2857), [('', [(['533', '928'], 'Velká, rozcestí'), (['2', '5', '534'], 'Oseva')])]),
    ((5546, 2861), [('', [(['8'], 'Hviezdoslavova')]), ('', [(['8'], 'Slávie')])]),
]
CITY_CROP = (400, 170, 1620, 770)


CITY_CARDS = [
    # tile, travel direction, side of the card (screen offset from the stand), cells
    ((5551, 2861), 'W', (26, -34), [dict(lines=['2', '3', '4', '6', '7', '521', '532', '535', '536', '539', '927'], name='třída 1.máje', dir='W'),
                                    dict(lines=['536', '949'], name='pošta', dir='W'), dict(lines=['1', '13'], name='Kaufland', dir='S')]),
    ((5554, 2859), 'E', (-26, 30), [dict(lines=['1', '2', '3', '4', '5', '6', '8', '13', '521', '532', '533', '534', '535', '536', '539', '927', '928', '949'], name='aut.st.', dir='E')]),
    ((5552, 2857), 'W', (26, -34), [dict(lines=['533', '928'], name='Velká, rozcestí', dir='W'), dict(lines=['2', '5', '534'], name='Oseva', dir='N')]),
    ((5546, 2861), 'W', (26, -34), [dict(lines=['8'], name='Hviezdoslavova', dir='W')]),
    ((5546, 2861), 'E', (-26, 30), [dict(lines=['8'], name='Slávie', dir='E')]),
]


def city_labels(debug=False):
    im = load('c_city.png')
    rects = []
    for (x, y), d, side, cells in CITY_CARDS:
        ox, oy = origin(CITY_ORG, x, y)
        gx, gy = ox + 64, oy + 94
        # the stand's kerb: a little to the right of the travel direction
        k = {'W': (10, -5), 'E': (-10, 5), 'N': (10, 5), 'S': (-10, -5)}[d]
        C.card_at(im, gx + k[0], gy + k[1], side, '', cells, 'bus', 190, rects)
    return im.crop(CITY_CROP)


def city_today():
    return load('t_city.png').crop(CITY_CROP)


# ---------------------------------------------------------------------------------------------
# label forms side by side

def ground(W, H):
    import random
    im = Image.new('RGB', (W, H), q(0x4A8229))
    px = im.load()
    random.seed(7)
    g = [q(0x4A8229), q(0x528A31), q(0x4A7929), q(0x5A9239)]
    for y in range(H):
        for x in range(W):
            px[x, y] = g[random.randrange(4)]
    return im


def label_forms():
    im = ground(860, 358)
    y = 8
    # 1 a track, lines by next stop
    C.card(im, 10, y, '7', cells_of(TRACKS[0][2], TRACK_DIR, 'E'), 'rail', 620)
    y += 26
    # 2 the same with destinations: the stop's second form
    C.table_card(im, 10, y, '7', table_rows('7'), 'rail', 700)
    y += 64
    # 3 direction only where it tells something
    C.card(im, 10, y, '2', [dict(lines=['Ex2'], name='Valašské Meziříčí', dir='E')], 'rail')
    C.card(im, 190, y, '9', [dict(lines=['929'])], 'bus')
    C.card(im, 250, y, '', [dict(lines=['8'], name='Hviezdoslavova', dir='W')], 'bus')
    # 4 arrivals only, mixed
    C.card(im, 400, y, '4', [dict(lines=['79'], pre='arr.')], 'bus')
    C.card(im, 480, y, '10', [dict(lines=['Ex1', 'Ex2'], name='Praha-Libeň', dir='E'), dict(lines=['R18'], pre='arr.')], 'rail')
    y += 26
    # 5 many lines wrap six to a row, a street stop
    C.card(im, 10, y, '', [dict(lines=['2', '3', '4', '6', '7', '521', '532', '535', '536', '539', '927'], name='třída 1.máje', dir='W'),
                           dict(lines=['536', '949'], name='pošta', dir='W'), dict(lines=['1', '13'], name='Kaufland', dir='S')], 'bus', 190)
    # 6 cut: more than three rows of lines
    C.card(im, 250, y, '', [dict(lines=['1', '2', '3', '4', '5', '6', '8', '13', '521', '532', '533', '534', '535', '536', '539', '927', '928', '+4'], name='aut.st.', dir='E')], 'bus', 190)
    # 7 tram and bus at one kerb
    C.card(im, 480, y, 'C', [dict(lines=['1', '4', '9'], name='Mendlovo nám.', dir='W', kind='tram'), dict(lines=['25', '26', '37'], name='Mendlovo nám.', dir='W', kind='bus')], 'bus', 190)
    y += 74
    # 8 far zoom: chips only
    C.card(im, 10, y, '', [dict(lines=['2', '3', '4', '6', '7', '521'], dir='W'), dict(lines=['+7'])], 'bus', 190)
    # 9 hover: every line with where it ends
    y2 = y
    x = 250
    full = [('W', 'třída 1.máje', [('2', 'Teplice n.B.'), ('3', 'Slavíč, škola'), ('4', 'Albert hyp.'), ('6', 'dům seniorů')]),
            ('W', 'pošta', [('536', 'Hustopeče n.B.'), ('949', 'Kelč')]), ('S', 'Kaufland', [('1', 'sport. areál'), ('13', 'Slavíč, škola')])]
    rows = sum(1 + len(p) for d, n, p in full)
    Wc, Hc = 250, rows * 13 + 6
    box(im, x, y, Wc, Hc, C.CARD_EDGE)
    box(im, x + 1, y + 1, Wc - 2, Hc - 2, C.CARD_BG)
    yy = y + 3
    for d, n, pairs in full:
        C.draw_dir(im, x + 5, yy + 2, d, 0x10284A)
        FONT.draw(im, x + 16, yy, n, 0x10284A)
        yy += 13
        for ln, dest in pairs:
            bg, fg = C.CHIP['bus']
            box(im, x + 16, yy, C.chip_w(ln), 11, bg)
            FONT.draw(im, x + 18, yy, ln, fg)
            FONT.draw(im, x + 46, yy, dest, 0x000000)
            yy += 13
    return im


# ---------------------------------------------------------------------------------------------
# windows

def big_window():
    return load('hr_haltbig0.png').crop((60, 40, 720, 600))


def board_trains(frame):
    win = big_window()
    rows = [dict(r) for r in TRAIN_ROWS]
    for r in rows:
        r['tstate'] = 'planned'
    rows[1]['tstate'] = 'confirmed'
    rows[2]['tstate'] = 'confirmed'
    rows[4]['tstate'] = 'changed'
    rows[4]['track'] = '3'
    rows[4]['was'] = '7'
    tmp = Image.new('RGB', (630, 600), (0, 0, 0))
    M.train_board(tmp, 0, 0, 630, rows, frame, with_track=True)
    win.paste(tmp.crop((0, 0, 630, 352)), (10, 198))
    return win


def board_buses():
    win = load('hr_haltbig1.png').crop((60, 40, 720, 460))
    rows = [('950', 'Bystřice pod Hostýnem, autobusové stanoviště', 'via Šromotovo náměstí, Pod farou', '7', 'now'),
            ('538', 'Horní Újezd, točna', 'via pošta, Pod Farou', '6', 'now'),
            ('927', 'Kopřivnice, Alšova', 'via Nový Jičín, aut.nádraží, Kopřivnice, žel.st.', '6', '19 min')]
    X, Y, W = 10, 198, 630
    PAD, LS_, VS = 4, LS, 4
    hh = LS_ + VS
    rh = LS_ + VS
    box(win, X, Y, W, hh + 3 * rh + VS, M.OTHER_BG)
    box(win, X, Y, W, hh, M.OTHER_HEAD_BG)
    lw = max(FONT.width('Line'), max(M.badge_w(r[0]) for r in rows))
    sw = max(FONT.width('Stand'), max(M.badge_w(r[3]) for r in rows))
    dw = max(FONT.width('Departure'), max(FONT.width(r[4]) for r in rows))
    c0 = X + 2 * PAD
    c1 = c0 + lw + 2 * PAD
    c3 = X + W - 2 * PAD - dw
    c2 = c3 - 2 * PAD - sw
    hy = Y + 2
    FONT.draw(win, c0, hy, 'Line', M.OTHER_HEAD)
    FONT.draw(win, c1, hy, 'Destination', M.OTHER_HEAD)
    M.text_c(win, c2, hy, sw, 'Stand', M.OTHER_HEAD)
    FONT.draw(win, c3 + dw - FONT.width('Departure'), hy, 'Departure', M.OTHER_HEAD)
    for i, (ln, d, via, stand, t) in enumerate(rows):
        ry = Y + hh + i * rh
        ty = ry + 2
        if i & 1:
            box(win, X, ry, W, rh, M.OTHER_STRIPE)
        M.draw_badge(win, c0, ty, ln, M.ROAD_BADGE, 0xFFFFFF)
        room = c2 - c1 - 2 * PAD
        dwid = FONT.ellipsis(win, c1, ty, room, d, M.OTHER_TEXT)
        if dwid + PAD < room:
            FONT.ellipsis(win, c1 + dwid + PAD, ty, room - dwid - PAD, via, M.OTHER_VIA)
        bw = M.badge_w(stand)
        sx = c2 + (sw - bw) // 2
        box(win, sx, ty - 1, bw, LS_ + 2, M.OTHER_HEAD_BG)
        FONT.draw(win, sx + PAD, ty, stand, M.OTHER_TEXT)
        FONT.draw(win, c3 + dw - FONT.width(t), ty, t, M.OTHER_TEXT)
    return win


def tabbar(win, active):
    W = win.size[0]
    box(win, 195, 146, W - 196, 19, M.WIN_BG)
    x = 195
    for t in ('Departure time', 'Platforms', 'Connections', 'Chart'):
        act = t == active
        w = FONT.width(t) + 8
        top = 148 if act else 150
        box(win, x + 1, top, w - 2, 1, M.TAB_LINE)
        box(win, x, top + 1, 1, 165 - top - 1, M.TAB_LINE)
        box(win, x + w - 1, top + 1, 1, 165 - top - 1, M.TAB_LINE)
        FONT.draw(win, x + 4, 151, t, M.TAB_ACTIVE_TEXT if act else M.BLACK)
        if act:
            box(win, x + 1, 165, w - 2, 1, M.WIN_BG)
        else:
            box(win, x, 165, w, 1, M.TAB_LINE)
        x += w
    box(win, x, 165, W - 1 - x, 1, M.TAB_LINE)


def inline_groups(im, x, y, groups, room):
    """lines by next stop as window text; returns False when cut"""
    x0 = x
    for i, (lines, nxt) in enumerate(groups):
        t = ', '.join(lines)
        need = FONT.width(t) + 12 + FONT.width(nxt) + (12 if i else 0)
        if x + need > x0 + room:
            FONT.draw(im, x + (6 if i else 0), y, '+%d' % (len(groups) - i), 0x5A5A5A)
            return False
        if i:
            box(im, x + 5, y, 1, 10, 0x8CC7E7)
            x += 12
        x = FONT.draw(im, x, y, t, M.BLACK)
        draw_arrow(im, x + 4, y, NAME_COL)
        x = FONT.draw(im, x + 12, y, nxt, NAME_COL)
    return True


def platforms_tab():
    win = big_window()
    W, H = win.size
    tabbar(win, 'Platforms')
    box(win, 1, 166, W - 2, H - 167 - 14, M.WIN_BG)
    box(win, 1, H - 15, W - 20, 14, M.WIN_BG)
    y = 172
    M.button(win, 10, y, 150, 'Map labels: this stop')
    M.button(win, 164, y, 150, 'Show: next stop')
    M.button(win, 318, y, 150, 'Numbers from markers')
    M.button(win, 472, y, 100, 'Number tracks')
    y += 24
    cols = [10, 62, 112]
    for cx_, h in zip(cols, ['No.', 'Kind', 'Lines boarding here, by next stop']):
        FONT.draw(win, cx_, y, h, M.TAB_ACTIVE_TEXT)
    y += LS + 3
    box(win, 8, y, W - 16, 1, M.TAB_LINE)
    y += 3
    rows = [(n, 'Track', g) for n, p, g in TRACKS] + [('1', 'Track', [])]
    order = ['1', '5', '6', '7', '8', '9', '11', '12', '13', '14', '15']
    st = dict((n, g) for n, p, g in STANDS)
    rows += [(n, 'Stand', st[n]) for n in order]
    for i, (n, kind, groups) in enumerate(rows):
        ry = y + i * 19
        if i == 1:
            box(win, 8, ry - 1, W - 16, 19, 0xC6E3F7)
        M.field(win, cols[0], ry, 40, 17, n)
        FONT.draw(win, cols[1], ry + 3, kind, M.BLACK)
        if groups:
            inline_groups(win, cols[2], ry + 3, groups, W - cols[2] - 14)
        else:
            FONT.draw(win, cols[2], ry + 3, 'no line boards here', 0x5A5A5A)
    return win


def platform_window(frame):
    W, H = 560, 232
    win = M.window_frame(W, H, 'Hranice na Moravě - track 7')
    X, Y, PW = 8, 30, W - 16
    PH = 66
    box(win, X, Y, PW, PH, M.TRAIN_BG)
    box(win, X + 6, Y + 6, 30, 30, M.TRAIN_TEXT)
    M.draw2x(win, X + 6 + (30 - 2 * FONT.width('7')) // 2, Y + 6 + 4, '7', M.TRAIN_BG)
    tx = X + 46
    ty = Y + 6
    x = FONT.draw(win, tx, ty, '11:29', M.TRAIN_TEXT) + 8
    x = FONT.draw(win, x, ty, 'EC 143 Ostravan', M.TRAIN_TEXT) + 8
    M.draw_badge(win, x, ty, 'Ex1', M.TRAIN_TEXT, M.TRAIN_BG)
    d = '+80 min'
    FONT.draw(win, X + PW - 6 - FONT.width(d), ty, d, M.TRAIN_DELAY)
    M.draw2x(win, tx, ty + LS + 2, 'Praha hl.n.', M.TRAIN_TEXT)
    FONT.draw(win, tx, ty + 3 * LS + 4, 'via Olomouc hlavní nádraží, Pardubice hlavní nádraží, Kolín', M.TRAIN_VIA)
    y = Y + PH + 8
    FONT.draw(win, X, y, 'Lines boarding here', M.TAB_ACTIVE_TEXT)
    y += LS + 3
    box(win, X, y, PW, 1, M.TAB_LINE)
    y += 4
    full = [('Olomouc hlavní nádraží', [('Ex1', 'Praha hl.n.'), ('RJ', 'Praha-Zahradní město')]),
            ('Přerov', [('Ex4', 'Wien Hbf, Budapest-Nyugati pu'), ('LE', 'Praha hl.n.')]),
            ('Lipník nad Bečvou', [('R8', 'Brno-Královo Pole')]),
            ('Bělotín', [('S3', 'Studénka')])]
    for nxt, pairs in full:
        draw_arrow(win, X + 2, y, NAME_COL)
        xx = FONT.draw(win, X + 10, y, nxt, NAME_COL)
        xx = X + 170
        for ln, dest in pairs:
            xx += M.draw_badge(win, xx, y, ln, M.TRAIN_BG, M.TRAIN_TEXT) + 4
            xx = FONT.draw(win, xx, y, dest, M.BLACK) + 12
        y += LS + 5
    y += 4
    M.button(win, X, y, 120, 'Show on the map')
    M.button(win, X + 124, y, 120, 'Stop window')
    return win


def convoy_bus():
    win = load('hr_cnv_bus.png').crop((60, 40, 596, 437))
    sx0, sy0, sw, sh = 398, 167, 128, 18
    PAD = 4
    box(win, sx0, sy0, sw, sh, M.OTHER_HEAD_BG)
    box(win, sx0 + 1, sy0 + 1, sw - 2, sh - 2, M.OTHER_BG)
    ty = sy0 + (sh - LS) // 2
    bw = M.draw_badge(win, sx0 + PAD, ty, '950', M.ROAD_BADGE, 0xFFFFFF)
    nw = FONT.width('7')
    tx = sx0 + sw - PAD - nw - 1
    box(win, tx - PAD, sy0 + 1, 1, sh - 2, M.OTHER_HEAD_BG)
    FONT.draw(win, tx, ty, '7', M.OTHER_TEXT)
    x = sx0 + PAD + bw + PAD
    FONT.ellipsis(win, x, ty, tx - PAD - 3 - x, 'Bystřice pod Hostýnem, autobusové stanoviště', M.OTHER_TEXT)
    return win


def objects(frame):
    im = load('c_station.png')
    a = frame == 0
    def gp(x, y):
        ox, oy = origin(ST_ORG, 0, 0)
        return (int(round(ST_ORG[2] + ((x - ST_ORG[0]) - (y - ST_ORG[1])) * 64 + 64)), int(round(ST_ORG[3] + ((x - ST_ORG[0]) + (y - ST_ORG[1])) * 32 + 96)))
    # rail: island between tracks 7 and 6, east end
    gx, gy = gp(5564.6, 2849.5)
    M.led_panel(im, gx, gy, 100, [[('num', '7'), ('text', '11:29'), ('badge', 'EX1'), ('text', 'PRAHA HL.N.' if a else 'VIA OLOMOUC')],
                                  [('num', '6'), ('text', '11:34'), ('badge', 'R8'), ('text', 'BOHUMÍN' if a else 'VIA SUCHDOL')]], 22)
    gx, gy = gp(5563.6, 2849.5)
    M.track_sign(im, gx, gy, '7', ['EX1', 'RJ'], 30)
    M.track_sign(im, gx, gy, '6', ['EX1', 'R8'], 16)
    rail = im.crop((1330, 380, 1650, 640))
    # road: stand 7 (the bus stands there) and stand 6, amber like the bus board
    keep = (M.TRAIN_BG, M.TRAIN_TEXT, M.TRAIN_VIA, M.TRAIN_DELAY, M.SZ_BLUE)
    M.TRAIN_BG, M.TRAIN_TEXT, M.TRAIN_VIA, M.TRAIN_DELAY, M.SZ_BLUE = M.OTHER_BG, M.OTHER_TEXT, M.OTHER_VIA, M.OTHER_DELAY, 0x1E64C8
    gx, gy = gp(5560.45, 2857.62)
    M.led_panel(im, gx, gy, 84, [[('num', '7'), ('badge', '950'), ('text', 'BYSTŘICE P.H.' if a else 'NOW')]], 20)
    gx, gy = gp(5561.5, 2855.62)
    M.track_sign(im, gx, gy, '6', ['532', '538', '927'], 18)
    gx, gy = gp(5559.5, 2857.62)
    M.track_sign(im, gx, gy, '9', ['929'], 18)
    M.TRAIN_BG, M.TRAIN_TEXT, M.TRAIN_VIA, M.TRAIN_DELAY, M.SZ_BLUE = keep
    road = im.crop((520, 540, 900, 760))
    return rail, road


def main():
    save(station_today(), 'station_today.png')
    save(station_labels(), 'station_labels.png')
    save(station_full(), 'station_full.png')
    save(station_combined(), 'station_combined.png')
    save(city_today(), 'city_today.png')
    save(city_labels(debug='debug' in sys.argv), 'city_labels.png')
    save(label_forms(), 'forms.png')
    for f in (0, 1):
        save(board_trains(f), 'board_trains_%d.png' % f)
        save(platform_window(f), 'platform_%d.png' % f)
    save(board_buses(), 'board_buses.png')
    save(platforms_tab(), 'platforms_tab.png')
    save(convoy_bus(), 'convoy_bus.png')
    for f in (0, 1):
        r, b = objects(f)
        save(r, 'obj_rail_%d.png' % f)
        save(b, 'obj_road_%d.png' % f)
    print('ok')


if __name__ == '__main__':
    main()
