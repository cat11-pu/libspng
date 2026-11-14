#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Horizontal-only crop and small x-offset on interlaced images. */
static int one(const char*path,int fmt){
    FILE*f=fopen(path,"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char*png=malloc(n);
    if(fread(png,1,n,f)!=(size_t)n){ fclose(f); return 2; }
    fclose(f);
    spng_ctx*c=spng_ctx_new(0); spng_set_png_buffer(c,png,n);
    struct spng_ihdr ih; spng_get_ihdr(c,&ih);
    size_t fs; spng_decoded_image_size(c,fmt,&fs);
    unsigned char*full=malloc(fs);
    if(spng_decode_image(c,full,fs,fmt,0)){spng_ctx_free(c);return 3;}
    spng_ctx_free(c);
    size_t row=fs/ih.height; size_t ps = fmt==SPNG_FMT_RGB8?3:fmt==SPNG_FMT_RGBA16?8:4;
    struct spng_region regs[]={
        {1,0,ih.width-1,ih.height},          /* drop first column */
        {ih.width/3, 0, ih.width/3, ih.height}, /* middle vertical strip */
        {ih.width-1, 0, 1, ih.height},       /* last column */
        {0,0,1,ih.height},                   /* first column */
        {2,3,ih.width/2, ih.height/2},       /* mixed box */
    };
    int bad=0;
    for(int r=0;r<5;r++){
        struct spng_region g=regs[r];
        c=spng_ctx_new(0); spng_set_png_buffer(c,png,n);
        size_t sz; int e=spng_decoded_region_size(c,fmt,&g,&sz);
        if(e){printf("%s r%d size err %s\n",path,r,spng_strerror(e));bad++;spng_ctx_free(c);continue;}
        unsigned char*b=malloc(sz);
        e=spng_decode_image_region(c,b,sz,fmt,0,&g);
        size_t rrow=sz/g.height;
        for(uint32_t y=0;y<g.height && !bad;y++)
          for(uint32_t x=0;x<g.width;x++){
            unsigned char*a=full+((g.y+y)*row)+(g.x+x)*ps;
            unsigned char*q=b+y*rrow+x*ps;
            if(memcmp(a,q,ps)){printf("%s r%d mismatch x=%u y=%u\n",path,r,x,y);bad=1;break;}
          }
        spng_ctx_free(c); free(b);
    }
    free(png);free(full);
    printf("%-24s fmt=%d %s\n",path,fmt,bad?"FAIL":"ok");
    return bad?1:0;
}
int main(void){
    int f=0;
    f|=one("tests/region/big_i.png",SPNG_FMT_RGBA8);
    f|=one("tests/images/basi2c08.png",SPNG_FMT_RGB8);
    f|=one("tests/images/basi6a16.png",SPNG_FMT_RGBA16);
    f|=one("tests/region/big_n.png",SPNG_FMT_RGBA8);
    return f;
}
