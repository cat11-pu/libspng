/* Boundary/edge-case tests for region decode. */
#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int failures;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); failures++; } \
                              else printf("ok: %s\n", msg); } while(0)

static unsigned char *slurp(const char*p,size_t*n){
    FILE*f=fopen(p,"rb"); fseek(f,0,SEEK_END); long z=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char*b=malloc(z);
    if(fread(b,1,z,f) != (size_t)z){ fclose(f); free(b); return NULL; }
    fclose(f); *n=(size_t)z; return b;
}

int main(void){
    size_t pn; unsigned char *png = slurp("tests/region/big_n.png",&pn);
    spng_ctx *t=spng_ctx_new(0); spng_set_png_buffer(t,png,pn);
    struct spng_ihdr ih; spng_get_ihdr(t,&ih); spng_ctx_free(t);
    const int W=ih.width, H=ih.height;

    /* size helper: clipping on the far edge shrinks width/height */
    struct spng_region over = { W-10, H-10, 9999, 9999 };
    spng_ctx *c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    size_t sz=0;
    int rc=spng_decoded_region_size(c,SPNG_FMT_RGBA8,&over,&sz);
    CHECK(rc==0, "oversized region accepted and clipped");
    CHECK(sz==(size_t)10*10*4, "clipped region size is 10x10 RGBA8");
    spng_ctx_free(c);

    /* exact whole-image region equals full decoded size */
    size_t fullsz; c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    spng_decoded_image_size(c,SPNG_FMT_RGBA8,&fullsz);
    struct spng_region whole={0,0,W,H}; size_t wsz;
    spng_decoded_region_size(c,SPNG_FMT_RGBA8,&whole,&wsz);
    CHECK(wsz==fullsz, "whole-image region size == full image size");
    spng_ctx_free(c);

    /* zero width / zero height rejected */
    struct spng_region zw={0,0,0,1}, zh={0,0,1,0};
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn); size_t d;
    CHECK(spng_decoded_region_size(c,SPNG_FMT_RGBA8,&zw,&d)==SPNG_EINVAL,"zero width -> EINVAL");
    spng_ctx_free(c);
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    CHECK(spng_decoded_region_size(c,SPNG_FMT_RGBA8,&zh,&d)==SPNG_EINVAL,"zero height -> EINVAL");
    spng_ctx_free(c);

    /* origin outside image rejected (not silently clamped to empty) */
    struct spng_region ox={W,0,1,1}, oy={0,H,1,1};
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    CHECK(spng_decoded_region_size(c,SPNG_FMT_RGBA8,&ox,&d)==SPNG_EINVAL,"x==width -> EINVAL");
    spng_ctx_free(c);
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    CHECK(spng_decoded_region_size(c,SPNG_FMT_RGBA8,&oy,&d)==SPNG_EINVAL,"y==height -> EINVAL");
    spng_ctx_free(c);

    /* NULL region pointer rejected by the region API */
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    CHECK(spng_decoded_region_size(c,SPNG_FMT_RGBA8,NULL,&d)==SPNG_EINVAL,"NULL region -> EINVAL");
    spng_ctx_free(c);

    /* PROGRESSIVE flag not allowed for region decode */
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    unsigned char tmp[64];
    CHECK(spng_decode_image_region(c,tmp,sizeof(tmp),SPNG_FMT_RGBA8,
          SPNG_DECODE_PROGRESSIVE,&whole)==SPNG_EFLAGS,"PROGRESSIVE -> EFLAGS");
    spng_ctx_free(c);

    /* undersized output buffer rejected */
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    struct spng_region small={0,0,4,4}; size_t need;
    spng_decoded_region_size(c,SPNG_FMT_RGBA8,&small,&need);
    int brc=spng_decode_image_region(c,tmp,need-1,SPNG_FMT_RGBA8,0,&small);
    CHECK(brc==SPNG_EBUFSIZ, "short buffer -> EBUFSIZ");
    spng_ctx_free(c);

    /* sub-byte PNG/RAW rejected, but unpacked G8/RGBA8 allowed */
    size_t p1n; unsigned char *p1=slurp("tests/images/basn0g01.png",&p1n);
    c=spng_ctx_new(0); spng_set_png_buffer(c,p1,p1n);
    struct spng_ihdr h1; spng_get_ihdr(c,&h1);
    struct spng_region r1={0,0,h1.width,h1.height};
    CHECK(spng_decoded_region_size(c,SPNG_FMT_PNG,&r1,&d)==SPNG_EFMT,"sub-byte PNG -> EFMT");
    spng_ctx_free(c);
    c=spng_ctx_new(0); spng_set_png_buffer(c,p1,p1n);
    CHECK(spng_decoded_region_size(c,SPNG_FMT_G8,&r1,&d)==0,"sub-byte via G8 unpack allowed");
    spng_ctx_free(c);
    free(p1);

    /* region decode can only run once per context (state advances to EOI) */
    c=spng_ctx_new(0); spng_set_png_buffer(c,png,pn);
    size_t ns; spng_decoded_region_size(c,SPNG_FMT_RGBA8,&whole,&ns);
    unsigned char *buf=malloc(ns);
    int first=spng_decode_image_region(c,buf,ns,SPNG_FMT_RGBA8,0,&whole);
    int second=spng_decode_image_region(c,buf,ns,SPNG_FMT_RGBA8,0,&whole);
    CHECK(first==0 && second==SPNG_EOI, "second region decode -> EOI");
    spng_ctx_free(c); free(buf);

    free(png);
    printf(failures ? "\nEDGE TESTS FAILED: %d\n" : "\nall edge tests passed\n", failures);
    return failures?1:0;
}
