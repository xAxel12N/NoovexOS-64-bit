#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define AMD_FW_HOST
#include "drivers/video/amd/fw/amd_fw.c"

static const char* root;
static int rd(const char* path,u8** out,u32* len){
    char p[512]; snprintf(p,sizeof p,"%s/%s",root,path+strlen("/lib/firmware/"));
    FILE* f=fopen(p,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    u8* b=malloc((size_t)n); if(fread(b,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(b); return -1; } fclose(f); *out=b; *len=(u32)n; return 0; }
int (*amd_fw_read)(const char* path,u8** out,u32* len)=rd;

int main(int argc,char** argv){
    root=argc>1?argv[1]:"assets/firmware/linux-firmware"; int bad=0; char s[160];
    static const struct { u32 v; u16 did; int want; } c[]={{AFV(2,1,0),0x1636,0},{AFV(2,1,0),0x1638,0},{AFV(2,1,0),0x15E7,0},{AFV(3,1,2),0x1681,0},{AFV(3,1,4),0x15BF,0},{AFV(3,1,5),0x1506,0},{AFV(3,1,6),0x164E,-1},{AFV(9,9,9),0,-1}};
    for(unsigned i=0;i<sizeof c/sizeof c[0];i++){ int r=amd_fw_dmcub_load(c[i].v,c[i].did); amd_fw_describe(s,sizeof s); printf("%06x %04x -> %d %s\n",c[i].v,c[i].did,r,s); if(r!=c[i].want)bad++; }
    u8* b=0; u32 n=0; rd("/lib/firmware/amdgpu/renoir_dmcub.bin",&b,&n);
    for(u32 cut=0;cut<n&&cut<4096;cut+=7){ amd_dmcub_t f; u8* t=malloc(cut?cut:1); memcpy(t,b,cut); if(amd_fw_dmcub_parse(&f,t,cut)==0)bad++; free(t); }
    for(int k=0;k<40;k++){ u8* t=malloc(n); memcpy(t,b,n); t[k]^=0xFF; amd_dmcub_t f; if(amd_fw_dmcub_parse(&f,t,n)==0&&f.ok){ if(f.ucode_off+f.ucode_len>n||f.inst_const+f.bss>f.ucode_len)bad++; } free(t); }
    for(int k=0;k<20000;k++){ u32 m=40+(u32)(rand()%300); u8* t=malloc(m); for(u32 j=0;j<m;j++)t[j]=(u8)rand(); if(rand()&1){ t[0]=(u8)m; t[1]=(u8)(m>>8); t[2]=t[3]=0; t[8]=1; t[9]=0; } amd_dmcub_t f; if(amd_fw_dmcub_parse(&f,t,m)==0&&(f.ucode_off+f.ucode_len>m||f.inst_const+f.bss>f.ucode_len))bad++; free(t); }
    free(b); amd_fw_dmcub_free();
    printf(bad?"FAIL %d\n":"PASS\n",bad); return bad!=0; }
