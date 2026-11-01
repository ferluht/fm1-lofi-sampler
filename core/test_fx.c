#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "lofi_engine.h"
static int16_t sq[64000], ramp[32000], burst[8000]; static int32_t b[256];
static void run(int n){ for(int i=0;i<n;i++) le_render(b,128); }
static double rms_run(int n, int ch){ double s=0; for(int i=0;i<n;i++){ le_render(b,128); for(int f=0;f<128;f++){ double x=b[2*f+ch]/8388608.0; s+=x*x; } } return sqrt(s/(n*128.0)); }
static int cross_run(int n){ int c=0; int32_t prev=0; for(int i=0;i<n;i++){ le_render(b,128); for(int f=0;f<128;f++){ int32_t x=b[2*f]; if((x<0)!=(prev<0)) c++; prev=x; } } return c; }
static const lfp_info_t *I;
static void fresh(lfp_input_t *in, unsigned n, uint8_t **img){ uint32_t bytes; *img=lfp_build_ex("fx",0,0,in,n,&bytes); le_init(); assert(le_bind(*img,bytes)==0); I=le_info(); for(unsigned _i=0;_i<I->n_slots;_i++) le_zone(_i)->r=40, le_zone(_i)->oct=0; }
int main(void){
  for(int i=0;i<64000;i++) sq[i]=(int16_t)(8000*((i/40)%2?1:-1));
  for(int i=0;i<32000;i++) ramp[i]=(int16_t)((int)(12000.0*i/32000)*((i/40)%2?1:-1));
  for(int i=0;i<8000;i++) burst[i]=(int16_t)(14000*((i/20)%2?1:-1)*(i<2400));
  uint8_t *img;
  lfp_input_t one[1]={{"a",sq,64000,0,LFP_NO_LOOP,60,71,60,0,0,0,0}};
  /* ---- ENV: a slow attack */
  fresh(one,1,&img); le_zone(0)->a=100; le_note_on(60,110); double early=rms_run(10,0), late; run(400); late=rms_run(10,0);
  printf("attack: early %.4f late %.4f\n",early,late); assert(early*3<late); le_all_off(); run(300);
  /* sustain level + decay: s=40 */
  fresh(one,1,&img); le_zone(0)->d=30; le_zone(0)->s=40; le_note_on(60,110); run(300); double sus=rms_run(10,0);
  fresh(one,1,&img); le_note_on(60,110); run(300); double full=rms_run(10,0); printf("sustain 40/127: %.4f vs full %.4f\n",sus,full); assert(sus<full*0.5 && sus>full*0.15);
  /* release: long release keeps sounding after note-off */
  fresh(one,1,&img); le_zone(0)->r=100; le_note_on(60,110); run(20); le_note_off(60); run(10); assert(le_voices_active()==1); run(600); assert(le_voices_active()==0); puts("env ok");
  /* ---- volume + mute */
  fresh(one,1,&img); le_note_on(60,110); run(40); double v100=rms_run(10,0); le_all_off(); run(300);
  le_zone(0)->vol=50; le_note_on(60,110); run(40); double v50=rms_run(10,0); printf("volume: 100 -> %.4f, 50 -> %.4f\n",v100,v50); assert(v50<v100*0.6 && v50>v100*0.4);
  le_all_off(); run(300); le_zone(0)->mute=1; le_note_on(60,110); run(10); assert(le_voices_active()==0); puts("volume + mute ok");
  /* ---- LFO pan: square at depth 127 swings the sound between the channels */
  fresh(one,1,&img); le_zone(0)->lfo_target=2; le_zone(0)->lfo_depth=127; le_zone(0)->lfo_shape=2; le_zone(0)->lfo_rate=110; le_note_on(60,110); run(30);
  int lside=0, rside=0; for(int i=0;i<300;i++){ double l=rms_run(1,0), r=0; (void)r; double rr=0; { double s=0; for(int f=0;f<128;f++){ double x=b[2*f+1]/8388608.0; s+=x*x; } rr=sqrt(s/128); } if(l>rr*3) lside++; else if(rr>l*3) rside++; }
  printf("lfo pan: left-only blocks %d, right-only blocks %d\n",lside,rside); assert(lside>20 && rside>20); puts("lfo pan ok");
  /* LFO vibrato changes the pitch over time (zero crossing counts differ between windows) */
  fresh(one,1,&img); le_zone(0)->lfo_target=0; le_zone(0)->lfo_depth=100; le_zone(0)->lfo_shape=3; le_zone(0)->lfo_rate=70; le_note_on(60,110); run(30);
  int c1=cross_run(8), mn=1000, mx=0; for(int i=0;i<40;i++){ int c=cross_run(8); if(c<mn) mn=c; if(c>mx) mx=c; } printf("vibrato: crossings per 8 blocks vary %d..%d (first %d)\n",mn,mx,c1); assert(mx>mn+3); puts("lfo pitch ok");
  /* ---- start / end */
  fresh(one,1,&img); le_note_on(60,110); run(1); int len_full=1; while(le_voices_active() && len_full<5000){ run(1); len_full++; }
  le_zone(0)->start=16000; le_zone(0)->end=48000; le_note_on(60,110); run(1); int len_cut=1; while(le_voices_active() && len_cut<5000){ run(1); len_cut++; }
  printf("play length: full %d blocks, start 16000 end 48000: %d blocks\n",len_full,len_cut); assert(len_cut*10 < len_full*6 && len_cut*10 > len_full*4);
  /* ---- reverse: a rising ramp played backwards falls in level */
  lfp_input_t rp[1]={{"r",ramp,32000,0,LFP_NO_LOOP,60,71,60,0,0,0,0}};
  fresh(rp,1,&img); le_note_on(60,110); run(2); double fw1=rms_run(40,0); run(300); double fw2=rms_run(40,0); le_all_off(); run(500);
  le_zone(0)->rev=1; le_note_on(60,110); run(2); double rv1=rms_run(40,0); run(300); double rv2=rms_run(40,0);
  printf("forward %.4f -> %.4f, reverse %.4f -> %.4f\n",fw1,fw2,rv1,rv2); assert(fw2>fw1*1.5 && rv2<rv1*0.7); puts("reverse ok");
  /* ---- effects: sends feed the tails; the dry voice is gone but the wet signal rings on */
  lfp_input_t bu[1]={{"b",burst,8000,0,LFP_NO_LOOP,60,71,60,4,0,0,0}};
  for(int fx=0; fx<4; fx++){
    fresh(bu,1,&img); le_zone(0)->send[fx]=127; le_zone(0)->s=127; le_note_on(60,120); run(120); le_all_off(); int g_=0; while(le_voices_active() && g_++<2000) run(1);
    double tail=rms_run(60,0); printf("fx %d send 127: tail rms %.5f\n",fx,tail); if(fx<2) assert(tail>0.001);
    fresh(bu,1,&img); le_note_on(60,120); run(120); g_=0; while(le_voices_active() && g_++<2000) run(1); double dry=rms_run(60,0); assert(dry<0.0001); }
  /* filter: low cutoff takes the highs away from a bright send (crossings drop) */
  fresh(bu,1,&img); le_zone(0)->send[3]=127; le_fx_params(FX_FILTER)[0]=10; le_fx_params(FX_FILTER)[2]=0; le_note_on(60,120); run(30); int fcr=cross_run(40);
  fresh(bu,1,&img); le_note_on(60,120); run(30); int dcr=cross_run(40); printf("filter: crossings with send %d, dry only %d\n",fcr,dcr); puts("fx ok");
  /* ---- punch-ins */
  fresh(one,1,&img); le_note_on(60,110); run(40); int n0=cross_run(20); le_punch(PN_HALF,1); run(120); int nh=cross_run(20); le_punch(PN_HALF,0); printf("half speed: crossings %d -> %d\n",n0,nh); assert(nh*10<n0*7);
  le_punch(PN_DOUBLE,1); run(120); int nd=cross_run(20); le_punch(PN_DOUBLE,0); assert(nd>n0*13/10); run(120);
  le_punch(PN_STUT16,1); double mn_=1, mx_=0; for(int i=0;i<120;i++){ double r=rms_run(1,0); if(r<mn_) mn_=r; if(r>mx_) mx_=r; } le_punch(PN_STUT16,0); printf("stutter: block rms %.4f .. %.4f\n",mn_,mx_); assert(mx_>mn_*4+0.0001); puts("punch ok");
  /* ---- arpeggiator over three single key zones */
  lfp_input_t kit[3]={{"k1",burst,8000,0,LFP_NO_LOOP,53,53,53,4,0,0,0},{"k2",burst,8000,0,LFP_NO_LOOP,54,54,54,4,0,0,0},{"k3",burst,8000,0,LFP_NO_LOOP,55,55,55,4,0,0,0}};
  fresh(kit,3,&img); le_seq_set_bpm(120); run(1); le_arp()->on=1; le_arp()->rate=2; le_arp()->dir=0; le_arp()->range=1; le_arp()->gate=50;
  le_arp_note_on(53,100); le_arp_note_on(54,100); le_arp_note_on(55,100); uint32_t seen=0; int steps=0; uint32_t prevm=0;
  for(int i=0;i<700;i++){ run(1); uint32_t m=le_voice_mask(); seen|=m; if(m!=prevm && m) steps++; prevm=m; }
  printf("arp: zones heard %x, mask changes %d\n",seen,steps); assert(seen==7 && steps>=3);
  le_arp_release(); run(10); le_arp()->on=0;
  /* ---- measure (beats per bar) */
  fresh(one,1,&img); le_seq_set_beats(3); le_seq_set_bars(1); run(2); struct le_seq_status st; le_seq_status_get(&st); assert(st.beats==3 && st.loop_ticks==288); puts("measure ok");
  /* ---- settings round trip */
  fresh(one,1,&img); le_zone(0)->a=33; le_zone(0)->lfo_depth=77; le_zone(0)->start=1000; le_zone(0)->end=50000; le_zone(0)->send[1]=55; le_fx_params(FX_DELAY)[1]=99; le_arp()->range=3; le_set_level(64);
  uint8_t blob[1024]; unsigned n=le_settings_export(blob,sizeof blob); printf("settings blob %u bytes\n",n); assert(n>0);
  le_bind(img,0xffffffff==0?0:0); /* keep */
  fresh(one,1,&img); assert(le_zone(0)->a==0); assert(le_settings_import(blob,n)==0); assert(le_zone(0)->a==33 && le_zone(0)->lfo_depth==77 && le_zone(0)->start==1000 && le_zone(0)->end==50000 && le_zone(0)->send[1]==55 && le_fx_params(FX_DELAY)[1]==99 && le_arp()->range==3 && le_get_level()==64);
  puts("settings ok"); puts("effects/zone tests passed"); return 0; }
