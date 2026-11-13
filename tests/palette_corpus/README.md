# Palette / tRNS regression corpus

Indexed-color (color type 3) PNGs for the palette index and transparency
fixes. These files are self-contained inputs for `palette_index_test` and for
manual decoder comparison; they do not depend on libpng.

## Valid images (must decode pixel-identical to a spec reference)

- `case1_trns_bleed.png` — 4-bit, 4 PLTE entries, tRNS with a distinct alpha
  per index. Entries 0 and 1 share identical RGB but have different alpha, so
  any RGB-keyed transparency handling collapses them; correct behavior keeps
  the per-index alpha.
- `case2_edge_fringe.png` — 1-bit, width 11. The last packed byte carries 7
  padding bits; the decoder must ignore them and emit no right-edge fringe.
- `case4_short_trns.png` — tRNS is shorter than PLTE; indices without a tRNS
  entry must be fully opaque (alpha 255).

## Malformed images (must fail with `SPNG_EPLTE_IDX`, no out-of-bounds read)

- `case3_bad_index.png` — 4-bit frame containing index 3 with a 2-entry PLTE.
- `case3_bad_index_il.png` — same, Adam7 interlaced.
- `case3b_bad_index_8bit.png` — 8-bit frame containing index 200 with a
  2-entry PLTE.
- `case3c_bad_last.png` — invalid index at the final (right-edge) pixel.
- `edge_bad.png` — invalid index at the right edge of a sub-byte row.

`SPNG_FMT_PNG` / `SPNG_FMT_RAW` are index-passthrough formats that never
consult the palette LUT, so they intentionally still return success; only the
expanded formats (`RGBA8`, `RGB8`, `RGBA16`) reject out-of-range indices.
