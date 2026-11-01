/* (documentation: docs/UPLOAD_PROTOCOL.md)
 * lofi pack upload protocol over BLE/USB MIDI SysEx + device-side state machine (flash abstracted).
 * Frame:  F0 7D 'L' 'F' 01 cmd seq_lo seq_hi  <payload packed 8->7 bit, LSB first>  checksum F7
 *         checksum makes the sum of bytes 7D..checksum = 0 mod 128. seq = 14 bit, echoed in the reply. Reply cmd = cmd|0x40.
 *         Every frame <= 192 bytes (stock fm1_app_sysex limit).
 * Commands (payload raw, little endian):
 *   1 HELLO   ()                                  -> status, ver(1), data_sectors u16, state u8(0 empty,1 pack,2 uploading), pack_size u32, pack_crc u32, gen u32, next_sector u16
 *   2 BEGIN   (pack_size u32, pack_crc u32)       -> status, next_sector u16, sector_count u16   (same size+crc while still uploading = resume)
 *   3 DATA    (sector u16, block u8, 128 B, crc16 u16)   no reply. Only the sector == next_sector is accepted, 32 blocks of 128 B per 4096 B sector.
 *   4 QUERY   (sector u16)                        -> status, sector u16, done u8, bitmap u32 (blocks received of the sector in progress)
 *   5 END     (pack_crc u32)                      -> status  (verifies CRC32 of the whole image in flash, then commits; a repeated END of the
 *                                                    already committed pack answers ok, so a lost reply can be retried)
 *   6 ABORT   ()  7 DELETE ()                     -> status
 * status: 0 ok, 1 no space, 2 bad argument, 3 wrong state, 4 crc mismatch, 5 flash io error.
 * Flash: sectors 0,1 = commit records (alternate by generation, power-loss safe), sectors 2.. = pack image, the last two sectors = saved loops and settings (firmware).
 * BEGIN first invalidates the old pack, so an interrupted upload leaves an empty bank, never a half pack. */
#ifndef LOFI_PROTO_H
#define LOFI_PROTO_H
#include <stdint.h>
#include <string.h>
#define LF_SECTOR 4096u
#define LF_BLOCK 128u
#define LF_BLOCKS 32u
#define LF_MAXMSG 192u
enum { LF_HELLO=1, LF_BEGIN, LF_DATA, LF_QUERY, LF_END, LF_ABORT, LF_DELETE };
enum { LF_OK=0, LF_NOSPACE, LF_BADARG, LF_STATE, LF_CRC, LF_IO };
enum { LF_EMPTY=0, LF_PACK=1, LF_UPLOADING=2 };

static inline uint32_t lf_crc32(uint32_t crc, const uint8_t *p, size_t n){
    crc = ~crc; while(n--){ crc ^= *p++; for(int k=0;k<8;k++) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1))); } return ~crc; }
static inline uint16_t lf_crc16(const uint8_t *p, size_t n){
    uint16_t c = 0xffff; while(n--){ c ^= (uint16_t)(*p++ << 8); for(int k=0;k<8;k++) c = (c&0x8000) ? (uint16_t)((c<<1)^0x1021) : (uint16_t)(c<<1); } return c; }
static inline size_t lf_pack7(const uint8_t *in, size_t n, uint8_t *out){
    uint32_t acc=0; int bits=0; size_t o=0;
    for(size_t i=0;i<n;i++){ acc |= (uint32_t)in[i] << bits; bits += 8; while(bits >= 7){ out[o++] = acc & 0x7f; acc >>= 7; bits -= 7; } }
    if(bits) out[o++] = acc & 0x7f; return o; }
static inline size_t lf_unpack7(const uint8_t *in, size_t n, uint8_t *out){
    uint32_t acc=0; int bits=0; size_t o=0;
    for(size_t i=0;i<n;i++){ acc |= (uint32_t)(in[i]&0x7f) << bits; bits += 7; if(bits >= 8){ out[o++] = acc & 0xff; acc >>= 8; bits -= 8; } }
    return o; }
/* build a frame; returns length */
static inline size_t lf_frame(uint8_t *out, unsigned cmd, unsigned seq, const uint8_t *payload, size_t n){
    size_t o=0; out[o++]=0xf0; out[o++]=0x7d; out[o++]='L'; out[o++]='F'; out[o++]=1; out[o++]=(uint8_t)cmd;
    out[o++]=seq&0x7f; out[o++]=(seq>>7)&0x7f; o += lf_pack7(payload,n,out+o);
    unsigned sum=0; for(size_t i=1;i<o;i++) sum += out[i]; out[o++] = (uint8_t)((128 - (sum&0x7f)) & 0x7f); out[o++]=0xf7; return o; }
