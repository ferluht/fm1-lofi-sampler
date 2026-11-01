#include <stdio.h>
#include <math.h>
#include "lofi_codec.h"
int main(int c,char**v){ FILE*f=fopen(v[1],"rb"); fseek(f,0,SEEK_END); unsigned n=ftell(f)/2; fseek(f,0,SEEK_SET);
  int16_t*in=calloc(n+LOFI_SUPER,2),*rec=calloc(n+LOFI_SUPER,2),*dec=calloc(n+LOFI_SUPER,2),*rnd=calloc(LOFI_SUPER,2);
  if(fread(in,2,n,f)!=n) return 2; fclose(f);
  uint8_t*e=lofi_encode(in,n,rec); unsigned nsb=(n+LOFI_SUPER-1)/LOFI_SUPER;
  for(unsigned b=0;b<nsb;b++) lofi_decode_super(e+b*LOFI_SUPER_BYTES,dec+b*LOFI_SUPER);
  unsigned bad=0; for(unsigned i=0;i<n;i++) if(dec[i]!=rec[i]) bad++;
  /* random access: decode superblocks in reverse order, must match */
  unsigned bad2=0; for(int b=nsb-1;b>=0;b--){ lofi_decode_super(e+b*LOFI_SUPER_BYTES,rnd); for(unsigned i=0;i<LOFI_SUPER&&b*LOFI_SUPER+i<n;i++) if(rnd[i]!=rec[b*LOFI_SUPER+i]) bad2++; }
  double se=0,sn=0; for(unsigned i=0;i<n;i++){double d=in[i]-dec[i];se+=d*d;sn+=(double)in[i]*in[i];}
  printf("%s: %u frames -> %u bytes (%.2f KB/s) SNR %.1f dB, enc/dec mismatches %u, random-access mismatches %u\n",v[1],n,lofi_encoded_bytes(n),lofi_encoded_bytes(n)/(n/16000.0)/1000,10*log10(sn/(se+1e-9)),bad,bad2);
  if(c>2){ FILE*o=fopen(v[2],"wb"); fwrite(dec,2,n,o); fclose(o);} return bad||bad2; }
