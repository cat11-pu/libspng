#!/usr/bin/env python3
import sys, os
sys.path.insert(0, ".")
from ref_decode import decode

for path in sys.argv[1:]:
    try:
        w, h, ref, n = decode(path)
    except ValueError as e:
        print(f"{path}: reference rejected ({e}); spng output comparison skipped")
        continue
    binp = path + ".spng.bin"
    if not os.path.exists(binp):
        print(f"{path}: no spng output (decode failed)")
        continue
    got = open(binp, "rb").read()
    if len(got) != len(ref):
        print(f"{path}: LENGTH MISMATCH ref={len(ref)} spng={len(got)}")
        continue
    diffs = [(i, ref[i], got[i]) for i in range(len(ref)) if ref[i] != got[i]]
    if not diffs:
        print(f"{path}: IDENTICAL ({w}x{h}, {len(ref)} bytes, {len(ref)//4} px)")
    else:
        px_bad = sorted({i//4 for i,_,_ in diffs})
        print(f"{path}: {len(diffs)} byte diffs / {len(px_bad)} pixels of {w*h}")
        for i, r, g in diffs[:12]:
            px, ch = i//4, "RGBA"[i%4]
            x, y = px % w, px // w
            print(f"   px({x},{y}) {ch}: ref={r} spng={g}  ref_rgba={ref[px*4:px*4+4].hex()} spng_rgba={got[px*4:px*4+4].hex()}")
