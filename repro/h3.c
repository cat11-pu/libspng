#include <stdio.h>
#include <stdlib.h>
#include "spng.h"
static unsigned char* loadf(const char*p,long*sz){FILE*f=fopen(p,"rb");fseek(f,0,2);*sz=ftell(f);fseek(f,0,0);unsigned char*b=malloc(*sz);if(fread(b,1,*sz,f)!=(size_t)*sz){}fclose(f);return b;}
static void one(const char*p,int fmt){
    long sz; unsigned char*buf=loadf(p,&sz);
    spng_ctx*ctx=spng_ctx_new(0); spng_set_png_buffer(ctx,buf,sz);
    spng_decode_chunks(ctx);
    size_t need=0; spng_decoded_image_size(ctx,fmt,&need);
    unsigned char*o=calloc(1,need);
    int rc=spng_decode_image(ctx,o,need,fmt,SPNG_DECODE_TRNS);
    printf("%s fmt=%d rc=%d bytes=",p,fmt,rc);
    for(size_t i=0;i<need;i++)printf("%02x",o[i]);
    printf("\n");
    free(o); spng_ctx_free(ctx); free(buf);
}
int main(int c,char**v){for(int a=1;a<c;a++){one(v[a],SPNG_FMT_RGBA8);one(v[a],SPNG_FMT_RGB8);one(v[a],SPNG_FMT_RGBA16);}return 0;}
