#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#include "BLT12_FNS"
static int fails=0, checks=0;
static void ck(const char* n,int ok){ checks++; printf("%s %s\n",n,ok?"ok":"FAIL"); if(!ok)fails++; }
int main(void){
    u32 r[64]; memset(r,0xAA,sizeof r);
    u32 n=blt12_fill(r,3,0x123456789000ULL,256,4,5,68,69,0xFF3366CCu);
    ck("fill length 11 dwords",n==14&&((r[3]&0xFF)+2)==11);
    ck("fill opcode XY_FAST_COLOR_BLT 2D client",(r[3]>>29)==2&&((r[3]>>22)&0x7F)==0x44);
    ck("fill 32bpp depth field",((r[3]>>19)&7)==2);
    ck("fill pitch minus one, MOCS 0",r[4]==255);
    ck("fill top-left",r[5]==((5u<<16)|4u)); ck("fill bottom-right exclusive",r[6]==((69u<<16)|68u));
    ck("fill address split",r[7]==0x56789000u&&r[8]==0x1234u);
    ck("fill system-memory type bit",r[9]==0x80000000u);
    ck("fill colour in DW7, upper colour dwords zero",r[10]==0xFF3366CCu&&r[11]==0&&r[12]==0&&r[13]==0);
    ck("fill writes nothing past its length",r[14]==0xAAAAAAAAu&&r[2]==0xAAAAAAAAu);
    memset(r,0xAA,sizeof r);
    n=blt12_copy(r,0,0x200000ULL,7680,10,20,1920,1080,0x100000000ULL,8192,1,2);
    ck("copy length 10 dwords",n==10&&((r[0]&0xFF)+2)==10);
    ck("copy opcode XY_FAST_COPY_BLT linear",(r[0]>>29)==2&&((r[0]>>22)&0x7F)==0x42&&((r[0]>>20)&3)==0&&((r[0]>>13)&3)==0);
    ck("copy 32bpp and destination pitch in bytes",r[1]==((3u<<24)|7680u));
    ck("copy destination rectangle",r[2]==((20u<<16)|10u)&&r[3]==((1100u<<16)|1930u));
    ck("copy destination address",r[4]==0x200000u&&r[5]==0);
    ck("copy source origin and pitch",r[6]==((2u<<16)|1u)&&r[7]==8192u);
    ck("copy source address",r[8]==0&&r[9]==1u&&r[10]==0xAAAAAAAAu);
    u64 d=g12_desc(0x00345000ULL);
    ck("descriptor valid, privileged, 4-level addressing",(d&1)&&((d>>3)&3)==3&&((d>>8)&1));
    ck("descriptor context address",(d&0xFFFFF000ULL)==0x00345000ULL);
    ck("descriptor sw context id 1 (bits 37-47)",((d>>37)&0x7FF)==1);
    ck("descriptor engine class copy, instance 0",((d>>61)&7)==3&&((d>>48)&0x3F)==0);
    ck("descriptor no force-restore bit",((d>>2)&1)==0);
    u32 st[0x40]; memset(st,0,sizeof st); u32 B=0x22000;
    g12_ctx_regs(st,B,0x7000,4,0x00000001ABCDE000ULL);
    ck("context LRI 13 posted with CS MMIO remap",st[1]==((0x22u<<23)|(1u<<19)|(1u<<12)|25u));
    static const u32 off[13]={0x244,0x34,0x30,0x38,0x3C,0x168,0x140,0x110,0x1C0,0x1C4,0x1C8,0x180,0x2B4}; int ok=1;
    for(int k=0;k<13;k++) if(st[2+k*2]!=B+off[k])ok=0;
    ck("context register order matches gen12 xcs image",ok);
    ck("context control restore inhibit on first run",st[3]==0x00090009u);
    ck("ring head/tail/start/ctl slots",st[4]==B+0x34&&st[5]==0&&st[6]==B+0x30&&st[7]==0&&st[8]==B+0x38&&st[9]==0x7000&&st[0xA]==B+0x3C&&st[0xB]==((3u*4096u)|1u));
    ck("batch state ppgtt bit",st[0x10]==B+0x110&&st[0x11]==0x20);
    ck("second LRI 9 posted",st[0x21]==((0x22u<<23)|(1u<<19)|(1u<<12)|17u)&&st[0x22]==B+0x3A8);
    ok=1; for(int k=0;k<8;k++){ u32 want=B+0x28C-(u32)k*4; if(st[0x24+k*2]!=want)ok=0; }
    ck("PDP register order",ok);
    ck("PML4 in PDP0, others zero",st[0x31]==0x00000001u&&st[0x33]==0xABCDE000u&&st[0x25]==0&&st[0x27]==0&&st[0x2D]==0&&st[0x2F]==0);
    ck("csb idle entry",g12_csb_idle(0x7FFu<<15)&&!g12_csb_idle(1u<<15)&&!g12_csb_idle(0x1u));
    if(fails){ printf("FAILURES %d/%d\n",fails,checks); return 1; }
    printf("ALL PASS %d\n",checks); return 0; }