/* parse a frame; returns payload length or -1 */
static inline int lf_parse(const uint8_t *m, size_t n, unsigned *cmd, unsigned *seq, uint8_t *payload /* >= LF_MAXMSG */){
    if(n < 10 || n > LF_MAXMSG || m[0]!=0xf0 || m[n-1]!=0xf7 || m[1]!=0x7d || m[2]!='L' || m[3]!='F' || m[4]!=1) return -1;
    unsigned sum=0; for(size_t i=1;i<n-1;i++){ if(m[i]&0x80) return -1; sum += m[i]; } if(sum & 0x7f) return -1;
    *cmd=m[5]; *seq=m[6]|m[7]<<7; return (int)lf_unpack7(m+8,n-10,payload); }

/* ---------------- device side ---------------- */
typedef struct { void *ctx; uint32_t size;   /* bytes of the user partition */
    int (*read)(void*, uint32_t off, void *p, size_t n); int (*write)(void*, uint32_t off, const void *p, size_t n); int (*erase)(void*, uint32_t sector_off); } lf_flash_t;
typedef struct { lf_flash_t fl; uint8_t secbuf[LF_SECTOR]; uint32_t mask, gen, pack_size, pack_crc; uint16_t next, total; uint8_t state; } lf_dev_t;
#define LF_REC_MAGIC 0x4d43464cu  /* 'LFCM' */
static inline uint32_t lf_data_sectors(const lf_dev_t *d){ return d->fl.size/LF_SECTOR - 4; }   /* minus the two commit records and the two sectors of the saved loops + settings */
static inline unsigned lf_blocks_in(const lf_dev_t *d, unsigned s){
    uint32_t rem = d->pack_size - s*LF_SECTOR; if(rem > LF_SECTOR) rem = LF_SECTOR; return (rem + LF_BLOCK-1)/LF_BLOCK; }
static inline uint32_t lf_full_mask(unsigned blocks){ return blocks >= 32 ? 0xffffffffu : (1u<<blocks)-1; }
static inline void lf_p32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static inline uint32_t lf_g32(const uint8_t *p){ return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24; }
static inline int lf_write_record(lf_dev_t *d, unsigned state, uint32_t size, uint32_t crc){
    uint8_t r[32]; memset(r,0,sizeof r); uint32_t gen = d->gen + 1;
    lf_p32(r,LF_REC_MAGIC); lf_p32(r+4,gen); lf_p32(r+8,state); lf_p32(r+12,size); lf_p32(r+16,crc); lf_p32(r+28,lf_crc32(0,r,28));
    uint32_t off = (gen & 1) * LF_SECTOR;
    if(d->fl.erase(d->fl.ctx,off) || d->fl.write(d->fl.ctx,off,r,sizeof r)) return -1;
    d->gen = gen; return 0; }
/* power-on: pick the valid record with the highest generation */
static inline void lf_dev_init(lf_dev_t *d){
    d->mask=d->gen=d->pack_size=d->pack_crc=0; d->next=d->total=0; d->state=LF_EMPTY;
    for(unsigned s=0;s<2;s++){ uint8_t r[32]; if(d->fl.read(d->fl.ctx,s*LF_SECTOR,r,sizeof r)) continue;
        if(lf_g32(r)!=LF_REC_MAGIC || lf_g32(r+28)!=lf_crc32(0,r,28)) continue;
        uint32_t gen = lf_g32(r+4); if(gen <= d->gen && d->gen) continue;
        d->gen=gen; d->pack_size=lf_g32(r+12); d->pack_crc=lf_g32(r+16); d->state = lf_g32(r+8)==LF_PACK ? LF_PACK : LF_EMPTY; }
    if(d->state==LF_EMPTY) d->pack_size=d->pack_crc=0; }
