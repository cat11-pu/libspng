#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int rw_fn(spng_ctx *ctx, void *user, void *dst, size_t len){
    FILE *f=user; (void)ctx;
    return fread(dst,1,len,f)==len ? 0 : 1;
}
int main(void){
    const char*path="tests/images/basn6a08.png";
    /* buffer reference */
    FILE *f=fopen(path,"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char *png=malloc(n); fread(png,1,n,f); fclose(f);
    spng_ctx *c=spng_ctx_new(0); spng_set_png_buffer(c,png,n);
    struct spng_ihdr ih; spng_get_ihdr(c,&ih);
    size_t fs; spng_decoded_image_size(c,SPNG_FMT_RGBA8,&fs);
    unsigned char *full=malloc(fs); spng_decode_image(c,full,fs,SPNG_FMT_RGBA8,0); spng_ctx_free(c);
    size_t row=fs/ih.height;
    struct spng_region r={3,2,ih.width-4,ih.height-3};

    /* stream input region decode */
    f=fopen(path,"rb");
    spng_ctx *s=spng_ctx_new(0);
    int e=spng_set_png_stream(s,rw_fn,f);
    size_t rs; int e2=spng_decoded_region_size(s,SPNG_FMT_RGBA8,&r,&rs);
    unsigned char *b=malloc(rs);
    int e3=spng_decode_image_region(s,b,rs,SPNG_FMT_RGBA8,0,&r);
    spng_ctx_free(s); fclose(f);
    size_t rrow=rs/r.height; int bad=0;
    for(uint32_t y=0;y<r.height;y++)
      if(memcmp(full+(r.y+y)*row+r.x*4, b+y*rrow, rrow)) bad=1;
    printf("stream: set=%d size=%d decode=%d match=%d\n",e,e2,e3,!bad);

    /* file input region decode */
    f=fopen(path,"rb");
    spng_ctx *fc=spng_ctx_new(0); spng_set_png_file(fc,f);
    size_t frs; spng_decoded_region_size(fc,SPNG_FMT_RGBA8,&r,&frs);
    unsigned char *fb=malloc(frs);
    int e4=spng_decode_image_region(fc,fb,frs,SPNG_FMT_RGBA8,0,&r);
    spng_ctx_free(fc); fclose(f);
    printf("file: decode=%d match=%d\n",e4,!memcmp(fb,b,rs));
    int fail = bad||e||e2||e3||e4;
    free(png); free(full); free(b); free(fb);
    return fail?1:0;
}
