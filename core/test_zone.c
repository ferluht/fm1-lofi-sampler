#include <stdio.h>
#include <assert.h>
#include "lofi_engine.h"
static int16_t pcm[64000];
int main(void){ for(int i=0;i<64000;i++) pcm[i]=(int16_t)(6000*((i/40)%2?1:-1));
  lfp_input_t in[5]={ {"mono",pcm,64000,0,LFP_NO_LOOP,60,71,60,0, 1,0,20},      /* mono, glide 200 ms */
                      {"hatc",pcm,16000,0,LFP_NO_LOOP,40,40,40,4, 0,1,0}, {"hato",pcm,64000,0,LFP_NO_LOOP,41,41,41,4, 0,1,0},   /* choke group 1 */
                      {"pad",pcm,64000,0,LFP_NO_LOOP,72,83,72,0, 2,0,0},                                                        /* 2 voices */
                      {"free",pcm,64000,0,LFP_NO_LOOP,90,90,90,4, 0,0,0} };
  uint32_t bytes; uint8_t*img=lfp_build_ex("z",0,0,in,5,&bytes); le_init(); assert(le_bind(img,bytes)==0); for(unsigned _i=0;_i<le_info()->n_slots;_i++) le_zone(_i)->r=40, le_zone(_i)->oct=0;
  const lfp_info_t*i=le_info(); assert(i->slot[0].voices==1 && i->slot[0].glide==20 && i->slot[1].choke==1 && i->slot[2].choke==1 && i->slot[3].voices==2 && i->slot[4].voices==0);
  int32_t b[256]; unsigned blk=0;
#define RUN(n) for(int _k=0;_k<(n);_k++){ le_render(b,128); blk++; }
  /* mono: two keys held -> still one voice (legato slide, no retrigger) */
  le_note_on(60,100); RUN(10); assert(le_voices_active()==1); le_note_on(67,100); RUN(10); assert(le_voices_active()==1);
  le_note_off(60); RUN(2); assert(le_voices_active()==1); le_note_off(67); RUN(400); assert(le_voices_active()==0);
  { le_bind(img,bytes); for(unsigned _i=0;_i<le_info()->n_slots;_i++) le_zone(_i)->r=40, le_zone(_i)->oct=0; le_note_on(60,100); RUN(40); int32_t prev=0, cr[16]; le_note_on(67,100);
    for(int w=0;w<16;w++){ cr[w]=0; for(int k=0;k<8;k++){ le_render(b,128); for(int f=0;f<128;f++){ int32_t x=b[2*f]; if((x<0)!=(prev<0)) cr[w]++; prev=x; } } }
    printf("zero crossings per 8 blocks after the second key: %d %d %d ... %d %d (slides 7 semitones up)\n",cr[0],cr[1],cr[2],cr[14],cr[15]);
    assert(cr[0]*13 <= cr[15]*10 && cr[7] >= cr[0] && cr[7] <= cr[15]); le_all_off(); RUN(300); }
  puts("mono + glide ok");
  /* choke: open hat sounding, closed hat cuts it */
  le_note_on(41,100); RUN(5); assert(le_voices_active()==1); le_note_on(40,100); RUN(3); RUN(40); assert(le_voices_active()==1);
  RUN(400); assert(le_voices_active()==0); puts("choke ok");
  /* zone voices = 2: third note steals the oldest of the zone */
  le_note_on(72,100); le_note_on(74,100); RUN(4); assert(le_voices_active()==2); le_note_on(76,100); RUN(60); assert(le_voices_active()==2);
  le_all_off(); RUN(500); assert(le_voices_active()==0); puts("zone voices ok");
  /* unlimited zone: 5 retriggers of the same free hit -> up to 5 voices */
  for(int k=0;k<5;k++){ le_note_on(90,100); RUN(2); } assert(le_voices_active()==5); puts("unlimited ok");
  puts("zone behaviour tests passed"); return 0; }
