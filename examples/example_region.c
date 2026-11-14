/* Decode a rectangular region of a PNG without allocating the full image.
   The output is identical to a full decode followed by cropping. */
#include <spng.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if(argc != 2)
    {
        printf("usage: %s file.png\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if(file == NULL)
    {
        printf("error opening %s\n", argv[1]);
        return 1;
    }

    spng_ctx *ctx = spng_ctx_new(0);
    if(ctx == NULL) return 1;

    int ret = spng_set_png_file(ctx, file);
    if(ret)
    {
        printf("spng_set_png_file() error: %s\n", spng_strerror(ret));
        goto err;
    }

    struct spng_ihdr ihdr;
    ret = spng_get_ihdr(ctx, &ihdr);
    if(ret)
    {
        printf("spng_get_ihdr() error: %s\n", spng_strerror(ret));
        goto err;
    }

    /* Decode the top-left quarter of the image. A region extending past the
       image edge is clipped automatically. */
    struct spng_region region =
    {
        .x = 0,
        .y = 0,
        .width = ihdr.width / 2 ? ihdr.width / 2 : 1,
        .height = ihdr.height / 2 ? ihdr.height / 2 : 1
    };

    size_t region_size;
    ret = spng_decoded_region_size(ctx, SPNG_FMT_RGBA8, &region, &region_size);
    if(ret)
    {
        printf("spng_decoded_region_size() error: %s\n", spng_strerror(ret));
        goto err;
    }

    unsigned char *out = malloc(region_size);
    if(out == NULL) goto err;

    ret = spng_decode_image_region(ctx, out, region_size,
                                   SPNG_FMT_RGBA8, 0, &region);
    if(ret)
    {
        printf("spng_decode_image_region() error: %s\n", spng_strerror(ret));
        free(out);
        goto err;
    }

    /* region.width/height were clipped to the image if they overflowed. */
    printf("decoded %ux%u pixels (%zu bytes)\n",
           region.width, region.height, region_size);

    free(out);
    spng_ctx_free(ctx);
    fclose(file);

    return 0;

err:
    spng_ctx_free(ctx);
    fclose(file);
    return 1;
}
