#!/usr/bin/env python3
# Verify RGB8 and RGBA16 outputs against an independent expected expansion.
import glob, struct, subprocess, sys
sys.path.insert(0,".")
from ref_decode import decode

def expand(p, fmt):
    w,h,rgba,n = decode(p)
    if fmt=="RGB8":
        return b"".join(rgba[i:i+3] for i in range(0,len(rgba),4))
    # RGBA16: replicate each byte
    return b"".join(bytes((c,c)) for c in rgba)

import ctypes
# reuse a small C dumper instead: call h3-style binary via subprocess
