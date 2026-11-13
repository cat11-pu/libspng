/* Progressive/raw-format + ASAN-friendly OOB probe */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spng.h"

static void load(const char *path, unsigned char **buf, long *sz){
    FILE *f=fopen(path,"rb"); fseek(f,0,SEEK_END); *sz=ftell(f); fseek(f,0,SEEK_SET);
    *buf=malloc(*sz); if(fread(*buf,1,*sz,f)!=(size_t)*sz){} fclose(f);
}
static int try_decode(const char *path, int fmt, int flags, int progressive){
    unsigned char *buf; long sz; load(path,&buf,&sz);
    spng_ctx *ctx=spng_ctx_new(0);
    spng_set_png_buffer(ctx,buf,sz);
    int rc=spng_decode_chunks(ctx);
    if(rc){ printf("  %s fmt=%d prog=%d: chunks rc=%d\n",path,fmt,progressive,rc); spng_ctx_free(ctx); free(buf); return rc; }
    size_t need=0; spng_decoded_image_size(ctx,fmt,&need);
    unsigned char *out=calloc(1,need+64);
    if(!progressive){
        rc=spng_decode_image(ctx,out,need,fmt,flags);
        printf("  %s fmt=%d flags=%d: rc=%d (%s)\n",path,fmt,flags,rc,spng_strerror(rc));
    } else {
        rc=spng_decode_image(ctx,NULL,0,fmt,flags|SPNG_DECODE_PROGRESSIVE);
        int r2=0;
        struct spng_row_info ri;
        while(!(r2=spng_get_row_info(ctx,&ri))){
            r2=spng_decode_row(ctx,out,need);
            if(r2) break;
        }
        printf("  %s fmt=%d PROG: init_rc=%d loop_rc=%d (%s)\n",path,fmt,rc,r2,spng_strerror(r2));
    }
    free(out); spng_ctx_free(ctx); free(buf);
    return rc;
}
int main(int argc,char**argv){
    for(int a=1;a<argc;a++){
        printf("== %s ==\n",argv[a]);
        try_decode(argv[a], SPNG_FMT_RGBA8, SPNG_DECODE_TRNS, 0);
        try_decode(argv[a], SPNG_FMT_RGBA8, 0, 0);
        try_decode(argv[a], SPNG_FMT_RGB8, 0, 0);
        try_decode(argv[a], SPNG_FMT_RAW, 0, 0);
        try_decode(argv[a], SPNG_FMT_PNG, 0, 0);
        try_decode(argv[a], SPNG_FMT_RGBA8, SPNG_DECODE_TRNS, 1);
    }
    return 0;
}
