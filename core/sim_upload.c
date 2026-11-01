/* Host test: pack build, SysEx upload over a lossy/duplicating link into a simulated flash, power-loss sweep. */
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "lofi_pack.h"
#include "lofi_sender.h"
#define FLASH_BYTES 536576u
typedef struct { uint8_t mem[FLASH_BYTES]; long ops, fail_at; int dead; } flash_t;
static int f_read(void*c,uint32_t o,void*p,size_t n){ flash_t*f=c; if(o+n>FLASH_BYTES) return -1; memcpy(p,f->mem+o,n); return 0; }
static int f_erase(void*c,uint32_t o){ flash_t*f=c; if(f->dead||(f->ops++,f->fail_at>=0&&f->ops>f->fail_at)){ f->dead=1; memset(f->mem+o,0xa5,LF_SECTOR/2); return -1; } assert(o%LF_SECTOR==0); memset(f->mem+o,0xff,LF_SECTOR); return 0; }
static int f_write(void*c,uint32_t o,const void*p,size_t n){ flash_t*f=c; if(f->dead||(f->ops++,f->fail_at>=0&&f->ops>f->fail_at)){ f->dead=1; memcpy(f->mem+o,p,n/2); return -1; }
    const uint8_t*s=p; for(size_t i=0;i<n;i++){ assert((f->mem[o+i]&s[i])==s[i]); /* only 1->0 on erased */ f->mem[o+i]&=s[i]; } return 0; }
typedef struct { lf_dev_t *dev; unsigned loss, dup; unsigned rng; long frames; } link_t;
static unsigned rnd(link_t*l){ l->rng=l->rng*1664525u+1013904223u; return l->rng>>16; }
static int link_fn(void*c,const uint8_t*m,size_t n,uint8_t*r,size_t*rn){ link_t*l=c; *rn=0; l->frames++;
    if(rnd(l)%100 < l->loss) return -1;                       /* request lost */
    size_t k=lf_dev_handle(l->dev,m,n,r); if(rnd(l)%100 < l->dup) k=lf_dev_handle(l->dev,m,n,r); /* duplicate delivery */
    if(rnd(l)%100 < l->loss) return -1;                       /* reply lost */
    *rn=k; return 0; }
static int16_t *load(const char*p,unsigned*n){ FILE*f=fopen(p,"rb"); if(!f){perror(p);exit(2);} fseek(f,0,SEEK_END); *n=ftell(f)/2; fseek(f,0,SEEK_SET); int16_t*x=malloc(*n*2+2); fread(x,2,*n,f); fclose(f); return x; }
static flash_t F;
static void setup(lf_dev_t*d){ memset(F.mem,0xff,sizeof F.mem); F.ops=0; F.fail_at=-1; F.dead=0;
    d->fl=(lf_flash_t){&F,FLASH_BYTES,f_read,f_write,f_erase}; lf_dev_init(d); }
