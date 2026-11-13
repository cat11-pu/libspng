#!/usr/bin/env python3
import os, random, sys
sys.path.insert(0,".")
from gen_png import write_png
random.seed(99)
os.makedirs("bad", exist_ok=True)
idx=0
for depth in (1,2,4,8):
    maxent=1<<depth
    for interlace in (0,1):
        for trial in range(5):
            n=random.randint(1,maxent-1)  # ensure room for an out-of-range index
            pal=[(random.randrange(256),)*3 for _ in range(n)]
            alphas=[random.randrange(256) for _ in range(n)] if trial%2 else None
            w=random.randint(1,25); h=random.randint(1,5)
            px=[random.randrange(n) for _ in range(w*h)]
            # plant 1-3 invalid indices, including possibly the final pixel
            for _ in range(random.randint(1,3)):
                px[random.randrange(len(px))]=random.randrange(n,maxent)
            name=f"bad/b{idx:03d}_d{depth}_n{n}_il{interlace}.png"
            write_png(name,w,h,depth,pal,alphas,px,interlace)
            idx+=1
print("generated",idx,"malformed images")
