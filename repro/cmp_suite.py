#!/usr/bin/env python3
import glob, sys
sys.path.insert(0,".")
from ref_decode import decode
bad=0; total=0; skipped=0
for p in sorted(glob.glob("../tests/images/*3p*.png")):
    total+=1
    try:
        w,h,ref,n=decode(p)
    except Exception as e:
        skipped+=1; print("skip",p,e); continue
    got=open(p+".spng.bin","rb").read()
    if got!=ref:
        bad+=1
        d=sum(1 for a,b in zip(ref,got) if a!=b)
        print("MISMATCH",p,"byte_diffs=",d)
print(f"PngSuite indexed: {total-bad-skipped} identical, {bad} mismatch, {skipped} skipped")
