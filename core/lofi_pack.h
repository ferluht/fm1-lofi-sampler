/* (documentation: docs/PACK_FORMAT.md)
 * lofi pack image format (little endian). A pack is one flat image, uploaded as-is and stored from data sector 0.
 *   0   magic 'LFP1'   4   u16 version(=1)  6   u16 n_slots (<=32)   8  char name[16]   24 u32 total_frames
 *   28  u8 scale (0 off, 1..7 = lofi_scale table)  29 u8 scale root pitch class (0..11)   30..63 reserved(0)
 *   64  slot table, 32 slots x 32 B:  u32 offset (bytes from image start, to the slot's superblocks)  u32 frames
 *                                     u32 loop_start  u32 loop_end (0xFFFFFFFF = no loop)  u8 lo u8 hi u8 root u8 flags  char name[11] + i8 tune (byte 31: fine tune in cents added to the playback pitch)   (lo/hi are note ids: bank*32 + key, a zone = bank of 27 keys; root of a melodic sample = its pitch class 0..11)
 *   1088 zone extras, 32 slots x 2 B: byte0 low nibble = max voices of the zone (0 = unlimited, 1 = mono), high nibble = choke group (0 none, 1..15:
 *        a note in the group cuts every sounding voice of every zone of the group); byte1 = glide time (0 off, else value x 10 ms, legato: a new key
 *        while one is held slides the voice, no retrigger)
 *   1152 first slot data (LFP_HEADER_BYTES), each slot = lofi_encoded_bytes(frames) bytes (whole 192 B superblocks)
 * flags: bit0 loop on, bit1 reverse default, bit2 one-shot (ignore note-off). Notes lo..hi play the slot. */
#ifndef LOFI_PACK_H
#define LOFI_PACK_H
#include "lofi_codec.h"
#define LFP_MAGIC 0x3150464cu
#define LFP_SLOTS 32u
#define LFP_HEADER_BYTES 1152u
#define LFP_NO_LOOP 0xffffffffu
typedef struct { uint32_t offset, frames, loop_start, loop_end; uint8_t lo, hi, root, flags; char name[13]; uint8_t voices, choke, glide; int8_t tune; } lfp_slot_t;
typedef struct { uint16_t n_slots; char name[17]; uint32_t total_frames; uint8_t scale, scale_root; lfp_slot_t slot[LFP_SLOTS]; } lfp_info_t;
static inline void lfp_p32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static inline uint32_t lfp_g32(const uint8_t *p){ return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24; }
/* parse and validate the header (LFP_HEADER_BYTES bytes) of an image of image_bytes; returns 0 ok, <0 invalid */
static int lfp_parse(const uint8_t *h, uint32_t image_bytes, lfp_info_t *o){
    if(image_bytes < LFP_HEADER_BYTES || lfp_g32(h) != LFP_MAGIC || (h[4]|h[5]<<8) != 1) return -1;
    memset(o,0,sizeof *o); o->n_slots = (uint16_t)(h[6]|h[7]<<8); if(o->n_slots > LFP_SLOTS) return -2;
    memcpy(o->name,h+8,16); o->total_frames = lfp_g32(h+24); o->scale = h[28] > 7 ? 0 : h[28]; o->scale_root = h[29] % 12;
    for(unsigned i=0;i<o->n_slots;i++){ const uint8_t *s=h+64+i*32; lfp_slot_t *t=&o->slot[i];
        t->offset=lfp_g32(s); t->frames=lfp_g32(s+4); t->loop_start=lfp_g32(s+8); t->loop_end=lfp_g32(s+12);
        t->lo=s[16]; t->hi=s[17]; t->root=s[18]; t->flags=s[19]; memcpy(t->name,s+20,11); t->tune=(int8_t)s[31];
        t->voices=h[1088+2*i]&15; t->choke=h[1088+2*i]>>4; t->glide=h[1089+2*i];
        if(t->offset < LFP_HEADER_BYTES || (uint64_t)t->offset + lofi_encoded_bytes(t->frames) > image_bytes) return -3;
        if(t->loop_end != LFP_NO_LOOP && (t->loop_end > t->frames || t->loop_start >= t->loop_end)) return -4; }
    return 0; }
#ifndef LOFI_DECODE_ONLY
/* host/app side: build an image from PCM (16 kHz mono int16). Returns malloc'd image, size in *bytes. */
typedef struct { const char *name; const int16_t *pcm; uint32_t frames, loop_start, loop_end; uint8_t lo, hi, root, flags, voices, choke, glide; int8_t tune; } lfp_input_t;
static uint8_t *lfp_build_ex(const char *name, unsigned scale, unsigned scale_root, const lfp_input_t *in, unsigned n, uint32_t *bytes){
    if(n > LFP_SLOTS) return NULL; uint32_t total = LFP_HEADER_BYTES, frames = 0;
    for(unsigned i=0;i<n;i++){ total += lofi_encoded_bytes(in[i].frames); frames += in[i].frames; }
    uint8_t *img = calloc(total,1); if(!img) return NULL;
    lfp_p32(img,LFP_MAGIC); img[4]=1; img[6]=(uint8_t)n; strncpy((char*)img+8,name,15); lfp_p32(img+24,frames); img[28]=(uint8_t)scale; img[29]=(uint8_t)scale_root;
    uint32_t off = LFP_HEADER_BYTES;
    for(unsigned i=0;i<n;i++){ uint8_t *s = img+64+i*32; uint8_t *e = lofi_encode(in[i].pcm,in[i].frames,NULL); if(!e){ free(img); return NULL; }
        uint32_t eb = lofi_encoded_bytes(in[i].frames); memcpy(img+off,e,eb); free(e);
        lfp_p32(s,off); lfp_p32(s+4,in[i].frames); lfp_p32(s+8,in[i].loop_start); lfp_p32(s+12,in[i].loop_end);
        s[16]=in[i].lo; s[17]=in[i].hi; s[18]=in[i].root; s[19]=in[i].flags; strncpy((char*)s+20,in[i].name,11); s[31]=(uint8_t)in[i].tune;
        img[1088+2*i]=(uint8_t)((in[i].voices&15)|(in[i].choke<<4)); img[1089+2*i]=in[i].glide; off += eb; }
    *bytes = total; return img; }
static uint8_t *lfp_build(const char *name, const lfp_input_t *in, unsigned n, uint32_t *bytes){ return lfp_build_ex(name,0,0,in,n,bytes); }
#endif /* LOFI_DECODE_ONLY */
#endif
