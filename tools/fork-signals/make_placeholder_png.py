#!/usr/bin/env python3
"""Draws placeholder images for the fork's platform signal and station boundary (pak64).

Writes platform_signal.png (8 tiles: red N,S,W,E then green N,S,W,E) and
station_boundary.png (4 tiles: N,S,W,E). Only needs the Python standard library.
Real art for other paksets replaces these; keep the same tile order.
"""
import struct
import zlib

TILE = 64
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


def canvas(tiles):
	return [TRANSPARENT] * (tiles * TILE * TILE), tiles * TILE


def rect(pix, width, x0, y0, x1, y1, colour):
	for y in range(y0, y1):
		for x in range(x0, x1):
			pix[y * width + x] = colour


# where the object stands in the tile for each direction (N, S, W, E), like the pak64 signals
SPOTS = [(40, 22), (18, 34), (14, 20), (44, 36)]


def platform_signal():
	pix, width = canvas(8)
	for state, lamp in enumerate([(220, 0, 0), (0, 200, 0)]):
		for d, (x, y) in enumerate(SPOTS):
			ox = (state * 4 + d) * TILE
			rect(pix, width, ox + x, y, ox + x + 2, y + 22, (60, 60, 60))           # pole
			rect(pix, width, ox + x - 3, y - 8, ox + x + 5, y + 2, (30, 60, 200))   # blue head: platform signal
			rect(pix, width, ox + x - 1, y - 6, ox + x + 3, y, lamp)                # lamp
	png('platform_signal.png', width, TILE, pix)


def station_boundary():
	pix, width = canvas(4)
	for d, (x, y) in enumerate(SPOTS):
		ox = d * TILE
		rect(pix, width, ox + x, y, ox + x + 2, y + 22, (60, 60, 60))                 # post
		for row in range(10):                                                          # trapezoid board
			half = 3 + row // 2
			rect(pix, width, ox + x + 1 - half, y - 10 + row, ox + x + 1 + half, y - 9 + row, (0, 0, 0) if row in (0, 9) else (255, 255, 255))
	png('station_boundary.png', width, TILE, pix)


platform_signal()
station_boundary()
