#!/usr/bin/env python3
import glob, sys
sys.path.insert(0,".")
from ref_decode import decode
tot={4:0,2:0}; bad={4:0,2:0}
files=glob.glob("../tests/images/*3p*.png")+glob.glob("batch/*.png")
for p in files:
    w,h,rgba,n=decode(p)
    exp4=b"".join(rgba[i:i+3] for i in range(0,len(rgba),4))
    exp2=b"".join(bytes((c,c)) for c in rgba)
    for fmt,exp in ((4,exp4),(2,exp2)):
        tot[fmt]+=1
        got=open(f"{p}.f{fmt}.bin","rb").read()
        if got!=exp: bad[fmt]+=1; print("MISMATCH",p,fmt)
print(f"RGB8:  {tot[4]-bad[4]}/{tot[4]} identical")
print(f"RGBA16:{tot[2]-bad[2]}/{tot[2]} identical")
