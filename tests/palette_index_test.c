/* Regression tests for indexed (palette + tRNS) decoding.
 *
 * Covers:
 *   1. tRNS alpha is applied per palette index, including entries that
 *      share identical RGB but have distinct alpha.
 *   2. Sub-byte frames whose last byte carries padding bits decode without
 *      any right-edge color fringe.
 *   3. A palette index >= PLTE n_entries is rejected with SPNG_EPLTE_IDX
 *      instead of reading an unused decode-LUT slot (no out-of-bounds read).
 *
 * The test images are synthesized in memory so the test has no data-file or
 * libpng dependency. Builds against the same zlib/miniz backend as the
 * library.
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SPNG_USE_MINIZ
#include <miniz.h>
#else
#include <zlib.h>
#endif

#include "spng.h"

static uint32_t be32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;  p[3] = v & 0xff;
    return 4;
}

static size_t put_chunk(uint8_t *out, const char tag[4], const uint8_t *body, size_t len)
{
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    be32(out, (uint32_t)len);
    memcpy(out + 4, tag, 4);
    if(len) memcpy(out + 8, body, len);
    crc = (uint32_t)crc32(crc, (const Bytef*)(out + 4), 4 + (uInt)len);
    be32(out + 8 + len, crc);
    return 12 + len;
}

static void pack_samples(uint8_t *out, const uint8_t *samples, uint32_t count, unsigned depth)
{
    if(depth == 8) { memcpy(out, samples, count); return; }
    unsigned spb = 8 / depth, mask = (1u << depth) - 1;
    memset(out, 0, (count * depth + 7) / 8);
    for(uint32_t i = 0; i < count; i++)
    {
        unsigned shift = 8 - depth - (i % spb) * depth;
        out[i / spb] |= (uint8_t)((samples[i] & mask) << shift);
    }
}

/* Build a non-interlaced indexed PNG with an optional tRNS chunk. */
static size_t make_indexed_png(uint8_t *out, uint32_t w, uint32_t h, unsigned depth,
                               const uint8_t *plte, uint32_t n_entries,
                               const uint8_t *alphas, uint32_t n_alpha,
                               const uint8_t *indices)
{
    static const uint8_t sig[8] = { 137,80,78,71,13,10,26,10 };
    size_t pos = 0;
    memcpy(out, sig, 8); pos += 8;

    uint8_t ihdr[13];
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = (uint8_t)depth; ihdr[9] = 3; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    pos += put_chunk(out + pos, "IHDR", ihdr, 13);
    pos += put_chunk(out + pos, "PLTE", plte, n_entries * 3);
    if(n_alpha) pos += put_chunk(out + pos, "tRNS", alphas, n_alpha);

    size_t row_bytes = (w * depth + 7) / 8;
    uint8_t *raw = malloc(h * (row_bytes + 1));
    for(uint32_t y = 0; y < h; y++)
    {
        raw[y * (row_bytes + 1)] = 0;
        pack_samples(raw + y * (row_bytes + 1) + 1, indices + y * w, w, depth);
    }
    uLong comp_len = compressBound(h * (row_bytes + 1));
    uint8_t *comp = malloc(comp_len);
    if(compress2(comp, &comp_len, raw, h * (row_bytes + 1), 9) != Z_OK) abort();
    pos += put_chunk(out + pos, "IDAT", comp, comp_len);
    pos += put_chunk(out + pos, "IEND", NULL, 0);
    free(raw); free(comp);
    return pos;
}

static int decode_rgba8(const uint8_t *png, size_t len, uint8_t *out, size_t out_len, int *rc_out)
{
    spng_ctx *ctx = spng_ctx_new(0);
    if(spng_set_png_buffer(ctx, png, len)) { spng_ctx_free(ctx); return -1; }
    int rc = spng_decode_image(ctx, out, out_len, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS);
    *rc_out = rc;
    spng_ctx_free(ctx);
    return rc;
}

static int failures = 0;

#define CHECK(cond, msg) do { \
    if(cond) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); failures++; } \
} while(0)

