#!/usr/bin/env python3
"""Minimal spec-compliant reference decoder for indexed PNGs.

Rules implemented (PNG spec):
  * PLTE index out of range -> error, no memory read
  * tRNS type 3: alpha per index; missing tRNS entry => 255
  * sub-byte samples: padding bits ignored
  * Adam7 interlacing
Output format: RGBA8, top-to-bottom.
"""
import struct, zlib, sys

ADAM7 = ((0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),
         (0,2,2,4),(1,0,2,2),(0,1,1,2))

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
    if pa <= pb and pa <= pc: return a
    if pb <= pc: return b
    return c

def parse(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    chunks = []
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos+4])[0]
        tag = data[pos+4:pos+8]
        body = data[pos+8:pos+8+ln]
        pos += 12 + ln
        chunks.append((tag, body))
        if tag == b"IEND": break
    info = {}
    idat = b""
    for tag, body in chunks:
        if tag == b"IHDR":
            (w, h, depth, ctype, comp, filt, interlace) = struct.unpack(">IIBBBBB", body)
            info.update(w=w, h=h, depth=depth, ctype=ctype, interlace=interlace)
        elif tag == b"PLTE":
            info["plte"] = [tuple(body[i:i+3]) for i in range(0, len(body), 3)]
        elif tag == b"tRNS":
            info["trns"] = list(body)
        elif tag == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    return info, raw

def unpack_samples(packed, depth, count):
    out = []
    if depth == 8:
        return list(packed[:count])
    spb = 8 // depth
    mask = (1 << depth) - 1
    for i in range(count):
        byte = packed[i // spb]
        shift = 8 - depth - (i % spb) * depth
        out.append((byte >> shift) & mask)
    return out

def defilter(raw, width_bytes, height):
    """Non-interlaced: rows prefixed by filter byte."""
    stride = width_bytes
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    bpp = 1  # indexed images: 1 byte per pixel max
    for _ in range(height):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos+stride]); pos += stride
        for i in range(stride):
            a = line[i-bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i-bpp] if i >= bpp else 0
            x = line[i]
            if f == 1: x = (x + a) & 255
            elif f == 2: x = (x + b) & 255
            elif f == 3: x = (x + ((a + b) >> 1)) & 255
            elif f == 4: x = (x + paeth(a, b, c)) & 255
            elif f != 0: raise ValueError("bad filter")
            line[i] = x
        out += line
        prev = line
    return bytes(out)

def decode(path):
    info, raw = parse(path)
    assert info["ctype"] == 3, "only indexed supported"
    w, h, depth = info["w"], info["h"], info["depth"]
    plte = info["plte"]
    trns = info.get("trns", [])
    n = len(plte)

    def rgba(idx):
        if idx >= n:
            raise ValueError(f"palette index {idx} out of range (n_entries={n})")
        r, g, b = plte[idx]
        a = trns[idx] if idx < len(trns) else 255
        return (r, g, b, a)

    image = [[None]*w for _ in range(h)]

    if info["interlace"] == 0:
        row_bytes = (w * depth + 7) // 8
        planes = defilter(raw, row_bytes, h)
        for y in range(h):
            idxs = unpack_samples(planes[y*row_bytes:(y+1)*row_bytes], depth, w)
            for x, idx in enumerate(idxs):
                image[y][x] = rgba(idx)
    else:
        pos = 0
        for p in range(7):
            x0, y0, dx, dy = ADAM7[p]
            pw = max(0, (w - x0 + dx - 1) // dx)
            ph = max(0, (h - y0 + dy - 1) // dy)
            if pw == 0 or ph == 0: continue
            row_bytes = (pw * depth + 7) // 8
            prev = bytearray(row_bytes)
            for yy in range(ph):
                f = raw[pos]; pos += 1
                line = bytearray(raw[pos:pos+row_bytes]); pos += row_bytes
                for i in range(row_bytes):
                    a = line[i-1] if i >= 1 else 0
                    b = prev[i]; c = prev[i-1] if i >= 1 else 0
                    x = line[i]
                    if f == 1: x = (x+a)&255
                    elif f == 2: x = (x+b)&255
                    elif f == 3: x = (x+((a+b)>>1))&255
                    elif f == 4: x = (x+paeth(a,b,c))&255
                    elif f != 0: raise ValueError("bad filter")
                    line[i] = x
                prev = line
                idxs = unpack_samples(bytes(line), depth, pw)
                for xx, idx in enumerate(idxs):
                    image[y0+yy*dy][x0+xx*dx] = rgba(idx)
    flat = bytearray()
    for row in image:
        for px in row:
            flat += bytes(px)
    return w, h, bytes(flat), n

if __name__ == "__main__":
    for p in sys.argv[1:]:
        try:
            w, h, px, n = decode(p)
            print(f"{p}: OK {w}x{h} n_entries={n} bytes={len(px)}")
        except ValueError as e:
            print(f"{p}: REF_ERROR {e}")
