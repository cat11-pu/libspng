#!/usr/bin/env python3
import glob, sys
sys.path.insert(0,".")
from ref_decode import decode
bad=0; total=0
for p in sorted(glob.glob("batch/*.png")):
    total+=1
    w,h,ref,n=decode(p)
    got=open(p+".spng.bin","rb").read()
    if got!=ref:
        bad+=1; print("MISMATCH",p,len(ref),len(got))
print(f"compared {total} images: {total-bad} pixel-identical, {bad} mismatched")
