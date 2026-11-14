/* Generate a large non-interlaced and an Adam7 interlaced RGBA8 PNG using
   spng's encoder, for memory/region benchmarking. */
#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void write_png(const char*path,uint32_t w,uint32_t h,int interlace){
    size_t img_size=(size_t)w*h*4;
    unsigned char *img=malloc(img_size);
    for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){
        unsigned char*p=img+((size_t)y*w+x)*4;
        p[0]=(x*7+y*3)&255; p[1]=(x*5+y*11)&255; p[2]=(x*13+y*17)&255; p[3]=255;
    }
    spng_ctx *c=spng_ctx_new(SPNG_CTX_ENCODER);
    spng_set_option(c,SPNG_IMG_COMPRESSION_LEVEL,6);
    spng_set_option(c,SPNG_ENCODE_TO_BUFFER,1);
    struct spng_ihdr ih={0};
    ih.width=w; ih.height=h; ih.bit_depth=8; ih.color_type=SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ih.compression_method=0; ih.filter_method=0; ih.interlace_method=interlace;
    spng_set_ihdr(c,&ih);
    size_t out_len; int e;
    void *out=NULL;
    e=spng_encode_image(c,img,img_size,SPNG_FMT_PNG,SPNG_ENCODE_FINALIZE);
    if(e){ fprintf(stderr,"encode failed: %s\n",spng_strerror(e)); exit(1);}
    out=spng_get_png_buffer(c,&out_len,&e);
    FILE*f=fopen(path,"wb"); fwrite(out,1,out_len,f); fclose(f);
    spng_ctx_free(c); free(img);
    printf("wrote %s (%ux%u %s) %zu B\n",path,w,h,interlace?"Adam7":"non-int",out_len);
}
int main(void){
    write_png("tests/region/big_n.png",1024,1024,0);
    write_png("tests/region/big_i.png",1024,1024,1);
    return 0;
}
