#!/usr/bin/env python3
import os, random, sys
sys.path.insert(0,".")
from gen_png import write_png
random.seed(1234)
os.makedirs("batch", exist_ok=True)
idx=0
for depth in (1,2,4,8):
    maxent=1<<depth
    for interlace in (0,1):
        for trial in range(6):
            n=random.randint(1,maxent)
            pal=[(random.randrange(256),random.randrange(256),random.randrange(256)) for _ in range(n)]
            # tRNS: absent, full length, or partial length
            tmode=random.choice(("none","full","partial"))
            if tmode=="none": alphas=None
            elif tmode=="full": alphas=[random.randrange(256) for _ in range(n)]
            else: alphas=[random.randrange(256) for _ in range(max(1,n//2))]
            w=random.randint(1,29); h=random.randint(1,9)
            px=[random.randrange(n) for _ in range(w*h)]  # always valid indices
            name=f"batch/t{idx:03d}_d{depth}_n{n}_il{interlace}_{tmode}.png"
            write_png(name,w,h,depth,pal,alphas,px,interlace)
            idx+=1
print("generated",idx,"valid images")