static inline size_t lf_reply(uint8_t *out, unsigned cmd, unsigned seq, const uint8_t *pl, size_t n){ return lf_frame(out,cmd|0x40,seq,pl,n); }
/* handle one incoming SysEx; writes the reply frame to out (>= LF_MAXMSG) and returns its length, 0 = no reply */
static inline size_t lf_dev_handle(lf_dev_t *d, const uint8_t *m, size_t n, uint8_t *out){
    uint8_t p[LF_MAXMSG], r[24]; unsigned cmd, seq; int pn = lf_parse(m,n,&cmd,&seq,p); if(pn < 0) return 0;
    switch(cmd){
    case LF_HELLO: r[0]=LF_OK; r[1]=1; r[2]=(uint8_t)lf_data_sectors(d); r[3]=(uint8_t)(lf_data_sectors(d)>>8); r[4]=d->state;
        lf_p32(r+5,d->pack_size); lf_p32(r+9,d->pack_crc); lf_p32(r+13,d->gen); r[17]=(uint8_t)d->next; r[18]=(uint8_t)(d->next>>8);
        return lf_reply(out,cmd,seq,r,19);
    case LF_BEGIN: { if(pn != 8) return 0; uint32_t size=lf_g32(p), crc=lf_g32(p+4); r[0]=LF_OK;
        if(size == 0 || size > lf_data_sectors(d)*LF_SECTOR){ r[0]=LF_NOSPACE; return lf_reply(out,cmd,seq,r,1); }
        if(!(d->state==LF_UPLOADING && d->pack_size==size && d->pack_crc==crc)){
            if(lf_write_record(d,LF_EMPTY,0,0)){ r[0]=LF_IO; return lf_reply(out,cmd,seq,r,1); }
            d->state=LF_UPLOADING; d->pack_size=size; d->pack_crc=crc; d->next=0; d->mask=0; d->total=(uint16_t)((size+LF_SECTOR-1)/LF_SECTOR); }
        r[1]=(uint8_t)d->next; r[2]=(uint8_t)(d->next>>8); r[3]=(uint8_t)d->total; r[4]=(uint8_t)(d->total>>8); return lf_reply(out,cmd,seq,r,5); }
    case LF_DATA: { if(pn != 3+LF_BLOCK+2 || d->state != LF_UPLOADING) return 0;
        unsigned sec=p[0]|p[1]<<8, blk=p[2]; if(sec != d->next || sec >= d->total || blk >= lf_blocks_in(d,sec)) return 0;
        if(lf_crc16(p,3+LF_BLOCK) != (p[3+LF_BLOCK] | p[4+LF_BLOCK]<<8)) return 0;
        memcpy(d->secbuf+blk*LF_BLOCK,p+3,LF_BLOCK); d->mask |= 1u<<blk;
        if(d->mask == lf_full_mask(lf_blocks_in(d,sec))){ uint32_t off=(2+sec)*LF_SECTOR;
            if(d->fl.erase(d->fl.ctx,off) || d->fl.write(d->fl.ctx,off,d->secbuf,LF_SECTOR)){ d->state=LF_EMPTY; d->pack_size=d->pack_crc=0; return 0; }
            d->next++; d->mask=0; }
        return 0; }
    case LF_QUERY: { if(pn != 2) return 0; unsigned sec=p[0]|p[1]<<8; r[0]=LF_OK; r[1]=p[0]; r[2]=p[1];
        if(d->state != LF_UPLOADING){ r[0]=LF_STATE; return lf_reply(out,cmd,seq,r,1); }
        if(sec < d->next){ r[3]=1; lf_p32(r+4,0xffffffffu); } else if(sec == d->next){ r[3]=0; lf_p32(r+4,d->mask); } else { r[0]=LF_BADARG; return lf_reply(out,cmd,seq,r,1); }
        return lf_reply(out,cmd,seq,r,8); }
    case LF_END: { r[0]=LF_OK; if(pn != 4) return 0;
        if(d->state==LF_PACK && lf_g32(p)==d->pack_crc) return lf_reply(out,cmd,seq,r,1);   /* retry of a committed END */
        if(lf_g32(p)!=d->pack_crc && d->state==LF_UPLOADING){ r[0]=LF_BADARG; return lf_reply(out,cmd,seq,r,1); }
        if(d->state != LF_UPLOADING || d->next != d->total){ r[0]=LF_STATE; return lf_reply(out,cmd,seq,r,1); }
        uint32_t crc=0; uint8_t buf[256]; for(uint32_t o=0;o<d->pack_size;){ size_t k = d->pack_size-o > sizeof buf ? sizeof buf : d->pack_size-o;
            if(d->fl.read(d->fl.ctx,2*LF_SECTOR+o,buf,k)){ r[0]=LF_IO; return lf_reply(out,cmd,seq,r,1); } crc=lf_crc32(crc,buf,k); o+=k; }
        if(crc != d->pack_crc){ r[0]=LF_CRC; return lf_reply(out,cmd,seq,r,1); }
        if(lf_write_record(d,LF_PACK,d->pack_size,d->pack_crc)){ r[0]=LF_IO; return lf_reply(out,cmd,seq,r,1); }
        d->state=LF_PACK; return lf_reply(out,cmd,seq,r,1); }
    case LF_ABORT: case LF_DELETE: r[0]=LF_OK;
        if(cmd==LF_DELETE || d->state==LF_UPLOADING){ if(lf_write_record(d,LF_EMPTY,0,0)) r[0]=LF_IO; d->state=LF_EMPTY; d->pack_size=d->pack_crc=0; d->next=d->total=0; d->mask=0; }
        return lf_reply(out,cmd,seq,r,1);
    }
    return 0; }
#endif
