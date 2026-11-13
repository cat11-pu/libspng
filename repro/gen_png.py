#!/usr/bin/env python3
"""Generate indexed PNG test cases for palette/tRNS bugs."""
import struct, zlib, sys, os

def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

def ihdr(w, h, depth, color_type=3, interlace=0):
    return chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, color_type, 0, 0, interlace))

def plte(entries):
    data = b"".join(struct.pack("BBB", r, g, b) for (r, g, b) in entries)
    return chunk(b"PLTE", data)

def trns(alphas):
    return chunk(b"tRNS", bytes(alphas))

def idat(rows, depth, interlace=0):
    if interlace == 0:
        raw = b""
        for row in rows:
            raw += b"\x00" + pack_row(row, depth)
    else:
        raw = adam7(rows, depth)
    return chunk(b"IDAT", zlib.compress(raw, 9))

def pack_row(samples, depth):
    if depth == 8:
        return bytes(samples)
    out = bytearray()
    spb = 8 // depth
    mask = (1 << depth) - 1
    for i in range(0, len(samples), spb):
        byte = 0
        for j in range(spb):
            if i + j < len(samples):
                byte = (byte << depth) | (samples[i + j] & mask)
            else:
                byte <<= depth
        out.append(byte)
    return bytes(out)

# Adam7 pass geometry
ADAM7 = ((0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),
         (0,2,2,4),(1,0,2,2),(0,1,1,2))

def pass_dims(w, h, p):
    x0, y0, dx, dy = ADAM7[p]
    pw = (w - x0 + dx - 1) // dx if w > x0 else 0
    ph = (h - y0 + dy - 1) // dy if h > y0 else 0
    return pw, ph

def adam7(rows, depth):
    h = len(rows); w = len(rows[0]); raw = b""
    for p in range(7):
        x0, y0, dx, dy = ADAM7[p]
        pw, ph = pass_dims(w, h, p)
        if pw == 0 or ph == 0:
            continue
        for yy in range(ph):
            samples = [rows[y0 + yy * dy][x0 + xx * dx] for xx in range(pw)]
            raw += b"\x00" + pack_row(samples, depth)
    return raw

def write_png(path, w, h, depth, entries, alphas, pixels, interlace=0):
    rows = [pixels[r*w:(r+1)*w] for r in range(h)]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(ihdr(w, h, depth, 3, interlace))
        f.write(plte(entries))
        if alphas is not None:
            f.write(trns(alphas))
        f.write(idat(rows, depth, interlace))
        f.write(chunk(b"IEND", b""))

# ---- Case 1: tRNS alpha bleed ("颜色串了") ----
# 4-entry palette, 4 tRNS entries with distinct alpha values.
# Two palette entries share identical RGB but different alpha; a naive
# decoder that keys on RGB instead of per-index alpha collapses them.
palette4 = [(255, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]
alpha4 = [0, 128, 200, 255]
pixels1 = [0, 1, 2, 3, 3, 2, 1, 0,
           1, 0, 3, 2, 2, 3, 0, 1]
write_png("case1_trns_bleed.png", 8, 2, 4, palette4, alpha4, pixels1)
write_png("case1_trns_bleed_il.png", 8, 2, 4, palette4, alpha4, pixels1, interlace=1)

# ---- Case 2: right edge color fringe (width not multiple of samples/byte) ----
# 1-bit depth, width 11 -> last byte carries 3 padding bits.
palette2 = [(10, 20, 30), (200, 100, 50)]
alpha2 = [0, 255]
pixels2 = [0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0,
           1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1]
write_png("case2_edge_fringe.png", 11, 2, 1, palette2, alpha2, pixels2)
write_png("case2_edge_fringe_il.png", 11, 2, 1, palette2, alpha2, pixels2, interlace=1)
# 2-bit variant, width 7 -> padding bits too
pixels2b = [0, 1, 2, 3, 3, 2, 1,
            2, 3, 0, 1, 1, 0, 3]
write_png("case2b_edge_2bit.png", 7, 2, 2, palette4, alpha4, pixels2b)

# ---- Case 3: malformed index >= n_entries (no OOB read) ----
# 2 palette entries but a 4-bit frame contains index 3.
bad_pixels = [0, 1, 3, 0, 1, 2, 1, 0]
write_png("case3_bad_index.png", 8, 1, 4, palette2, alpha2, bad_pixels)
write_png("case3_bad_index_il.png", 8, 1, 4, palette2, alpha2, bad_pixels, interlace=1)
# 8-bit variant with index 200
bad8 = [0, 1, 200, 1, 0]
write_png("case3b_bad_index_8bit.png", 5, 1, 8, palette2, alpha2, bad8)
# malformed index as the very last pixel (edge), 1-bit cannot exceed 1 so use 4-bit width 9
bad_last = [0, 1, 0, 1, 0, 1, 0, 1, 3]
write_png("case3c_bad_last.png", 9, 1, 4, palette2, alpha2, bad_last)

# ---- Case 4: tRNS shorter than PLTE: remaining indices must be opaque 255 ----
short_alpha = [0, 64]  # entries 2,3 default to 255
write_png("case4_short_trns.png", 8, 1, 4, palette4, short_alpha, pixels1[:8])

print("generated:", sorted(os.listdir(".")))
