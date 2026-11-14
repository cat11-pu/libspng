/* Verify spng_decode_image_region() against full decode + crop, pixel by pixel.
   Covers non-interlaced and Adam7 images, all color types, clipping and
   invalid regions. */
#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int load_file(const char *path, unsigned char **buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if(!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(n <= 0) { fclose(f); return -1; }
    *len = (size_t)n;
    *buf = malloc(*len);
    if(fread(*buf, 1, *len, f) != *len) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static const int fmts[] = {
    SPNG_FMT_RGBA8, SPNG_FMT_RGBA16, SPNG_FMT_RGB8,
    SPNG_FMT_PNG, SPNG_FMT_RAW, SPNG_FMT_G8, SPNG_FMT_GA8, SPNG_FMT_GA16
};

static size_t pixel_size_of(int fmt, const struct spng_ihdr *ihdr)
{
    switch(fmt)
    {
        case SPNG_FMT_RGBA8: return 4;
        case SPNG_FMT_RGBA16: return 8;
        case SPNG_FMT_RGB8: return 3;
        case SPNG_FMT_G8: return 1;
        case SPNG_FMT_GA8: return 2;
        case SPNG_FMT_GA16: return 4;
        case SPNG_FMT_PNG:
        case SPNG_FMT_RAW:
        {
            if(ihdr->bit_depth < 8) return 0;
            int channels;
            switch(ihdr->color_type)
            {
                case 0: channels = 1; break;
                case 2: channels = 3; break;
                case 3: channels = 1; break;
                case 4: channels = 2; break;
                case 6: channels = 4; break;
                default: channels = 1;
            }
            return (size_t)channels * (ihdr->bit_depth / 8);
        }
        default: return 0;
    }
}

