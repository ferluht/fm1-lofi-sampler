/* host test of the engine: render a pack through le_render like the audio interrupt would. test_engine pack.lfp out.wav */
#include <stdio.h>
#include <math.h>
#include "lofi_engine.h"
static void wav(const char*p,const int16_t*x,unsigned n){ FILE*f=fopen(p,"wb"); uint32_t sr=44100,br=sr*4,ds=n*4,cs=36+ds; uint16_t t=1,ch=2,ba=4,bps=16; uint32_t fmt=16;
  fwrite("RIFF",1,4,f);fwrite(&cs,4,1,f);fwrite("WAVEfmt ",1,8,f);fwrite(&fmt,4,1,f);fwrite(&t,2,1,f);fwrite(&ch,2,1,f);fwrite(&sr,4,1,f);fwrite(&br,4,1,f);fwrite(&ba,2,1,f);fwrite(&bps,2,1,f);fwrite("data",1,4,f);fwrite(&ds,4,1,f);fwrite(x,4,n,f);fclose(f); }
int main(int c,char**v){ FILE*f=fopen(v[1],"rb"); fseek(f,0,SEEK_END); long bytes=ftell(f); fseek(f,0,SEEK_SET); uint8_t*img=malloc(bytes); if(fread(img,1,bytes,f)!=bytes) return 2; fclose(f);
  le_init(); if(le_bind(img,bytes)){ puts("bind failed"); return 1; } const lfp_info_t*in=le_info(); printf("pack '%s' %u slots\n",in->name,in->n_slots);
  unsigned total=44100*40, blocks=total/128; int16_t*pcm=malloc(total*4); int32_t blk[256]; unsigned o=0; int fail=0;
  /* script: each of the first slots at original speed, then Tape Key with live speed sweep 0 .. -4 oct .. +1 oct */
  for(unsigned b=0;b<blocks;b++){
    unsigned t=b*128; /* sample time */
#define AT(sec) (t<=(unsigned)((sec)*44100) && (unsigned)((sec)*44100)<t+128)
    if(AT(0)) le_note_on(in->slot[0].root,110);                   /* slot 0 at root */
    if(AT(8)) le_note_off(in->slot[0].root);
    if(AT(9)) le_note_on(in->slot[0].root+12,110);          /* octave up by pitch */
    if(AT(12)) le_note_off(in->slot[0].root+12);
    if(AT(13)) { le_note_on(in->slot[0].root,110); }
    if(t>=44100*13 && t<44100*23) le_set_speed(-(int)((t-44100*13)*192/(44100*10)));    /* sweep down 4 octaves in 10 s while playing */
    if(t>=44100*23 && t<44100*26) le_set_speed(-192+(int)((t-44100*23)*(192+48)/(44100*3)));  /* and up to +1 oct */
    if(AT(26)) { le_note_off(in->slot[0].root); le_set_speed(0); }
    if(AT(27)) for(unsigned s=3;s<in->n_slots;s++) le_slot_on(s,in->slot[s].root,100);   /* all hits at once: polyphony 8 steal */
    le_render(blk,128);
    for(int i=0;i<256;i++){ int32_t s=blk[i]>>8; if(s>32767)s=32767; if(s<-32768)s=-32768; pcm[o++]=(int16_t)s; }
    if(b==10) printf("voices active after 10 blocks: %u\n",le_voices_active());
  }
  wav(v[2],pcm,total); double pk=0; for(unsigned i=0;i<o;i++) if(fabs(pcm[i])>pk) pk=fabs(pcm[i]); printf("rendered %u frames, peak %.0f\n",o/2,pk); (void)fail; return 0; }
