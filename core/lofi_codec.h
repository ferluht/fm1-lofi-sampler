/* (documentation: docs/CODEC.md)
 * lofi codec: 16 kHz mono, 2 bit/sample, SEA-style LMS-ADPCM (integer, bit exact between encoder and decoder).
 * Format:  group = 20 samples, chunk = 16 groups = 320 samples = 80 B codes + 8 B scales (4 bit per group)
 *          superblock = 2 chunks = 640 samples: 16 B seed (8 x int16 LMS state at its start) + 2 x 88 B = 192 B (4.8 KB/s)
 * Every superblock starts from its own seed, so it is decoded independently: seek, loop points, reverse, scrub.
 * Decoder is allocation free and RAM-light (state 8 x int32). Encoder is for the phone/host only. */
#ifndef LOFI_CODEC_H
#define LOFI_CODEC_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define LOFI_GROUP 20u
#define LOFI_CHUNK 320u
#define LOFI_SUPER 640u
#define LOFI_SUPER_BYTES 192u
#ifndef LOFI_INL
#define LOFI_INL static inline __attribute__((always_inline))   /* decoder runs in the audio interrupt: always inline, never in flash */
#endif
#ifndef LOFI_DQT_ATTR
#define LOFI_DQT_ATTR
#endif
#include "lofi_dqt.inc"
LOFI_INL int32_t lofi_sat(int32_t x){ return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
LOFI_INL int32_t lofi_predict(const int32_t s[8]){ int64_t a=0; for(int i=0;i<4;i++) a+=(int64_t)s[i]*s[4+i]; return (int32_t)(a>>13); }
LOFI_INL void lofi_update(int32_t s[8], int32_t pcm, int32_t res){
    int32_t d=res>>4; for(int i=0;i<4;i++) s[4+i]=lofi_sat(s[4+i]+(s[i]<0?-d:d));
    for(int i=0;i<3;i++) s[i]=s[i+1]; s[3]=pcm; }
/* decode one group (0..31 inside a superblock) given the running state; chunk data pointer layout below */
static inline const uint8_t *lofi_chunk(const uint8_t *sb, unsigned c){ return sb + 16 + c*88; }
LOFI_INL void lofi_seed(const uint8_t *sb, int32_t s[8]){ for(int i=0;i<8;i++) s[i]=(int16_t)(sb[2*i]|sb[2*i+1]<<8); }
LOFI_INL void lofi_decode_group(const uint8_t *chunk, unsigned g, int32_t s[8], int16_t *pcm){
    unsigned sc=(chunk[80+g/2]>>((g&1)?0:4))&15; const uint8_t *d=chunk+g*5;
    for(unsigned i=0;i<LOFI_GROUP;i++){
        unsigned code=(d[i>>2]>>(6-2*(i&3)))&3; int32_t res=lofi_dqt[sc*4+code];
        int32_t v=lofi_sat(lofi_predict(s)+res); lofi_update(s,v,res); pcm[i]=(int16_t)v; } }
/* whole superblock -> 640 samples (independent of any other superblock) */
LOFI_INL void lofi_decode_super(const uint8_t *sb, int16_t pcm[LOFI_SUPER]){
    int32_t s[8]; lofi_seed(sb,s);
    for(unsigned g=0;g<32;g++) lofi_decode_group(lofi_chunk(sb,g/16), g%16, s, pcm+g*LOFI_GROUP); }
LOFI_INL unsigned lofi_encoded_bytes(unsigned frames){ return (frames+LOFI_SUPER-1)/LOFI_SUPER*LOFI_SUPER_BYTES; }
#ifndef LOFI_DECODE_ONLY
static const int32_t lofi_state_init[8] = {0,0,0,0,0,0,-8192,16384};
static inline uint64_t lofi_pen(const int32_t s[8]){ int64_t a=0; for(int i=4;i<8;i++) a+=(int64_t)s[i]*s[i]; int64_t v=(a>>18)-0x8ff; return v>0?(uint64_t)(v*v):0; }
/* encode `frames` samples; returns malloc'd buffer of lofi_encoded_bytes(frames); recon (optional) gets the decoded signal */
static uint8_t *lofi_encode(const int16_t *in, unsigned frames, int16_t *recon){
    unsigned nsb=(frames+LOFI_SUPER-1)/LOFI_SUPER; uint8_t *out=calloc(nsb,LOFI_SUPER_BYTES); if(!out) return NULL;
    int32_t s[8]; memcpy(s,lofi_state_init,sizeof s); unsigned prev=0;
    for(unsigned b=0;b<nsb;b++){ uint8_t *sb=out+b*LOFI_SUPER_BYTES;
        for(int i=0;i<8;i++){ sb[2*i]=(uint8_t)s[i]; sb[2*i+1]=(uint8_t)((uint16_t)s[i]>>8); }
        for(unsigned g=0;g<32;g++){ uint8_t *chunk=sb+16+(g/16)*88; unsigned gi=g%16;
            uint64_t best=UINT64_MAX; int32_t bs[8]; uint8_t bc[LOFI_GROUP]; int16_t bo[LOFI_GROUP]; unsigned bsf=prev;
            for(unsigned k=0;k<16;k++){ unsigned sf=(k+prev)&15; int32_t t[8]; memcpy(t,s,sizeof t); uint64_t rank=0; uint8_t cc[LOFI_GROUP]; int16_t oo[LOFI_GROUP]; unsigned i;
                for(i=0;i<LOFI_GROUP;i++){ unsigned p=b*LOFI_SUPER+g*LOFI_GROUP+i; int32_t pcm=p<frames?in[p]:0, pr=lofi_predict(t);
                    unsigned bq=0; int32_t be=INT32_MAX;
                    for(unsigned q=0;q<4;q++){ int32_t e=pcm-lofi_sat(pr+lofi_dqt[sf*4+q]); if(abs(e)<be){be=abs(e);bq=q;} }
                    int32_t res=lofi_dqt[sf*4+bq], rec=lofi_sat(pr+res); int64_t er=pcm-rec; rank+=(uint64_t)(er*er)+lofi_pen(t);
                    if(rank>best) break; cc[i]=(uint8_t)bq; oo[i]=(int16_t)rec; lofi_update(t,rec,res); }
                if(i==LOFI_GROUP && rank<best){ best=rank; memcpy(bs,t,sizeof t); memcpy(bc,cc,sizeof cc); memcpy(bo,oo,sizeof oo); bsf=sf; } }
            prev=bsf; memcpy(s,bs,sizeof s);
            chunk[80+gi/2]|=(uint8_t)(bsf<<((gi&1)?0:4));
            for(unsigned i=0;i<LOFI_GROUP;i++){ chunk[gi*5+(i>>2)]|=(uint8_t)(bc[i]<<(6-2*(i&3)));
                if(recon){ unsigned p=b*LOFI_SUPER+g*LOFI_GROUP+i; if(p<frames) recon[p]=bo[i]; } } } }
    return out; }
#endif /* LOFI_DECODE_ONLY */
#endif
