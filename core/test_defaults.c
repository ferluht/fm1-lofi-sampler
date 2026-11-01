#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "lofi_engine.h"
static int16_t pcm[16000]; static int32_t b[256];
static double rms(int n){ double s=0; for(int i=0;i<n;i++){ le_render(b,128); for(int f=0;f<256;f++){ double x=b[f]/8388608.0; s+=x*x; } } return sqrt(s/(n*256.0)); }
int main(void){ le_init(); assert(le_get_scale()==4 && le_get_scale_root()==0); puts("scale is C minor from the start");
  struct le_seq_status st; le_seq_status_get(&st); assert(st.bpm==140 && st.freemode==1); puts("bpm 140, free first take by default");
  for(int i=0;i<16000;i++) pcm[i]=(int16_t)(6000*((i/40)%2?1:-1));
  lfp_input_t in[2]={{"keys",pcm,16000,0,LFP_NO_LOOP,0,26,5,0,0,0,0},{"kit",pcm,16000,0,LFP_NO_LOOP,32,32,32,4,0,0,0}};
  uint32_t bytes; uint8_t *img=lfp_build_ex("d",0,0,in,2,&bytes); assert(le_bind(img,bytes)==0);
  { lfp_input_t tn[1]={{"t",pcm,16000,0,LFP_NO_LOOP,0,26,5,0,0,0,0,-30}}; uint32_t tb; uint8_t *ti=lfp_build_ex("t",0,0,tn,1,&tb); le_init(); assert(le_bind(ti,tb)==0); assert(le_info()->slot[0].tune==-30 && le_info()->slot[0].name[0]=='t'); puts("fine tune (cents) survives the pack"); le_init(); assert(le_bind(img,bytes)==0); }
  unsigned ms=1u<<((le_zone(0)->r*12/127)); printf("default release idx %u = about %u ms\n",le_zone(0)->r,ms); assert(le_zone(0)->r>=105);
  assert(le_zone(0)->oct==-1 && le_zone(1)->oct==0); puts("a melodic zone starts one octave down"); le_zone(0)->oct=0; le_set_scale(0,0); int base=le_pitch_offset(0,5); le_zone(0)->oct=1; assert(le_pitch_offset(0,5)==base+12); le_zone(0)->oct=-2; assert(le_pitch_offset(0,5)==base-24); le_zone(0)->oct=0;
  le_set_scale(4,0); int a=le_pitch_offset(0,0); le_zone(0)->oct=1; assert(le_pitch_offset(0,0)==a+12); assert(le_pitch_offset(1,32)==0); le_zone(0)->oct=0; puts("base octave moves a melodic zone, a kit key stays");
  /* all 27 keys of a melodic zone sound different, up to 6x the sample rate */
  le_zone(0)->oct=-1; le_set_scale(4,0); le_zone(0)->r=30; int last_c=-1; int c_[5]; int ks[5]={0,7,14,21,26};
  for(int q=0;q<5;q++){ le_all_off(); for(int i=0;i<40;i++) le_render(b,128); le_note_on((unsigned)ks[q],110); for(int i=0;i<10;i++) le_render(b,128); int cr=0; int32_t prev=0; for(int i=0;i<20;i++){ le_render(b,128); for(int f=0;f<128;f++){ int32_t x=b[2*f]; if((x<0)!=(prev<0)) cr++; prev=x; } } c_[q]=cr; }
  printf("zero crossings of keys 1,8,15,22,27: %d %d %d %d %d\n",c_[0],c_[1],c_[2],c_[3],c_[4]); for(int q=1;q<5;q++) assert(c_[q]>c_[q-1]+4); (void)last_c; le_all_off(); for(int i=0;i<80;i++) le_render(b,128); le_zone(0)->oct=0;
  /* metronome: silent while playing, clicks while recording (count-in off) */
  le_seq_set_bpm(140); le_seq_set_metro(1,0); le_seq_set_bars(1); le_render(b,128); le_seq_play(); le_render(b,128);
  double play=rms(300); le_seq_rec(1); le_render(b,128); le_seq_status_get(&st); assert(st.state==LS_REC); double rec=rms(300);
  printf("metronome: rms while playing %.5f, while recording %.5f\n",play,rec); assert(play<1e-6 && rec>1e-3); puts("defaults ok"); return 0; }
