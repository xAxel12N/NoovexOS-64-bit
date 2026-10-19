#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
void uefi_mark(int idx,unsigned col);
int main(void){
  uint8_t* lo=mmap((void*)0x5000,0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
  if(lo!=(uint8_t*)0x5000){ printf("cannot map 0x5000\n"); return 2; }
  uint32_t W=1366,H=768,pitch=(1366+10)*4; static uint32_t fb[(1366+10)*768];
  *(uint64_t*)0x5000=(uint64_t)fb; *(uint32_t*)0x5008=pitch; *(uint16_t*)0x500C=W; *(uint16_t*)0x500E=H;
  for(int k=2;k<5;k++)uefi_mark(k,0x100+k);
  int ok=1; uint32_t st=pitch/4;
  for(int k=2;k<5;k++){ int x0=16+k*28, y0=H-36;
    for(int y=y0-1;y<=y0+24;y++) for(int x=x0-1;x<=x0+24;x++){ uint32_t v=fb[y*st+x]; int in=(x>=x0&&x<x0+24&&y>=y0&&y<y0+24); if(in&&v!=0x100u+k)ok=0; if(!in&&v==0x100u+k)ok=0; } }
  printf("  real uefi_mark: three 24x24 squares exactly in place on a 1366x768 screen with padded pitch: %s\n",ok?"ok":"FAIL");
  *(uint64_t*)0x5000=0; uefi_mark(2,0xFF); printf("  no framebuffer -> draws nothing, no crash: ok\n");
  return !ok; }
