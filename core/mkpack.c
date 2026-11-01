/* mkpack out.lfp name file.raw[:lo:hi:root:flags[:loop_start:loop_end[:tune_cents]]] ...   (16 kHz mono s16le raw files) */
#include <stdio.h>
#include "lofi_pack.h"
int main(int c,char**v){ lfp_input_t in[32]; unsigned n=0; static char names[32][16];
  for(int i=3;i<c&&n<32;i++){ char path[256]; unsigned lo=36+n,hi=36+n,root=36+n,fl=4,ls=0,le=LFP_NO_LOOP; int tu=0; sscanf(v[i],"%255[^:]:%u:%u:%u:%u:%u:%u:%d",path,&lo,&hi,&root,&fl,&ls,&le,&tu);
    FILE*f=fopen(path,"rb"); if(!f){perror(path);return 2;} fseek(f,0,SEEK_END); unsigned fr=ftell(f)/2; fseek(f,0,SEEK_SET); int16_t*x=malloc(fr*2+2); if(fread(x,2,fr,f)!=fr) return 2; fclose(f);
    const char*b=strrchr(path,'/'); snprintf(names[n],16,"%s",b?b+1:path); char*d=strchr(names[n],'.'); if(d)*d=0;
    in[n]=(lfp_input_t){names[n],x,fr,ls,le,(uint8_t)lo,(uint8_t)hi,(uint8_t)root,(uint8_t)fl,0,0,0,(int8_t)tu}; n++; }
  uint32_t bytes; uint8_t*img=lfp_build_ex(v[2],getenv("LFP_SCALE")?atoi(getenv("LFP_SCALE")):0,getenv("LFP_ROOT")?atoi(getenv("LFP_ROOT")):0,in,n,&bytes); if(!img) return 3; FILE*o=fopen(v[1],"wb"); fwrite(img,1,bytes,o); fclose(o);
  printf("%s: %u slots, %u bytes\n",v[1],n,bytes); return 0; }
