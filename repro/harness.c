/* Decode every PNG given on argv with spng, dump RGBA8 to .spng.bin,
   print return code and palette/tRNS metadata. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spng.h"

int main(int argc, char **argv)
{
    for(int a=1; a<argc; a++)
    {
        const char *path = argv[a];
        FILE *f = fopen(path, "rb");
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *buf = malloc(sz);
        fread(buf, 1, sz, f); fclose(f);

        spng_ctx *ctx = spng_ctx_new(0);
        spng_set_png_buffer(ctx, buf, sz);
        struct spng_ihdr ihdr;
        int rc = spng_decode_chunks(ctx);
        if(rc) { printf("%s: chunks rc=%d (%s)\n", path, rc, spng_strerror(rc)); goto done; }
        spng_get_ihdr(ctx, &ihdr);
        struct spng_plte plte; int prc = spng_get_plte(ctx, &plte);
        struct spng_trns trns; int trc = spng_get_trns(ctx, &trns);
        size_t need = 0;
        spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &need);
        unsigned char *out = calloc(1, need + 64);
        rc = spng_decode_image(ctx, out, need, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS);
        printf("%s: %ux%u depth=%u n_entries=%u trns=%u n_alpha=%u decode_rc=%d (%s)\n",
               path, ihdr.width, ihdr.height, ihdr.bit_depth,
               prc==0?plte.n_entries:0, trc==0, trc==0?trns.n_type3_entries:0,
               rc, spng_strerror(rc));
        if(rc == 0)
        {
            char outp[512]; snprintf(outp, sizeof outp, "%s.spng.bin", path);
            FILE *o = fopen(outp, "wb"); fwrite(out, 1, need, o); fclose(o);
        }
        free(out);
done:
        spng_ctx_free(ctx); free(buf);
    }
    return 0;
}