int main(void){
    static lf_dev_t dev; const char *names[]={"Tape_Key","Shiny","Phonk"}; lfp_input_t in[16]; unsigned n=0;
    char path[128]; static char nm[9][12];
    for(n=0;n<3;n++){ snprintf(path,sizeof path,"../out/%s.raw",names[n]); unsigned fr; int16_t*x=load(path,&fr);
        in[n]=(lfp_input_t){names[n],x,fr,0,LFP_NO_LOOP,(uint8_t)(48+n*12),(uint8_t)(59+n*12),(uint8_t)(48+n*12+6),0}; }
    for(unsigned h=1;h<=9;h++,n++){ snprintf(nm[h-1],12,"hit%02u",h); snprintf(path,sizeof path,"../out/kit/hit%02u.raw",h);
        unsigned fr; int16_t*x=load(path,&fr); in[n]=(lfp_input_t){nm[h-1],x,fr,0,LFP_NO_LOOP,(uint8_t)(35+h),(uint8_t)(35+h),(uint8_t)(35+h),4}; }
    uint32_t bytes; uint8_t *img=lfp_build("test pack",in,n,&bytes); assert(img);
    lfp_info_t info; assert(lfp_parse(img,bytes,&info)==0);
    printf("pack: %u slots, %u frames (%.1f s), image %u bytes (%.1f%% of %u data sectors)\n",info.n_slots,info.total_frames,info.total_frames/16000.0,bytes,100.0*bytes/((FLASH_BYTES/LF_SECTOR-2)*LF_SECTOR),FLASH_BYTES/LF_SECTOR-2);
    /* 1. lossy link: several loss rates */
    for(unsigned loss=0; loss<=30; loss+=10){ setup(&dev); link_t l={&dev,loss,loss/2,12345+loss,0}; lf_sender_t s={link_fn,&l,0,0,0,0};
        int rc=lf_send_pack(&s,img,bytes); assert(rc==0); assert(dev.state==LF_PACK);
        uint8_t *back=malloc(bytes); f_read(&F,2*LF_SECTOR,back,bytes); assert(!memcmp(back,img,bytes)); free(back);
        lf_dev_t d2; d2.fl=dev.fl; lf_dev_init(&d2); assert(d2.state==LF_PACK && d2.pack_crc==lf_crc32(0,img,bytes));
        printf("loss %2u%% dup %2u%%: ok, %ld frames, blocks sent %u (resent %u), request retries %u\n",loss,loss/2,l.frames,s.sent_blocks,s.resent_blocks,s.retries); }
    /* 2. second upload replaces the first (different pack) and a resume after link failure */
    { setup(&dev); link_t l={&dev,0,0,1,0}; lf_sender_t s={link_fn,&l,0,0,0,0}; assert(lf_send_pack(&s,img,bytes)==0);
      uint32_t b2=bytes/2; /* a truncated image is a different pack: must replace */
      assert(lf_send_pack(&s,img,b2)==0); assert(dev.state==LF_PACK && dev.pack_size==b2); puts("replace pack: ok"); }
    /* 3. power loss sweep: an old pack is on the device, the flash dies at every single operation of the next upload;
       afterwards the bank must be EMPTY, or a complete valid pack (old or new), never anything else */
    uint32_t small, oldb; uint8_t *simg=lfp_build("small",&in[3],4,&small), *oimg=lfp_build("old",&in[7],3,&oldb); assert(simg&&oimg);
    long total_ops; { setup(&dev); link_t l={&dev,0,0,1,0}; lf_sender_t s={link_fn,&l,0,0,0,0}; assert(lf_send_pack(&s,oimg,oldb)==0);
      F.ops=0; assert(lf_send_pack(&s,simg,small)==0); total_ops=F.ops; }
    long bad=0, empty=0, kept_old=0, got_new=0;
    for(long k=0;k<=total_ops;k++){ setup(&dev); link_t l={&dev,0,0,1,0}; lf_sender_t s={link_fn,&l,0,0,0,0};
        assert(lf_send_pack(&s,oimg,oldb)==0); F.ops=0; F.fail_at=k; (void)lf_send_pack(&s,simg,small); F.dead=0; F.fail_at=-1;
        lf_dev_t d2; d2.fl=dev.fl; lf_dev_init(&d2);
        if(d2.state==LF_PACK){ uint8_t*bk=malloc(d2.pack_size); f_read(&F,2*LF_SECTOR,bk,d2.pack_size);
            if(d2.pack_size==small && !memcmp(bk,simg,small) && d2.pack_crc==lf_crc32(0,simg,small)) got_new++;
            else if(d2.pack_size==oldb && !memcmp(bk,oimg,oldb) && d2.pack_crc==lf_crc32(0,oimg,oldb)) kept_old++; else bad++; free(bk); }
        else if(d2.state==LF_EMPTY) empty++; else bad++; }
    printf("power-loss sweep: %ld flash ops per upload, failure injected at each: empty %ld, old pack intact %ld, new pack complete %ld, corrupt %ld\n",total_ops,empty,kept_old,got_new,bad); assert(!bad);
    puts("ALL OK"); return 0; }
