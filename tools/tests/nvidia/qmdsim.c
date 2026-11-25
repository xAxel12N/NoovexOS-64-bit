#include <stdio.h>
#include <string.h>
#include "include/nvx/base.h"
#include "include/nvx/drivers/video/nvidia/nvqmd.h"
#include "include/nvx/drivers/video/nvidia/gqk_cubin.h"
#include "include/nvx/drivers/video/nvidia/gqk_cubin_sm8x.h"

static int fails=0;
static void chk(const char* w,int c){ printf("  %-100s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static u32 get(const u32* q,u32 lo,u32 hi){ u32 v=0; for(u32 b=hi+1;b-->lo;)v=(v<<1)|((q[b>>5]>>(b&31))&1u); return v; }
static void legacy(u32* q,u32 gx,u32 gy,u32 regs,u32 bars,const u64* cba,const u32* cbl,u64 prog){
    for(int i=0;i<64;i++)q[i]=0;
    nvqmd_set(q,134,134,1); nvqmd_set(q,189,189,1); nvqmd_set(q,191,191,1); nvqmd_set(q,366,366,1); nvqmd_set(q,368,369,1); nvqmd_set(q,378,378,1);
    nvqmd_set(q,384,415,gx); nvqmd_set(q,416,431,gy); nvqmd_set(q,448,463,1);
    nvqmd_set(q,544,561,0); nvqmd_set(q,562,568,3); nvqmd_set(q,569,575,17); nvqmd_set(q,657,663,3);
    nvqmd_set(q,576,579,2); nvqmd_set(q,580,583,2);
    nvqmd_set(q,592,607,32); nvqmd_set(q,608,623,4); nvqmd_set(q,624,639,1);
    nvqmd_set(q,648,656,regs); nvqmd_set(q,955,959,bars);
    for(int b=0;b<8;b++){ u64 a=cba[b]; u32 l=cbl[b]; if(!a||!l)continue; u32 base=1024u+(u32)b*64u;
        nvqmd_set(q,640u+(u32)b,640u+(u32)b,1); nvqmd_set(q,base,base+31,(u32)a); nvqmd_set(q,base+32,base+48,(u32)(a>>32)); nvqmd_set(q,base+51,base+63,(l+15u)>>4); }
    nvqmd_set(q,1536,1567,(u32)prog); nvqmd_set(q,1568,1584,(u32)(prog>>32)); }

static u32 lu32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u64 lu64(const u8* p){ return (u64)lu32(p)|((u64)lu32(p+4)<<32); }
static int text_of(const u8* f,u64* o,u64* l){
    u64 sho=lu64(f+0x28); u32 shn=f[0x3C]|(f[0x3D]<<8), sst=f[0x3E]|(f[0x3F]<<8); const u8* ss=f+sho+(u64)sst*64; const char* nm=(const char*)f+lu64(ss+24);
    for(u32 i=0;i<shn;i++){ const u8* h=f+sho+(u64)i*64; if(!strcmp(nm+lu32(h),".text.gqk")){ *o=lu64(h+24); *l=lu64(h+32); return 1; } } return 0; }
static u32 bits(const u8* in,u32 lo,u32 n){ u32 v=0; for(u32 i=0;i<n;i++)v|=(u32)((in[(lo+i)>>3]>>((lo+i)&7))&1u)<<i; return v; }

static void port_check(const unsigned char* p,u32 n,u32 sm,const char* what){
    char w[160]; int same_len=n==g_gqk_cubin_len, flags=(lu32(p+0x30)&0xFF00FFu)==(sm|(sm<<16))&&((lu32(p+0x30)>>8)&0xFFu)==((lu32(g_gqk_cubin+0x30)>>8)&0xFFu);
    u64 o=0,l=0,o2=0,l2=0; int t=text_of(p,&o,&l)&&text_of(g_gqk_cubin,&o2,&l2)&&o==o2&&l==l2;
    int outside=0, mem=0, membad=0, stall=1, other=0, brx=0, brxp=0, imad=0; u64 bi=0;
    for(u32 i=0;i<n&&same_len;i++){ if(i>=0x30&&i<0x34)continue; if(t&&i>=o&&i<o+l)continue; if(p[i]!=g_gqk_cubin[i])outside++; }
    for(u64 i=0;t&&i<l;i+=16)if(bits(g_gqk_cubin+o+i,0,12)==0x949){ brx++; bi=i/16; }
    for(u64 i=0;t&&i<l;i+=16){ const u8* a=g_gqk_cubin+o+i; const u8* b=p+o+i; u32 op=bits(a,0,12); if(bits(b,0,12)==0x949)brxp++;
        if(bits(b,105,4)<15)stall=0;
        int carry=(bits(a,0,9)==0x024||bits(a,0,9)==0x025)&&bits(a,87,4)==15; if(carry&&bits(b,87,4)==0)imad++;
        if(i/16+6>=bi&&i/16<=bi&&brx)continue;
        for(u32 k=0;k<128;k++){ if(k>=105&&k<109)continue; if((op==0x381||op==0x386)&&k>=77&&k<81)continue; if(carry&&k>=87&&k<91)continue; if(bits(a,k,1)!=bits(b,k,1))other++; }
        if(op==0x381||op==0x386){ mem++; u32 o75=bits(a,79,2), m=bits(b,77,4); if(!(o75==1&&m==0))membad++; } }
    snprintf(w,sizeof w,"%s: same size as sm_75, e_flags sm_%u, everything outside .text and e_flags unchanged",what,sm); chk(w,same_len&&flags&&t&&!outside);
    snprintf(w,sizeof w,"%s: 15 LDG/STG re-encoded weak sm_75 (order 1) -> sm_80 weak (0x0) at bits 77..80 (NAK set_mem_order)",what); chk(w,mem==15&&!membad);
    snprintf(w,sizeof w,"%s: outside the switch no other instruction bit changed, every stall count raised to 15",what); chk(w,!other&&stall);
    snprintf(w,sizeof w,"%s: IMAD/IMAD.WIDE unused carry field 87..90 = 0 as NAK writes it (%d)",what,imad); chk(w,imad>0);
    const u8* s=p+o+(bi-6)*16; u32 t1=(u32)(bi-3)+1+(bits(s+48,34,32)>>2), t2=(u32)(bi-1)+1+(bits(s+80,34,32)>>2);
    snprintf(w,sizeof w,"%s: BRX@%u replaced: ISETP.GT.U32 k,2 then ISETP.EQ.U32 k,0 / BRA %u, ISETP.EQ.U32 k,1 / BRA %u, NOP",what,(u32)bi,t1,t2);
    chk(w,brx==1&&!brxp&&bits(s,0,12)==0x80c&&bits(s,73,1)==0&&bits(s,76,3)==4&&bits(s+32,0,12)==0x80c&&bits(s+32,76,3)==2&&bits(s+32,32,32)==0&&bits(s+48,0,12)==0x947&&bits(s+64,32,32)==1&&bits(s+80,0,12)==0x947&&bits(s+96,0,12)==0x918&&t1==73&&t2==75);
    snprintf(w,sizeof w,"%s: no BRX left, every instruction NAK-checked, so the port is marked verified",what); chk(w,!brxp&&g_gqk_sm8x_verified==1); }

int main(void){
    printf("QMD layouts and compositor shader selection per GPU generation\n");
    chk("Turing sm_75 + QMD 2 picks the sm_75 cubin",nvqmd_pick(75,2,0,0)==75);
    chk("Ampere sm_86 + QMD 3 refused while the port is unverified and not enabled",nvqmd_pick(86,3,0,0)==NVQMD_UNVERIFIED);
    chk("Ada sm_89 + QMD 3 refused while the port is unverified and not enabled",nvqmd_pick(89,3,0,0)==NVQMD_UNVERIFIED);
    chk("Ampere picks sm_86 and Ada sm_89 when enabled or verified",nvqmd_pick(86,3,0,1)==86&&nvqmd_pick(89,3,0,1)==89&&nvqmd_pick(86,3,1,0)==86);
    chk("mismatched QMD versions refused (sm_75/QMD 3, sm_86/QMD 2)",nvqmd_pick(75,3,1,1)==NVQMD_BAD_QMD&&nvqmd_pick(86,2,1,1)==NVQMD_BAD_QMD);
    chk("unknown generations refused (none chosen, sm_90)",nvqmd_pick(0,0,1,1)==NVQMD_NO_SHADER&&nvqmd_pick(90,3,1,1)==NVQMD_NO_SHADER);
    chk("refusal reasons fit the 80-byte reason buffer",strlen(nvqmd_why(NVQMD_UNVERIFIED))<200&&nvqmd_why(NVQMD_BAD_QMD)[0]&&nvqmd_why(NVQMD_NO_SHADER)[0]);
    u64 cba[8]={0x123456789A00ULL,0,0x1FFFF00001000ULL,0,0,0,0,0}; u32 cbl[8]={0x2E8,0,16,0,0,0,0,0}; u64 prog=0x1ABCDE0000ULL;
    u32 a[64], b[64]; legacy(a,60,270,48,0,cba,cbl,prog); int r2=nvqmd_fill(b,2,60,270,48,0,cba,cbl,prog);
    chk("QMD 2.2 writer is bit-identical to the previous Turing launch descriptor",r2&&!memcmp(a,b,sizeof a));
    legacy(a,7,9,32,1,cba,cbl,prog); nvqmd_fill(b,2,7,9,32,1,cba,cbl,prog); chk("QMD 2.2 with one barrier: BARRIER_COUNT at 959:955, identical",!memcmp(a,b,sizeof a)&&get(b,955,959)==1);
    u32 q[64]; int r3=nvqmd_fill(q,3,60,270,48,1,cba,cbl,prog);
    chk("QMD 3.0: QMD_MAJOR_VERSION 583:580 = 3, QMD_VERSION 579:576 = 0",r3&&get(q,580,583)==3&&get(q,576,579)==0);
    chk("QMD 3.0: BARRIER_COUNT moved to 767:763, old 959:955 left zero",get(q,763,767)==1&&get(q,955,959)==0);
    chk("QMD 3.0: bit 366 (RESERVED11A, was RELEASE_MEMBAR_TYPE) zero, CWD_MEMBAR_TYPE 369:368 = 1",get(q,366,366)==0&&get(q,368,369)==1);
    chk("QMD 3.0: SM_GLOBAL_CACHING 134, INVALIDATE_SHADER_DATA 189 / CONSTANT 191, API_VISIBLE_CALL_LIMIT 378",get(q,134,134)&&get(q,189,189)&&get(q,191,191)&&get(q,378,378));
    chk("QMD 3.0: CTA_RASTER 415:384/431:416/463:448, CTA_THREAD_DIMENSION 607:592/623:608/639:624",get(q,384,415)==60&&get(q,416,431)==270&&get(q,448,463)==1&&get(q,592,607)==32&&get(q,608,623)==4&&get(q,624,639)==1);
    chk("QMD 3.0: 6-bit MIN/MAX/TARGET_SM_CONFIG_SHARED_MEM_SIZE 567:562/574:569/662:657, SHARED_ALLOCATION_ENABLE 663 zero",get(q,562,567)==3&&get(q,569,574)==17&&get(q,657,662)==3&&!get(q,568,568)&&!get(q,575,575)&&!get(q,663,663));
    chk("QMD 3.0: REGISTER_COUNT_V 656:648, PROGRAM_ADDRESS 1567:1536/1584:1568",get(q,648,656)==48&&get(q,1536,1567)==(u32)prog&&get(q,1568,1584)==(u32)(prog>>32));
    chk("QMD 3.0: constant buffers 0 and 2 valid (640+i), ADDR_LOWER/UPPER and SIZE_SHIFTED4 at 1024+64i",get(q,640,640)&&!get(q,641,641)&&get(q,642,642)&&get(q,1024,1055)==(u32)cba[0]&&get(q,1056,1072)==(u32)(cba[0]>>32)&&get(q,1075,1087)==(0x2E8+15)/16&&get(q,1152,1183)==(u32)cba[2]&&get(q,1184,1200)==(u32)(cba[2]>>32)&&get(q,1203,1215)==1);
    chk("QMD 3.0: SASS_VERSION 1663:1656 and QMD 2.2 SASS_VERSION 1023:1016 left zero",!get(q,1656,1663)&&!get(q,1016,1023));
    chk("unknown QMD major refused",!nvqmd_fill(q,4,1,1,8,0,cba,cbl,prog)&&!nvqmd_fill(q,1,1,1,8,0,cba,cbl,prog));
    port_check(g_gqk_cubin_sm86,g_gqk_cubin_sm86_len,86,"sm_86 port");
    port_check(g_gqk_cubin_sm89,g_gqk_cubin_sm89_len,89,"sm_89 port");
    printf(fails?"%d FAILED\n":"ALL PASS\n",fails); return fails?1:0; }
