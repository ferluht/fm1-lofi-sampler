/* Prototype lo-fi codec test: SEA-style LMS-ADPCM with variable bits.
 * usage: lofi_codec in.raw16 bits out.raw16   (mono s16le)  -> prints bytes, SNR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#define FR 320
#define GR 20
static int32_t sat(int32_t x){return x>32767?32767:x<-32768?-32768:x;}
static int16_t dqt[16*64];
static void make_dqt(int bits){
  int L=1<<bits;
  for(int s=0;s<16;s++){ double w=2.0*pow(1.75,s);
    for(int k=0;k<L/2;k++){ int v=(int)lround((2*k+1)*w/2); if(v<1)v=1; dqt[(s<<bits)+2*k]=v; dqt[(s<<bits)+2*k+1]=-v; } }
}
static int32_t pred(const int32_t*st){int64_t s=0;for(int i=0;i<4;i++)s+=(int64_t)st[i]*st[4+i];return (int32_t)(s>>13);}
static void upd(int32_t*st,int32_t pcm,int32_t res){int32_t d=res>>4;for(int i=0;i<4;i++)st[4+i]=sat(st[4+i]+(st[i]<0?-d:d));for(int i=0;i<3;i++)st[i]=st[i+1];st[3]=pcm;}
static uint64_t pen(const int32_t*st){int64_t s=0;for(int i=4;i<8;i++)s+=(int64_t)st[i]*st[i];int64_t v=(s>>18)-0x8ff;return v>0?(uint64_t)(v*v):0;}
int main(int c,char**v){
  FILE*f=fopen(v[1],"rb");int bits=atoi(v[2]);fseek(f,0,SEEK_END);long n=ftell(f)/2;fseek(f,0,SEEK_SET);
  int16_t*in=calloc(n+FR,2),*out=calloc(n+FR,2);fread(in,2,n,f);fclose(f);
  make_dqt(bits);int L=1<<bits;
  int32_t st[8]={0,0,0,0,0,0,-8192,16384};int prev=0;long blocks=(n+FR-1)/FR;
  long bytes=blocks*(24+FR*bits/8);
  for(long b=0;b<blocks;b++){
    for(int g=0;g<FR/GR;g++){
      uint64_t best=UINT64_MAX;int32_t bs[8];int16_t bo[GR];int bsf=prev;
      for(int sr=0;sr<16;sr++){int sf=(sr+prev)&15;int32_t t[8];memcpy(t,st,sizeof t);uint64_t rank=0;int16_t o[GR];int i;
        for(i=0;i<GR;i++){long p=b*FR+g*GR+i;int32_t pcm=p<n?in[p]:0;int32_t pr=pred(t);
          int bq=0;int32_t be=INT32_MAX;for(int q=0;q<L;q++){int32_t r=dqt[(sf<<bits)+q];int32_t e=pcm-sat(pr+r);if(abs(e)<be){be=abs(e);bq=q;}}
          int32_t res=dqt[(sf<<bits)+bq];int32_t rec=sat(pr+res);int64_t er=pcm-rec;rank+=(uint64_t)(er*er)+pen(t);
          if(rank>best)break;o[i]=rec;upd(t,rec,res);}
        if(i==GR&&rank<best){best=rank;memcpy(bs,t,sizeof t);memcpy(bo,o,sizeof o);bsf=sf;}}
      prev=bsf;memcpy(st,bs,sizeof st);for(int i=0;i<GR;i++)out[b*FR+g*GR+i]=bo[i];
    }
  }
  double se=0,sn=0;for(long i=0;i<n;i++){double d=in[i]-out[i];se+=d*d;sn+=(double)in[i]*in[i];}
  FILE*o=fopen(v[3],"wb");fwrite(out,2,n,o);fclose(o);
  printf("bits=%d frames=%ld bytes=%ld (%.2f KB/s @16k) SNR=%.1f dB\n",bits,n,bytes,bytes/(n/16000.0)/1000,10*log10(sn/(se+1e-9)));
}
