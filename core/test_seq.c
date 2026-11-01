#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "lofi_engine.h"
static int16_t pcm[32000]; static int32_t b[256];
static struct le_seq_status st(void){ struct le_seq_status s; le_seq_status_get(&s); return s; }
static void run(int n){ for(int i=0;i<n;i++) le_render(b,128); }
static void until_tick(unsigned t){ int guard=0; while(st().tick<t && guard++<100000) run(1); assert(guard<100000); }
int main(void){ for(int i=0;i<32000;i++) pcm[i]=(int16_t)(6000*((i/40)%2?1:-1));
  lfp_input_t in[2]={ {"kick",pcm,16000,0,LFP_NO_LOOP,53,53,53,4,0,0,0}, {"keys",pcm,32000,0,LFP_NO_LOOP,60,71,60,0,0,0,0} };
  uint32_t bytes; uint8_t*img=lfp_build_ex("s",0,0,in,2,&bytes); le_init(); assert(le_bind(img,bytes)==0); for(unsigned _i=0;_i<le_info()->n_slots;_i++) le_zone(_i)->r=40, le_zone(_i)->oct=0;
  le_seq_set_free(0); le_seq_set_bpm(120); le_seq_set_bars(1); le_seq_set_quant(2); le_seq_set_metro(1,1); run(1);
  struct le_seq_status s=st(); assert(s.bpm==120 && s.bars==1 && s.quant==2 && s.loop_ticks==384);
  /* record: count-in of one bar, then the kick a little after beat 2 (tick 96+5 -> 1/16 grid 24 -> 96), a key at tick 200 -> 192 */
  le_seq_rec(1); run(1); assert(st().state==LS_COUNTIN);
  int guard=0; while(st().state==LS_COUNTIN && guard++<5000) run(1); assert(st().state==LS_REC);
  until_tick(101); le_note_on(53,100); run(1); le_note_off(53); run(1);
  until_tick(200); le_note_on(64,90); until_tick(260); le_note_off(64); run(1);
  assert(st().events==4); printf("recorded %u events\n",st().events);
  uint8_t buf[4096]; unsigned n=le_seq_export(buf,sizeof buf); printf("export %u bytes\n",n);
  /* parse: pattern 0 */
  unsigned o=10; assert(buf[o]==1); unsigned cnt=buf[o+1]|buf[o+2]<<8; assert(cnt==4); o+=3;
  for(unsigned i=0;i<cnt;i++){ unsigned t=buf[o]|buf[o+1]<<8; printf("  tick %3u note %u %s\n",t,buf[o+2],buf[o+3]&128?"on":"off"); o+=5; }
  assert((buf[23]|buf[24]<<8)>=102 && "gate at least 6 ticks" );
  /* quantised starts: kick on at 96, key on at 192 */
  o=13; assert((buf[o]|buf[o+1]<<8)==96 && (buf[o+3]&128)); o+=10; assert((buf[o]|buf[o+1]<<8)==192 && (buf[o+3]&128));
  /* stop recording: loops back and plays; the kick must sound again at tick 96 of the next pass */
  le_seq_rec(0); run(1); assert(st().state==LS_PLAY);
  guard=0; while(st().tick>5 && guard++<5000) run(1); le_all_off(); run(2);                      /* wait for the wrap of the loop */
  until_tick(60); unsigned before=le_voices_active(); until_tick(100); run(2); assert(le_voices_active()>before); puts("kick played back from the loop at tick 96");
  /* undo removes the whole last REC pass, clear empties */
  le_seq_stop(); run(2); assert(st().state==LS_STOP); le_seq_undo(); run(2); assert(st().events==0); puts("undo ok");
  /* import / export round trip */
  assert(le_seq_import(buf,n)==0); assert(st().events==4); assert(st().bpm==120);
  /* overdub: a second REC pass keeps the first and adds; fresh notes are not played back twice in the same pass */
  le_seq_play(); run(1); le_seq_rec(1); run(1); assert(st().state==LS_REC); until_tick(10); le_note_on(53,100); run(1); le_note_off(53); until_tick(40); assert(st().events==6);
  le_seq_clear(); run(1); assert(st().events==0); puts("overdub + clear ok");
  /* a key still held when REC ends gets its release at that moment; a key held BEFORE rec records nothing */
  le_seq_stop(); run(1); le_seq_clear(); run(1); le_seq_play(); run(1); le_note_on(53,100); run(1); le_seq_rec(1); run(1); le_note_off(53); run(1); assert(st().events==0);
  until_tick(50); le_note_on(64,90); run(1); le_seq_rec(0); run(1); assert(st().events==2); le_note_off(64); run(1); assert(st().events==2); le_seq_stop(); run(1); le_seq_clear(); run(1); puts("held keys closed at REC end");
  /* free first take: starts with the first note, ends with REC, length -> bars + bpm (3 s at the provisional 120 bpm = 576 ticks -> 2 bars at ~160 bpm) */
  le_seq_stop(); run(1); le_seq_clear(); run(1); le_seq_set_bpm(120); le_seq_set_bars(1); le_seq_set_free(1); run(1);
  le_seq_rec(1); run(1); assert(st().state==LS_FREE && st().armed); run(400); assert(st().armed && st().tick==0 && "clock waits for the first note");
  le_note_on(53,100); run(1); le_note_off(53); until_tick(200); le_note_on(64,90); until_tick(575); le_note_off(64); run(1); until_tick(576);
  le_seq_rec(0); run(1); { struct le_seq_status f=st(); printf("free take -> %u bars, %u bpm, %u events\n",f.bars,f.bpm,f.events);
    assert(f.state==LS_PLAY && f.bars==2 && f.bpm>=158 && f.bpm<=162 && f.events==4 && f.loop_ticks==768); }
  { uint8_t fb[4096]; le_seq_export(fb,sizeof fb); unsigned t1=fb[13]|fb[14]<<8; assert(t1==0); }
  le_seq_stop(); run(1); le_seq_clear(); run(1);
  le_seq_rec(1); run(1); assert(st().state==LS_FREE); le_seq_rec(0); run(1); assert(st().state==LS_STOP && "empty free take is cancelled");
  le_seq_set_free(0); run(1); le_seq_set_bpm(120); le_seq_set_bars(1); run(1);
  /* tempo accuracy: one bar at 120 BPM = 2.0 s = 88200 frames */
  le_seq_stop(); run(1); le_seq_play(); run(1); long fr=0; guard=0; unsigned prev=0; while(guard++<100000){ run(1); fr+=128; unsigned t=st().tick; if(t<prev) break; prev=t; }
  printf("bar length %ld frames (ideal 88200)\n",fr); assert(fr>88200-300 && fr<88200+300);
  /* patterns stay in step: switching while playing keeps the position of the shared clock */
  le_seq_stop(); run(2); le_seq_set_bars(2); le_seq_select(0); le_seq_play(); run(2); until_tick(300);
  struct le_seq_status s1=st(); unsigned t_a=s1.tick; le_seq_set_bars(1); run(1); /* A is 1 bar now */ le_seq_select(1); run(1); le_seq_set_bars(2); run(1);
  struct le_seq_status s2=st(); printf("pattern switch: position before %u, after %u (the new pattern has %u ticks)\n",t_a,s2.tick,s2.loop_ticks); assert(s2.pattern==1 && s2.tick>=t_a && s2.tick<t_a+60);
  le_seq_select(0); run(1); struct le_seq_status s3=st(); assert(s3.tick>=s2.tick && s3.tick<s2.tick+40); puts("patterns play in step");
  puts("sequencer tests passed"); return 0; }
