#include <stdio.h>
#include <assert.h>
#include "lofi_engine.h"
int main(void){ static int16_t pcm[6400]; for(int i=0;i<6400;i++) pcm[i]=(int16_t)(8000*(i%80<40?1:-1));
  lfp_input_t in[3]={{"kick",pcm,6400,0,LFP_NO_LOOP,53,53,53,4},{"keys",pcm,6400,0,LFP_NO_LOOP,60,71,64,0},{"bass",pcm,6400,0,LFP_NO_LOOP,40,47,43,0}};
  uint32_t bytes; uint8_t*img=lfp_build_ex("t",4,0,in,3,&bytes); le_init(); assert(le_bind(img,bytes)==0); for(unsigned _i=0;_i<3;_i++) le_zone(_i)->oct=0;
  printf("pack scale %s root %u\n",le_scale_name(le_get_scale()),le_get_scale_root());
  /* keys start at F: C minor gives F G Ab Bb C D Eb, then F again an octave up; the zone 'keys' has the sample pitch E (root 64 -> pc 4), nearest F is one semitone up */
  int want[14]={1+0,1+2,1+3,1+5,1+7,1+9,1+10, 1+12,1+14,1+15,1+17,1+19,1+21,1+22};
  for(int k=0;k<14;k++){ int d=le_pitch_offset(1,60+k); printf("key %d offset %+d (want %+d)\n",k+1,d,want[k]); assert(d==want[k]); }
  assert(le_pitch_offset(0,53)==0);                        /* drum hit ignores the scale */
  le_set_scale(0,0); for(int k=0;k<14;k++) assert(le_pitch_offset(1,60+k)==1+k);          /* no scale: chromatic from F */
  le_set_scale(4,2);  /* D minor: D E F G A Bb C -> from F: F G A Bb C D E */
  { int w[7]={0,2,4,5,7,9,11}; for(int k=0;k<7;k++) assert(le_pitch_offset(1,60+k)==1+w[k]); }
  le_set_scale(3,0); /* C major from F: F G A B C D E */ { int w[7]={0,2,4,6,7,9,11}; for(int k=0;k<7;k++) assert(le_pitch_offset(1,60+k)==1+w[k]); }
  puts("scale tests passed"); return 0; }
