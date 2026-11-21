#!/usr/bin/env python3
"""Placeholder images for the fork's platform display and track/stand sign (pak128, documentation/fork-platforms-plan.md).

The game draws the panel, the plate and their text itself (gui/platform_cards.cc), so the object's own images only
need a foot on the ground: row 0 holds four of them (N, S, W, E, like a sign). Row 1 holds the toolbar icons, drawn in
the top left 32x32 pixels of their tile: display, track sign, stand sign. Only needs the Python standard library.
Real art replaces these; keep the layout.
"""
import struct
import zlib

TILE = 128
TRANSPARENT = (231, 255, 255)  # simutrans transparent colour


def png(path, width, height, pixels):
	raw = b''.join(b'\x00' + bytes(v for p in pixels[y*width:(y+1)*width] for v in p) for y in range(height))
	def chunk(kind, data):
		return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
	with open(path, 'wb') as f:
		f.write(b'\x89PNG\r\n\x1a\n')
		f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
		f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
		f.write(chunk(b'IEND', b''))


W, H = 4 * TILE, 2 * TILE
pix = [TRANSPARENT] * (W * H)


def rect(x0, y0, x1, y1, c):
	for y in range(y0, y1):
		for x in range(x0, x1):
			pix[y * W + x] = c


# feet: a small grey base at the middle of the tile (the posts are drawn by the game)
for i in range(4):
	ox = i * TILE
	rect(ox + 62, 94, ox + 66, 97, (90, 90, 90))
	rect(ox + 63, 93, ox + 65, 94, (154, 154, 154))

# icons
NAVY, WHITE, BLUE, GREY, YELLOW = (11, 31, 75), (255, 255, 255), (0, 87, 168), (90, 90, 90), (255, 208, 48)
# display: a navy panel on two posts with two rows of light
ox, oy = 0, TILE
rect(ox + 3, oy + 8, ox + 29, oy + 20, NAVY)
rect(ox + 5, oy + 11, ox + 8, oy + 14, WHITE)
rect(ox + 10, oy + 11, ox + 26, oy + 12, WHITE)
rect(ox + 10, oy + 15, ox + 22, oy + 16, YELLOW)
rect(ox + 6, oy + 20, ox + 8, oy + 30, GREY)
rect(ox + 24, oy + 20, ox + 26, oy + 30, GREY)
# track sign: a blue plate with a white 1, a dark strip with a chip
ox = TILE
rect(ox + 4, oy + 6, ox + 15, oy + 19, WHITE)
rect(ox + 5, oy + 7, ox + 14, oy + 18, BLUE)
rect(ox + 9, oy + 9, ox + 11, oy + 16, WHITE)
rect(ox + 15, oy + 8, ox + 29, oy + 17, (42, 42, 42))
rect(ox + 17, oy + 10, ox + 27, oy + 15, WHITE)
rect(ox + 8, oy + 19, ox + 10, oy + 30, GREY)
# stand sign: the same in bus blue
ox = 2 * TILE
rect(ox + 4, oy + 6, ox + 15, oy + 19, WHITE)
rect(ox + 5, oy + 7, ox + 14, oy + 18, (30, 100, 200))
rect(ox + 8, oy + 9, ox + 12, oy + 10, WHITE)
rect(ox + 8, oy + 9, ox + 9, oy + 16, WHITE)
rect(ox + 8, oy + 12, ox + 12, oy + 13, WHITE)
rect(ox + 15, oy + 8, ox + 29, oy + 17, (42, 42, 42))
rect(ox + 17, oy + 10, ox + 27, oy + 15, (30, 100, 200))
rect(ox + 8, oy + 19, ox + 10, oy + 30, GREY)

png('platform_objects.png', W, H, pix)
print('platform_objects.png')
