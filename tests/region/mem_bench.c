/* Measure library-internal peak heap (via a counting allocator) and the
   caller output buffer size, comparing full decode vs region decode.
   The region width is kept equal to the image width so the only variable
   is region height, matching the "memory scales with region height" claim. */
#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static size_t cur_bytes, peak_bytes, n_alloc, n_free;
static void *m_alloc(size_t n){ cur_bytes+=n; if(cur_bytes>peak_bytes)peak_bytes=cur_bytes; n_alloc++; return malloc(n);}
static void *m_calloc(size_t c,size_t n){ size_t z=c*n; cur_bytes+=z; if(cur_bytes>peak_bytes)peak_bytes=cur_bytes; n_alloc++; return calloc(c,n);}
static void *m_realloc(void*p,size_t n){ /* not exercised by decode path here */ return realloc(p,n); }
static void m_free(void*p){ (void)p; n_free++; free(p); }
static struct spng_alloc allocator = { m_alloc, m_realloc, m_calloc, m_free };

static unsigned char *slurp(const char*p,size_t*n){
    FILE*f=fopen(p,"rb"); fseek(f,0,SEEK_END); long z=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char*b=malloc(z);
    if(fread(b,1,z,f) != (size_t)z){ fclose(f); free(b); return NULL; }
    fclose(f); *n=(size_t)z; return b;
}
static void reset(void){ cur_bytes=peak_bytes=n_alloc=n_free=0; }

static void measure(const char*path,int fmt){
    size_t pn; unsigned char*png=slurp(path,&pn);
    spng_ctx *t=spng_ctx_new(0); spng_set_png_buffer(t,png,pn);
    struct spng_ihdr ih; spng_get_ihdr(t,&ih); spng_ctx_free(t);

    /* full decode */
    reset();
    spng_ctx *c=spng_ctx_new2(&allocator,0);
    spng_set_png_buffer(c,png,pn);
    size_t full_size; spng_decoded_image_size(c,fmt,&full_size);
    void *out=m_alloc(full_size);
    spng_decode_image(c,out,full_size,fmt,0);
    size_t full_internal_peak=peak_bytes;
    spng_ctx_free(c); m_free(out);

    printf("%-24s %ux%u depth=%u %s\n", path, ih.width, ih.height, ih.bit_depth,
           ih.interlace_method?"Adam7":"non-int");
    printf("  full   : output=%8zu B  internal_peak=%8zu B  total=%8zu B\n",
           full_size, full_internal_peak, full_size+full_internal_peak);

    /* region decodes at several heights, full width */
    uint32_t heights[] = { ih.height, ih.height/2, ih.height/4, ih.height/8, 1 };
    int nh = sizeof(heights)/sizeof(heights[0]);
    int i;
    for(i=0;i<nh;i++){
        uint32_t h=heights[i]; if(!h) h=1;
        struct spng_region r={0,0,ih.width,h};
        reset();
        spng_ctx *rc=spng_ctx_new2(&allocator,0);
        spng_set_png_buffer(rc,png,pn);
        size_t rsize;
        int e=spng_decoded_region_size(rc,fmt,&r,&rsize);
        if(e){ printf("  region h=%u unsupported(%s)\n",h,spng_strerror(e)); spng_ctx_free(rc); continue;}
        void *rb=m_alloc(rsize);
        int de=spng_decode_image_region(rc,rb,rsize,fmt,0,&r);
        size_t rpeak=peak_bytes;
        spng_ctx_free(rc); m_free(rb);
        double out_ratio = (double)rsize/full_size;
        printf("  region h=%2u: output=%8zu B (%5.1f%%) internal_peak=%8zu B  total=%8zu B  decode_rc=%d\n",
               h, rsize, 100.0*out_ratio, rpeak, rsize+rpeak, de);
    }
    free(png);
}

int main(void){
    measure("tests/region/big_n.png", SPNG_FMT_RGBA8);
    measure("tests/region/big_i.png", SPNG_FMT_RGBA8);
    measure("tests/images/basn6a08.png", SPNG_FMT_RGBA8);
    measure("tests/images/basi6a08.png", SPNG_FMT_RGBA8);
    measure("tests/images/basn2c16.png", SPNG_FMT_RGBA16);
    measure("tests/images/basi2c16.png", SPNG_FMT_RGBA16);
    measure("tests/images/basn3p08.png", SPNG_FMT_RGBA8);
    return 0;
}
