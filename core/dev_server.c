/* Device side of the upload protocol as a stdin/stdout process, for the Java sender test.
 * stdin: one hex SysEx per line. stdout: the reply as hex (empty line = no reply). "dump" prints crc32 of the pack in flash, "reboot" re-reads the flash. */
#include <stdio.h>
#include <stdlib.h>
#include "lofi_proto.h"
#define FLASH_BYTES 536576u
static uint8_t mem[FLASH_BYTES];
static int f_read(void*c,uint32_t o,void*p,size_t n){ (void)c; if(o+n>FLASH_BYTES) return -1; memcpy(p,mem+o,n); return 0; }
static int f_erase(void*c,uint32_t o){ (void)c; memset(mem+o,0xff,LF_SECTOR); return 0; }
static int f_write(void*c,uint32_t o,const void*p,size_t n){ (void)c; const uint8_t*s=p; for(size_t i=0;i<n;i++){ if((mem[o+i]&s[i])!=s[i]){ fprintf(stderr,"flash write over unerased data\n"); exit(9);} mem[o+i]&=s[i]; } return 0; }
static lf_dev_t dev;
int main(void){ memset(mem,0xff,sizeof mem); dev.fl=(lf_flash_t){0,FLASH_BYTES,f_read,f_write,f_erase}; lf_dev_init(&dev);
  static char line[1024]; uint8_t m[LF_MAXMSG], r[LF_MAXMSG];
  while(fgets(line,sizeof line,stdin)){
    if(!strncmp(line,"dump",4)){ printf("state=%d size=%u crc=%08x\n",dev.state,dev.pack_size,dev.state==LF_PACK?lf_crc32(0,mem+2*LF_SECTOR,dev.pack_size):0); fflush(stdout); continue; }
    if(!strncmp(line,"reboot",6)){ lf_dev_init(&dev); puts("ok"); fflush(stdout); continue; }
    size_t n=0; for(char*p=line;p[0]&&p[1]&&p[0]!='\n'&&n<sizeof m;p+=2){ unsigned v; sscanf(p,"%2x",&v); m[n++]=(uint8_t)v; }
    size_t k=lf_dev_handle(&dev,m,n,r); for(size_t i=0;i<k;i++) printf("%02x",r[i]); printf("\n"); fflush(stdout); }
  return 0; }