static int run_one(const char *path)
{
    unsigned char *png = NULL;
    size_t png_len = 0;
    if(load_file(path, &png, &png_len)) { printf("skip %s (load)\n", path); return 0; }

    spng_ctx *ref = spng_ctx_new(0);
    spng_set_png_buffer(ref, png, png_len);
    struct spng_ihdr ihdr;
    spng_get_ihdr(ref, &ihdr);

    int failures = 0;
    int fi;
    for(fi = 0; fi < (int)(sizeof(fmts)/sizeof(fmts[0])); fi++)
    {
        int fmt = fmts[fi];

        /* Sub-byte PNG/RAW output stays byte-packed and can't be cropped on
           a pixel boundary; the library must reject it with SPNG_EFMT. */
        int expect_efmt = (fmt & (SPNG_FMT_PNG | SPNG_FMT_RAW)) &&
                          ihdr.bit_depth < 8;

        /* full reference decode */
        spng_ctx *c1 = spng_ctx_new(0);
        spng_set_png_buffer(c1, png, png_len);
        size_t full_size = 0;
        if(spng_decoded_image_size(c1, fmt, &full_size)) { spng_ctx_free(c1); continue; }
        unsigned char *full = malloc(full_size);
        int rc = spng_decode_image(c1, full, full_size, fmt, 0);
        if(rc) { /* intentionally corrupt file, nothing to compare against */
                 free(full); spng_ctx_free(c1); continue; }

        size_t full_row = full_size / ihdr.height;

        uint32_t half_w = ihdr.width > 1 ? ihdr.width / 2 : 0;
        uint32_t half_h = ihdr.height > 1 ? ihdr.height / 2 : 0;

        /* a set of regions: interior, edge-touching, oversize (clipped), 1px */
        struct spng_region regions[] = {
            {0, 0, ihdr.width, ihdr.height},                 /* whole image */
            {half_w, half_h, 1, 1},                          /* single pixel */
            {0, 0, 2, 2},                                     /* top-left */
            {ihdr.width > 1 ? ihdr.width - 2 : 0,
             ihdr.height > 1 ? ihdr.height - 2 : 0, 7, 7},    /* clip far edge */
            {ihdr.width - 1, ihdr.height - 1, 1, 1},          /* bottom-right */
            {ihdr.width > 2 ? ihdr.width / 3 : 0,
             ihdr.height > 2 ? ihdr.height / 3 : 0,
             ihdr.width, ihdr.height},                        /* clipped on both */
        };
        int nreg = sizeof(regions)/sizeof(regions[0]);
        /* Drop regions whose origin falls outside tiny images. */
        int ri2;
        for(ri2 = 0; ri2 < nreg; )
        {
            if(regions[ri2].x >= ihdr.width || regions[ri2].y >= ihdr.height)
            {
                regions[ri2] = regions[nreg - 1];
                nreg--;
            }
            else ri2++;
        }
        int ri;
        for(ri = 0; ri < nreg; ri++)
        {
            struct spng_region r = regions[ri];
            spng_ctx *c2 = spng_ctx_new(0);
            spng_set_png_buffer(c2, png, png_len);

            size_t rsize = 0;
            rc = spng_decoded_region_size(c2, fmt, &r, &rsize);
            if(rc) {
                if(expect_efmt && rc == SPNG_EFMT) { spng_ctx_free(c2); continue; }
                printf("  %s fmt=%d region_size failed: %s\n", path, fmt, spng_strerror(rc));
                     spng_ctx_free(c2); failures++; continue; }

            unsigned char *rbuf = malloc(rsize ? rsize : 1);
            rc = spng_decode_image_region(c2, rbuf, rsize, fmt, 0, &r);
            if(rc) { printf("  %s fmt=%d region decode failed: %s\n", path, fmt, spng_strerror(rc));
                     free(rbuf); spng_ctx_free(c2); failures++; continue; }

            /* compute clipped geometry independently */
            uint32_t cx = r.x, cy = r.y;
            uint32_t cw = r.x + r.width > ihdr.width ? ihdr.width - r.x : r.width;
            uint32_t ch = r.y + r.height > ihdr.height ? ihdr.height - r.y : r.height;
            size_t rrow = rsize / ch;
            size_t ps = pixel_size_of(fmt, &ihdr);

            if(ps == 0 || rrow != (size_t)cw * ps || rsize != rrow * ch)
            { printf("  %s fmt=%d size mismatch rsize=%zu rrow=%zu ch=%u ps=%zu\n",
                     path, fmt, rsize, rrow, ch, ps); failures++; }

            /* compare against cropped full image byte-for-byte */
            uint32_t y;
            for(y = 0; y < ch; y++)
            {
                const unsigned char *ref_row = full + ((size_t)(cy + y) * full_row)
                                                 + (size_t)cx * ps;
                const unsigned char *got_row = rbuf + (size_t)y * rrow;
                if(memcmp(ref_row, got_row, rrow) != 0)
                {
                    printf("  MISMATCH %s fmt=%d region#%d row=%u (pass=%u)\n",
                           path, fmt, ri, y, ihdr.interlace_method);
                    failures++;
                    break;
                }
            }
            free(rbuf);
            spng_ctx_free(c2);
        }
        free(full);
        spng_ctx_free(c1);
    }

    /* invalid region handling */
    struct spng_region bad[] = {
        {0, 0, 0, 1}, {0, 0, 1, 0},
        {ihdr.width, 0, 1, 1}, {0, ihdr.height, 1, 1},
        {ihdr.width, ihdr.height, 1, 1},
    };
    int bi;
    for(bi = 0; bi < (int)(sizeof(bad)/sizeof(bad[0])); bi++)
    {
        spng_ctx *c = spng_ctx_new(0);
        spng_set_png_buffer(c, png, png_len);
        size_t dummy = 0;
        int e1 = spng_decoded_region_size(c, SPNG_FMT_RGBA8, &bad[bi], &dummy);
        spng_ctx_free(c);
        c = spng_ctx_new(0);
        spng_set_png_buffer(c, png, png_len);
        unsigned char tmp[64];
        int e2 = spng_decode_image_region(c, tmp, sizeof(tmp), SPNG_FMT_RGBA8, 0, &bad[bi]);
        spng_ctx_free(c);
        if(e1 == 0 || e2 == 0)
        { printf("  %s: invalid region accepted e1=%d e2=%d\n", path, e1, e2); failures++; }
    }

    spng_ctx_free(ref);
    free(png);
    return failures;
}

int main(int argc, char **argv)
{
    int total = 0;
    int i;
    for(i = 1; i < argc; i++)
    {
        int f = run_one(argv[i]);
        if(f) { printf("FAIL %s (%d)\n", argv[i], f); total += f; }
        else printf("ok   %s\n", argv[i]);
    }
    printf(total ? "\nREGION TESTS FAILED: %d\n" : "\nall region tests passed\n", total);
    return total ? 1 : 0;
}