/* Two entries share RGB (255,0,0) but differ in alpha (0 vs 128). */
static void test_trns_per_index(void)
{
    const uint8_t plte[12] = { 255,0,0, 255,0,0, 0,255,0, 0,0,255 };
    const uint8_t alpha[4] = { 0, 128, 200, 255 };
    const uint8_t idx[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
    const uint8_t expect[32] = {
        255,0,0,0, 255,0,0,128, 0,255,0,200, 0,0,255,255,
        0,0,255,255, 0,255,0,200, 255,0,0,128, 255,0,0,0
    };
    uint8_t png[512], out[32]; int rc;
    size_t len = make_indexed_png(png, 8, 1, 4, plte, 4, alpha, 4, idx);
    decode_rgba8(png, len, out, sizeof out, &rc);
    CHECK(rc == 0, "tRNS image decodes successfully");
    CHECK(memcmp(out, expect, 32) == 0, "per-index alpha (no RGB keying bleed)");
}

/* tRNS shorter than PLTE: remaining indices must be fully opaque. */
static void test_short_trns(void)
{
    const uint8_t plte[12] = { 255,0,0, 255,0,0, 0,255,0, 0,0,255 };
    const uint8_t alpha[2] = { 0, 64 };
    const uint8_t idx[4] = { 0, 1, 2, 3 };
    const uint8_t expect[16] = {
        255,0,0,0, 255,0,0,64, 0,255,0,255, 0,0,255,255
    };
    uint8_t png[512], out[16]; int rc;
    size_t len = make_indexed_png(png, 4, 1, 4, plte, 4, alpha, 2, idx);
    decode_rgba8(png, len, out, sizeof out, &rc);
    CHECK(rc == 0, "short tRNS decodes successfully");
    CHECK(memcmp(out, expect, 16) == 0, "missing tRNS entries default to alpha 255");
}

/* 1-bit frame, width 11 (last byte has padding bits): no edge fringe. */
static void test_padding_bits_edge(void)
{
    const uint8_t plte[6] = { 10,20,30, 200,100,50 };
    const uint8_t alpha[2] = { 0, 255 };
    const uint8_t idx[11] = { 0,1,0,1,1,0,1,0,1,1,0 };
    uint8_t expect[44];
    for(int i = 0; i < 11; i++)
    {
        expect[i*4+0] = idx[i] ? 200 : 10;
        expect[i*4+1] = idx[i] ? 100 : 20;
        expect[i*4+2] = idx[i] ? 50 : 30;
        expect[i*4+3] = idx[i] ? 255 : 0;
    }
    uint8_t png[512], out[44]; int rc;
    size_t len = make_indexed_png(png, 11, 1, 1, plte, 2, alpha, 2, idx);
    decode_rgba8(png, len, out, sizeof out, &rc);
    CHECK(rc == 0, "sub-byte frame decodes successfully");
    CHECK(memcmp(out, expect, 44) == 0, "padding bits do not create an edge fringe");
}

/* Malformed: index >= n_entries across depths and at the final pixel. */
static void test_bad_index_rejected(unsigned depth, uint32_t n_entries,
                                    uint32_t w, const uint8_t *idx, const char *name)
{
    uint32_t maxent = 1u << depth;
    uint8_t plte[256 * 3];
    for(uint32_t i = 0; i < n_entries; i++)
    { plte[i*3] = (uint8_t)i; plte[i*3+1] = 0; plte[i*3+2] = 0; }
    uint8_t png[1024], out[256]; int rc = 0;
    size_t len = make_indexed_png(png, w, 1, depth, plte, n_entries, NULL, 0, idx);
    (void)maxent;
    decode_rgba8(png, len, out, sizeof out, &rc);
    CHECK(rc == SPNG_EPLTE_IDX, name);
}

int main(void)
{
    printf("palette + tRNS regression tests\n");

    test_trns_per_index();
    test_short_trns();
    test_padding_bits_edge();

    {
        const uint8_t idx4[8] = { 0,1,3,0,1,1,0,1 };       /* index 3, n_entries=2 */
        test_bad_index_rejected(4, 2, 8, idx4, "4-bit out-of-range index rejected");
    }
    {
        const uint8_t idx8[5] = { 0,1,200,1,0 };           /* index 200, n_entries=2 */
        test_bad_index_rejected(8, 2, 5, idx8, "8-bit out-of-range index rejected");
    }
    {
        const uint8_t idx_last[9] = { 0,1,0,1,0,1,0,1,3 }; /* bad index at right edge */
        test_bad_index_rejected(4, 2, 9, idx_last, "out-of-range index at final pixel rejected");
    }
    {
        const uint8_t idx2[8] = { 0,1,2,1,0,1,2,1 };       /* index 2, n_entries=2 */
        test_bad_index_rejected(2, 2, 8, idx2, "2-bit out-of-range index rejected");
    }

    if(failures) { printf("\n%d test(s) FAILED\n", failures); return 1; }
    printf("\nall palette index tests passed\n");
    return 0;
}
