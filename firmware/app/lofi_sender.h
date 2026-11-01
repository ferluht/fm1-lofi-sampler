/* Reference sender (the phone app implements the same logic). link(ctx, msg, n, reply, &reply_n) delivers one frame,
 * returns 0 if the link answered (reply_n may be 0 for DATA) or -1 on timeout/loss. */
#ifndef LOFI_SENDER_H
#define LOFI_SENDER_H
#include "lofi_proto.h"
typedef int (*lf_link_fn)(void *ctx, const uint8_t *msg, size_t n, uint8_t *reply, size_t *reply_n);
typedef struct { lf_link_fn link; void *ctx; unsigned seq; unsigned retries, sent_blocks, resent_blocks; } lf_sender_t;
/* request/reply with retries; returns payload length or -1 */
static int lf_request(lf_sender_t *s, unsigned cmd, const uint8_t *pl, size_t pn, uint8_t *rp){
    uint8_t m[LF_MAXMSG], r[LF_MAXMSG]; unsigned seq = ++s->seq & 0x3fff; size_t mn = lf_frame(m,cmd,seq,pl,pn);
    for(int attempt=0; attempt<12; attempt++){ size_t rn=0; if(attempt) s->retries++;
        if(s->link(s->ctx,m,mn,r,&rn)) continue; unsigned rc, rs; int k = rn ? lf_parse(r,rn,&rc,&rs,rp) : -1;
        if(k >= 0 && rc == (cmd|0x40) && rs == seq) return k; }
    return -1; }
/* uploads the image; returns 0 on success, negative on failure */
static int lf_send_pack(lf_sender_t *s, const uint8_t *img, uint32_t size){
    uint8_t p[LF_MAXMSG], b[3+LF_BLOCK+2]; uint32_t crc = lf_crc32(0,img,size); int k;
    k = lf_request(s,LF_HELLO,NULL,0,p); if(k < 19 || p[0]) return -1;
    lf_p32(b,size); lf_p32(b+4,crc); k = lf_request(s,LF_BEGIN,b,8,p); if(k < 5 || p[0]) return -2;
    unsigned next = p[1]|p[2]<<8, total = p[3]|p[4]<<8;
    for(unsigned sec=next; sec<total; sec++){
        unsigned blocks = ((size - sec*LF_SECTOR > LF_SECTOR ? LF_SECTOR : size - sec*LF_SECTOR) + LF_BLOCK-1)/LF_BLOCK;
        uint32_t want = lf_full_mask(blocks), have = 0; int done = 0;
        for(int round=0; round<40 && !done; round++){
            for(unsigned blk=0; blk<blocks; blk++){ if(have>>blk & 1) continue;
                uint8_t m[LF_MAXMSG], r[LF_MAXMSG]; size_t rn;
                b[0]=(uint8_t)sec; b[1]=(uint8_t)(sec>>8); b[2]=(uint8_t)blk;
                uint32_t off = sec*LF_SECTOR + blk*LF_BLOCK; memset(b+3,0,LF_BLOCK);
                memcpy(b+3, img+off, size-off > LF_BLOCK ? LF_BLOCK : size-off);
                uint16_t c = lf_crc16(b,3+LF_BLOCK); b[3+LF_BLOCK]=(uint8_t)c; b[4+LF_BLOCK]=(uint8_t)(c>>8);
                size_t mn = lf_frame(m,LF_DATA,0,b,sizeof b); s->link(s->ctx,m,mn,r,&rn);
                s->sent_blocks++; if(round) s->resent_blocks++; }
            uint8_t q[2] = {(uint8_t)sec,(uint8_t)(sec>>8)}; k = lf_request(s,LF_QUERY,q,2,p);
            if(k < 8 || p[0]) return -3;
            if(p[3]) done = 1; else have = lf_g32(p+4) & want; }
        if(!done) return -4; }
    lf_p32(b,crc); k = lf_request(s,LF_END,b,4,p); return (k >= 1 && p[0]==0) ? 0 : -5; }
#endif
