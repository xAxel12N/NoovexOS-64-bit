#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lib/base/fmt.c"
#include "drivers/video/amd/atom/atom.c"

static u8 rom[65536];
static void w16(u32 o,u16 v){ rom[o]=v; rom[o+1]=v>>8; }
static void w32(u32 o,u32 v){ w16(o,v); w16(o+2,v>>16); }
static void hdr(u32 o,u16 sz,u8 f,u8 c){ w16(o,sz); rom[o+2]=f; rom[o+3]=c; }

static u32 build(int isi2){
    memset(rom,0,sizeof rom); rom[0]=0x55; rom[1]=0xAA; rom[2]=64;
    rom[0x2F]=1; w16(0x6E,0x100); strcpy((char*)rom+0x100,"113-RENOIR-111"); strcpy((char*)rom+0x50,"2021/03/04 10:00");
    strcpy((char*)rom+0x140,"ATOMBIOSBK-AMD VER017.012.000.040.000000");
    u32 h=0x200; w16(0x48,h); hdr(h,38,2,2); memcpy(rom+h+4,"ATOM",4); w16(h+24,0x103C); w16(h+26,0x8A20); w16(h+30,0x300); w16(h+32,0x400);
    hdr(0x300,4+2*80,2,1); hdr(0x400,4+2*35,2,1);
    w16(0x404+8,0x500); hdr(0x500,0x60,3,4); w32(0x504,0x00120040); w32(0x508,40000); w32(0x50C,160000); w32(0x510,1);
    w16(0x404+60,0x600);
    if(isi2){ hdr(0x600,0x200,2,1); rom[0x600+26]=0x23; rom[0x600+27]=4; rom[0x600+29]=100; w16(0x600+32,200); rom[0x600+47]=8; rom[0x600+48]=128; }
    else { hdr(0x600,0x200,1,11); w16(0x600+40,1000); rom[0x600+42]=0x1E; rom[0x600+43]=2; rom[0x600+51]=12; rom[0x600+53]=105; }
    w16(0x404+44,0x900); hdr(0x900,8+16*3,1,isi2?5:4); w16(0x904,0x0F); rom[0x906]=3;
    w16(0x908,0x3114); w16(0x90C,0x211E); w16(0x914,2);
    w16(0x918,0x310C); w16(0x91C,0x2120); w16(0x924,0x10);
    w16(0x928,0x2113); w16(0x92C,0x2121);
    return 32768;
}

int main(void){
    char out[2048]; u32 n;
    for(int v=0;v<2;v++){ n=build(v);
        if(!amd_atom_parse(rom,n,&g_amd_atom)){ printf("parse fail %s\n",g_amd_atom.why); return 1; }
        amd_atom_describe(&g_amd_atom,out,sizeof out); fputs(out,stdout);
        if(g_amd_atom.ncon!=2||g_amd_atom.con[0].type!=0x14||g_amd_atom.boot_sclk_10khz!=40000){ puts("FAIL fields"); return 1; }
        if(v==0&&(g_amd_atom.memtype!=0x1E||g_amd_atom.bl_pwm_hz!=1000||g_amd_atom.htc_lim!=105)){ puts("FAIL isi1"); return 1; }
        if(v==1&&(g_amd_atom.memtype!=0x23||g_amd_atom.edp_bpc!=8||g_amd_atom.edp_boot_bl!=128)){ puts("FAIL isi2"); return 1; }
        u32 o; u8 f,c; u16 s; if(!amd_atom_table("integratedsysteminfo",&o,&f,&c,&s)||o!=0x600){ puts("FAIL table"); return 1; }
        if(strcmp(g_amd_atom.pn,"113-RENOIR-111")||strncmp(g_amd_atom.ver,"017.012",7)){ printf("FAIL str [%s][%s]\n",g_amd_atom.pn,g_amd_atom.ver); return 1; } }
    static u8 vf[0x4C+0x1C+32768+0x1C+16]; memset(vf,0,sizeof vf); memcpy(vf,"VFCT",4);
    u32 tl=sizeof vf; vf[4]=tl; vf[5]=tl>>8; vf[6]=tl>>16; vf[0x34]=0x4C;
    u8* e=vf+0x4C; e[0]=4; e[12]=0x02; e[13]=0x10; e[14]=0x36; e[15]=0x16; e[0x18]=16;
    e=vf+0x4C+0x1C+16; e[0]=5; e[12]=0x02; e[13]=0x10; e[14]=0x38; e[15]=0x16; u32 il=32768; e[0x18]=il; e[0x19]=il>>8; memcpy(e+0x1C,rom,il);
    u32 ol=0; u32 off=amd_vfct_find(vf,tl,5,0,0,0x1002,0x1638,&ol); if(off!=0x4C+0x1C+16+0x1C||ol!=il||!amd_atom_check(vf+off,ol)){ printf("FAIL vfct %u %u\n",off,ol); return 1; }
    if(amd_vfct_find(vf,tl,6,0,0,0x1002,0x1638,&ol)){ puts("FAIL vfct nomatch"); return 1; }
    vf[0x4C+0x1C+16+0x19]=0xFF; vf[0x4C+0x1C+16+0x1A]=0xFF; if(amd_vfct_find(vf,tl,5,0,0,0x1002,0x1638,&ol)){ puts("FAIL vfct trunc"); return 1; }
    srand(1); for(int it=0;it<20000;it++){ build(it&1); for(int k=0;k<8;k++)rom[0x200+rand()%0x800]=rand(); u32 L=0x100+rand()%32768; amd_atom_parse(rom,L,&g_amd_atom); amd_atom_describe(&g_amd_atom,out,sizeof out); }
    for(int it=0;it<5000;it++){ for(int k=0;k<16;k++)vf[rand()%0x200]=rand(); memcpy(vf,"VFCT",4); amd_vfct_find(vf,rand()%sizeof vf,5,0,0,0x1002,0x1638,&ol); }
    puts("atom: all tests passed"); return 0;
}
