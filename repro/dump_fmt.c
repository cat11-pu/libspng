#include <stdio.h>
#include <stdlib.h>
#include "spng.h"
static unsigned char* loadf(const char*p,long*sz){FILE*f=fopen(p,"rb");fseek(f,0,2);*sz=ftell(f);fseek(f,0,0);unsigned char*b=malloc(*sz);if(fread(b,1,*sz,f)!=(size_t)*sz){}fclose(f);return b;}
int main(int c,char**v){
    int fmt=atoi(v[1]);
    for(int a=2;a<c;a++){
        long sz;unsigned char*buf=loadf(v[a],&sz);
        spng_ctx*ctx=spng_ctx_new(0);spng_set_png_buffer(ctx,buf,sz);
        if(spng_decode_chunks(ctx)){printf("chunks fail\n");return 2;}
        size_t need=0;spng_decoded_image_size(ctx,fmt,&need);
        unsigned char*o=calloc(1,need);
        int rc=spng_decode_image(ctx,o,need,fmt,SPNG_DECODE_TRNS);
        if(rc){printf("%s rc=%d\n",v[a],rc);continue;}
        char out[512];snprintf(out,sizeof out,"%s.f%d.bin",v[a],fmt);
        FILE*g=fopen(out,"wb");fwrite(o,1,need,g);fclose(g);
        spng_ctx_free(ctx);free(buf);free(o);
    }
    return 0;
}
