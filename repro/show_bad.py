#!/usr/bin/env python3
# Show what current spng emits for out-of-range indices vs. what a valid
# last-in-range index looks like, and where the bad pixel lands.
import subprocess, struct, sys, os
sys.path.insert(0, ".")
from gen_png import write_png

# 1-bit image, width 9 -> last byte has 7 padding bits; PLTE has 2 entries.
# A crafted frame puts index 3 (4-bit) near the right edge.
pal = [(10,20,30),(200,100,50)]
alpha=[0,255]
# width 9, 4-bit: indices [0,1,0,1,0,1,0,1,3] -> bad index at last pixel (right edge)
write_png("edge_bad.png", 9, 1, 4, pal, alpha, [0,1,0,1,0,1,0,1,3])
subprocess.run(["./harness","edge_bad.png"],check=True)
got=open("edge_bad.png.spng.bin","rb").read()
print("emitted RGBA pixels:")
for x in range(9):
    print(f"  x={x}: {got[x*4:x*4+4].hex()}")
print("Last pixel (right edge) used index 3 with n_entries=2 -> spng silently",
      "emits", got[8*4:9*4].hex(), "(zeroed LUT slot), no error returned")
