#!/usr/bin/env python3
import subprocess, sys, os, glob
sys.path.insert(0,".")
from ref_decode import decode

cases = [
 "case1_trns_bleed.png","case1_trns_bleed_il.png",
 "case2_edge_fringe.png","case2_edge_fringe_il.png","case2b_edge_2bit.png",
 "case3_bad_index.png","case3_bad_index_il.png","case3b_bad_index_8bit.png",
 "case3c_bad_last.png","case4_short_trns.png","edge_bad.png",
]
def run(exe, p):
    out = subprocess.run([exe,p],capture_output=True,text=True).stdout
    rc = int(out.split("decode_rc=")[1].split(" ")[0])
    return rc

print(f"{'image':28} {'ref':>6} {'orig_rc':>8} {'fixed_rc':>9} {'fixed_vs_ref':>13}")
for p in cases:
    ref_reject = False
    try:
        w,h,ref,n = decode(p)
        ref_status = "ok"
    except ValueError:
        ref_reject = True; ref_status = "reject"
    rc0 = run("./harness_orig", p)
    rc1 = run("./harness", p)
    if ref_reject:
        match = "n/a"
    else:
        got = open(p+".spng.bin","rb").read()
        match = "IDENTICAL" if got==ref else "DIFF"
    print(f"{p:28} {ref_status:>6} {rc0:>8} {rc1:>9} {match:>13}")

# Aggregate batch + suite pixel equality for the fixed build
for label, files in [("random valid batch", sorted(glob.glob("batch/*.png"))),
                     ("PngSuite indexed", sorted(glob.glob('../tests/images/*3p*.png')))]:
    mism = 0; tot = 0
    for p in files:
        tot += 1
        w,h,ref,n = decode(p)
        got = open(p+".spng.bin","rb").read()
        if got != ref: mism += 1
    print(f"{label}: {tot-mism}/{tot} pixel-identical to reference")
