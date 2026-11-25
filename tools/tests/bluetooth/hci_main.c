#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define RTLBT_HOST
static void* kmalloc(unsigned long n){ return malloc(n); }
static void kfree(void* p){ free(p); }
static const char* host_fwdir="/tmp/rtlbt_hostfw";
static int host_loads;
static u8* rtlbt_load(const char* path,int* len){
    const char* pre="/lib/firmware/rtl_bt/"; if(strncmp(path,pre,strlen(pre)))return 0;
    char fn[256]; snprintf(fn,sizeof fn,"%s/%s",host_fwdir,path+strlen(pre)); host_loads++;
    FILE* f=fopen(fn,"rb"); if(!f)return 0; u8* b=malloc(1<<20); int n=(int)fread(b,1,1<<20,f); fclose(f);
    if(n<=0){ free(b); return 0; } *len=n; return b;
}
#include "net/bluetooth/hci_parse.h"
#include "drivers/bluetooth/firmware/rtlbt_fw.c"
#ifndef FWDIR
#define FWDIR "assets/firmware/rtl_bt"
#endif
#define INCBIN(sym,file) __asm__(".section .rodata\n.global " #sym "_start\n" #sym "_start:\n.incbin \"" FWDIR "/" file "\"\n.global " #sym "_end\n" #sym "_end:\n.previous\n")
INCBIN(_binary_rtl8821c_fw_bin,"rtl8821c_fw.bin");
INCBIN(_binary_rtl8821c_config_bin,"rtl8821c_config.bin");
INCBIN(_binary_rtl8822cu_fw_bin,"rtl8822cu_fw.bin");
INCBIN(_binary_rtl8761bu_config_bin,"rtl8761bu_config.bin");
static int fails, checks;
#define CHECK(c,msg) do{ checks++; if(c)printf("  %-60s ok\n",msg); else { fails++; printf("  %-60s FAIL (line %d)\n",msg,__LINE__); } }while(0)
static int mk(u8* e,int code,const u8* p,int n){ e[0]=(u8)code; e[1]=(u8)n; memcpy(e+2,p,(size_t)n); return n+2; }
static btdev_t* find(btlist_t* L,const u8* a){ for(int i=0;i<L->n;i++) if(!memcmp(L->d[i].addr,a,6))return &L->d[i]; return 0; }
static void hci_tests(void){
    static btlist_t L; u8 e[300], p[260]; int n;
    const u8 A[6]={0x11,0x22,0x33,0x44,0x55,0x66}, B[6]={0xA1,0xA2,0xA3,0xA4,0xA5,0xA6}, C[6]={1,2,3,4,5,6}, D[6]={9,8,7,6,5,4};
    memset(p,0,sizeof p); p[0]=1; memcpy(p+1,A,6); p[7]=1; p[8]=0; p[9]=0x04; p[10]=0x04; p[11]=0x24; p[12]=0x34; p[13]=0x12; p[14]=(u8)-60;
    n=mk(e,0x22,p,15);
    CHECK(hci_evt_parse(&L,e,n)==0x22&&L.n==1,"Inquiry Result with RSSI adds one device");
    btdev_t* d=find(&L,A);
    CHECK(d&&d->rssi==-60&&d->cod==0x240404&&d->clkoff==0x1234&&d->psrm==1&&!d->le,"RSSI, class of device, clock offset, page scan mode");
    CHECK(!strcmp(bth_major(d->cod),"audio"),"class of device 0x240404 is audio");
    memset(p,0,sizeof p); p[0]=1; memcpy(p+1,B,6); p[7]=2; p[8]=0; p[9]=0; p[10]=0x80; p[11]=0x05; p[12]=0x25; p[13]=0x11; p[14]=0x22; p[15]=(u8)-70;
    n=mk(e,0x22,p,16);
    CHECK(hci_evt_parse(&L,e,n)==0x22&&L.n==2,"Inquiry Result with RSSI, 15-byte variant");
    d=find(&L,B); CHECK(d&&d->rssi==-70&&d->cod==0x250580&&d->clkoff==0x2211,"15-byte variant fields");
    memset(p,0,sizeof p); p[0]=1; memcpy(p+1,A,6); p[7]=1; p[9]=0x04; p[10]=0x04; p[11]=0x24; p[12]=0x34; p[13]=0x12; p[14]=(u8)-45;
    const char* nm="Haralds h\xc3\xb6rlurar"; int nl=(int)strlen(nm); p[15]=(u8)(nl+1); p[16]=0x09; memcpy(p+17,nm,(size_t)nl);
    n=mk(e,0x2F,p,255);
    CHECK(hci_evt_parse(&L,e,n)==0x2F&&L.n==2,"Extended Inquiry Result updates the known device");
    d=find(&L,A); CHECK(d&&!strcmp(d->name,"Haralds h?rlurar")&&d->nameq==2,"EIR complete name, UTF-8 shown as ?");
    CHECK(d->rssi==-45,"best RSSI kept");
    memset(p,0,sizeof p); p[0]=1; memcpy(p+1,A,6); p[14]=(u8)-80; n=mk(e,0x22,p,15); hci_evt_parse(&L,e,n);
    CHECK(d->rssi==-45,"weaker later RSSI does not overwrite");
    memset(p,0,sizeof p); p[0]=0; memcpy(p+1,B,6); memcpy(p+7,"Speaker",8); n=mk(e,0x07,p,255);
    CHECK(hci_evt_parse(&L,e,n)==0x07&&!strcmp(find(&L,B)->name,"Speaker")&&find(&L,B)->nameq==3,"Remote Name Request Complete sets the name");
    memset(p,0,sizeof p); p[0]=4; memcpy(p+1,B,6); memcpy(p+7,"Bad",4); n=mk(e,0x07,p,255); hci_evt_parse(&L,e,n);
    CHECK(!strcmp(find(&L,B)->name,"Speaker"),"failed name request (status!=0) is ignored");
    memset(p,0,sizeof p); p[0]=2; memcpy(p+1,C,6); p[7]=1; p[10]=0x0C; p[11]=0x02; p[12]=0x5A; memcpy(p+15,D,6); p[21]=1; p[24]=0x40; p[25]=0x05; p[26]=0;
    n=mk(e,0x02,p,1+28);
    CHECK(hci_evt_parse(&L,e,n)==0x02&&L.n==4,"Inquiry Result with two responses");
    CHECK(find(&L,C)->cod==0x5A020C&&find(&L,D)->cod==0x000540&&find(&L,C)->rssi==127,"Inquiry Result fields, no RSSI");
    int o=0; p[o++]=0x02; p[o++]=2;
    const u8 E1[6]={0x10,0x20,0x30,0x40,0x50,0xC0};
    p[o++]=0; p[o++]=1; memcpy(p+o,E1,6); o+=6; p[o++]=3+6; p[o++]=2; p[o++]=0x01; p[o++]=0x06; p[o++]=5; p[o++]=0x08; memcpy(p+o,"Mous",4); o+=4; p[o++]=(u8)-55;
    p[o++]=0; p[o++]=0; memcpy(p+o,D,6); o+=6; p[o++]=4; p[o++]=3; p[o++]=0x19; p[o++]=0xC2; p[o++]=0x03; p[o++]=(u8)-90;
    n=mk(e,0x3E,p,o);
    CHECK(hci_evt_parse(&L,e,n)==0x3E&&L.n==6,"LE Advertising Report with two reports");
    btdev_t* le=0; for(int i=0;i<L.n;i++) if(L.d[i].le&&!memcmp(L.d[i].addr,E1,6))le=&L.d[i];
    CHECK(le&&!strcmp(le->name,"Mous")&&le->nameq==1&&le->rssi==-55&&le->atype==1,"LE short name, random address type, RSSI");
    btdev_t* le2=0; for(int i=0;i<L.n;i++) if(L.d[i].le&&!memcmp(L.d[i].addr,D,6))le2=&L.d[i];
    CHECK(le2&&le2->appear==0x03C2&&!le2->name[0],"LE appearance, same address as a BR/EDR device kept apart");
    o=0; p[o++]=0x02; p[o++]=1; p[o++]=4; p[o++]=1; memcpy(p+o,E1,6); o+=6; p[o++]=12; p[o++]=11; p[o++]=0x09; memcpy(p+o,"Mouse M720",10); o+=10; p[o++]=(u8)-50;
    n=mk(e,0x3E,p,o); hci_evt_parse(&L,e,n);
    CHECK(!strcmp(le->name,"Mouse M720")&&le->nameq==2&&le->rssi==-50&&L.n==6,"LE scan response: complete name replaces short name");
    o=0; p[o++]=0x02; p[o++]=1; p[o++]=0; p[o++]=1; memcpy(p+o,E1,6); o+=6; p[o++]=5; p[o++]=4; p[o++]=0x08; memcpy(p+o,"Mou",3); o+=3; p[o++]=(u8)-52;
    n=mk(e,0x3E,p,o); hci_evt_parse(&L,e,n);
    CHECK(!strcmp(le->name,"Mouse M720"),"short name does not replace a complete name");
    o=0; p[o++]=0x02; p[o++]=1; p[o++]=0; p[o++]=0; memcpy(p+o,C,6); o+=6; p[o++]=30; p[o++]=2; p[o++]=0x01;
    n=mk(e,0x3E,p,o);
    CHECK(hci_evt_parse(&L,e,n)==-1&&L.n==6,"truncated LE report rejected");
    o=0; p[o++]=0x02; p[o++]=1; p[o++]=0; p[o++]=0; memcpy(p+o,C,6); o+=6; p[o++]=3; p[o++]=0; p[o++]=0x09; p[o++]=(u8)-40; p[o++]=(u8)-40;
    n=mk(e,0x3E,p,o); hci_evt_parse(&L,e,n);
    CHECK(L.n==7,"zero-length AD structure does not loop");
    u8 bad[4]={0x22,0x20,1,2}; CHECK(hci_evt_parse(&L,bad,4)==-1,"event longer than its buffer rejected");
    memset(p,0,sizeof p); p[0]=3; n=mk(e,0x22,p,15); CHECK(hci_evt_parse(&L,e,n)==-1,"wrong response count rejected");
    memset(p,0,sizeof p); p[0]=0; n=mk(e,0x22,p,1); CHECK(hci_evt_parse(&L,e,n)==0x22&&L.n==7,"zero responses accepted, nothing added");
    u8 cc[]={0x0E,0x0C,0x01,0x01,0x10,0x00,0x08,0x0C,0x00,0x08,0x5D,0x00,0x21,0x88};
    CHECK(hci_cc_match(cc,(int)sizeof cc,0x1001)==1&&hci_cc_match(cc,(int)sizeof cc,0x1009)==0,"Command Complete matched by opcode");
    u8 cs[]={0x0F,0x04,0x00,0x01,0x01,0x04};
    CHECK(hci_cc_match(cs,(int)sizeof cs,0x0401)==2,"Command Status matched by opcode");
    char ab[20]; bth_addr(ab,A); CHECK(!strcmp(ab,"66:55:44:33:22:11"),"address printed most significant byte first");
    static btlist_t M; for(int i=0;i<45;i++){ memset(p,0,sizeof p); p[0]=1; p[1]=(u8)i; p[2]=0x77; p[14]=(u8)-30; n=mk(e,0x22,p,15); hci_evt_parse(&M,e,n); }
    CHECK(M.n==BTD_MAX&&M.drops==45-BTD_MAX,"list is bounded and counts what it dropped");
    for(int t=0;t<20000;t++){ int ln=rand()%40; e[0]=(u8)(rand()&1?0x3E:(rand()&1?0x2F:0x22)); e[1]=(u8)(rand()%256); for(int i=2;i<ln;i++)e[i]=(u8)rand(); hci_evt_parse(&L,e,ln); }
    CHECK(L.n<=BTD_MAX,"20000 random events: no crash, list bounded");
}
static int dl_n, dl_bad, dl_last, dl_tot, dl_ver_reads; static u8* dl_buf; static int dl_patched, dl_rom;
static int mock_cmd(u16 op,const u8* p,int n,u8* rp,int rcap){
    (void)rcap;
    if(op==0xFC61&&n==5&&p[1]==0x38){ rp[0]=0; rp[1]=0x21; rp[2]=0x88; return 3; }
    if(op==0xFC61){ rp[0]=0; rp[1]=0; rp[2]=0; return 3; }
    if(op==0x1001){ dl_ver_reads++; u8 v[9]={0,8,0x0c,0x00,8,0x5d,0x00,0x21,0x88}; if(dl_patched){ v[7]=0x34; v[8]=0x12; } memcpy(rp,v,9); return 9; }
    if(op==0xFC6D){ rp[0]=0; rp[1]=(u8)dl_rom; return 2; }
    if(op==0xFC20){ int idx=p[0]; int want=dl_n<=0x7F?dl_n:((dl_n-0x80)%0x7F)+1; if((idx&0x7F)!=want)dl_bad++;
        memcpy(dl_buf+dl_tot,p+1,(size_t)(n-1)); dl_tot+=n-1; dl_last=idx; dl_n++; if(idx&0x80)dl_patched=1; rp[0]=0; rp[1]=(u8)idx; return 2; }
    rp[0]=0x01; return 1;
}
static void fw_tests(void){
    const u8* fw; int fl=rtlbt_blob("rtl8821c_fw",&fw); u16 lmp=0;
    CHECK(fl==54636&&rtlbt_fw_type(fw,fl,&lmp)==1&&lmp==0x8821,"rtl8821c_fw.bin: V1 epatch for LMP 0x8821 (project 10)");
    static u8 out[70000];
    for(int rom=0;rom<4;rom++){ int r=rtlbt_parse_v1(fw,fl,(u8)rom,out,(int)sizeof out); printf("V1 %d %d\n",rom,r); if(r>0){ char fn[64]; snprintf(fn,sizeof fn,"/tmp/rtlbt_v1_%d.bin",rom); FILE* f=fopen(fn,"wb"); fwrite(out,1,(size_t)r,f); fclose(f); } }
    CHECK(rtlbt_parse_v1(fw,fl,1,out,(int)sizeof out)==34916&&!memcmp(out+34916-4,fw+8,4),"V1 ROM 1 patch ends with the header firmware version");
    CHECK(rtlbt_parse_v1(fw,fl,1,out,1000)==-1,"V1 refuses a too small output buffer");
    int f2=rtlbt_blob("rtl8822cu_fw",&fw);
    CHECK(f2==37327&&rtlbt_fw_type(fw,f2,&lmp)==2&&lmp==0x8822,"rtl8822cu_fw.bin: V2 epatch for LMP 0x8822 (project 13)");
    for(int rom=0;rom<4;rom++){ int r=rtlbt_parse_v2(fw,f2,(u8)rom,0,out,(int)sizeof out); printf("V2 %d %d\n",rom,r); if(r>0){ char fn[64]; snprintf(fn,sizeof fn,"/tmp/rtlbt_v2_%d.bin",rom); FILE* f=fopen(fn,"wb"); fwrite(out,1,(size_t)r,f); fclose(f); } }
    const u8* c1; const u8* c2; CHECK(rtlbt_blob("rtl8821c_config",&c1)==10&&rtlbt_blob("rtl8822cu_config",&c2)==6,"config blobs present");
    CHECK(rtlbt_get_ic(0x8821,0xc,8)&&rtlbt_get_ic(0x8822,0xc,0xa)&&!rtlbt_get_ic(0x8822,0xe,0xc)&&!rtlbt_get_ic(0x1234,0xc,8),"chip table: 8821C and 8822C present, unknown ids refused");
    static u8 syn[200]; int o=0; memcpy(syn,"RTBTCore",8); o=8; memset(syn+o,0x11,8); o+=8; syn[o]=2; syn[o+1]=syn[o+2]=syn[o+3]=0; o+=4;
    int s1=o; syn[o]=1; syn[o+1]=syn[o+2]=syn[o+3]=0; o+=8; int b1=o; syn[o]=3; syn[o+1]=0; o+=4;
    u8 subs[3][4]={{1,2,0,0},{1,1,0,0},{2,0,0,0}}; const char* dat[3]={"BB","AA","XX"};
    for(int i=0;i<3;i++){ memcpy(syn+o,subs[i],4); syn[o+4]=2; syn[o+5]=syn[o+6]=syn[o+7]=0; o+=8; memcpy(syn+o,dat[i],2); o+=2; }
    u32 l1=(u32)(o-b1); memcpy(syn+s1+4,&l1,4);
    int s2=o; syn[o]=3; syn[o+1]=syn[o+2]=syn[o+3]=0; o+=8; int b2=o; syn[o]=1; syn[o+1]=0; o+=4;
    syn[o]=1; syn[o+1]=0; syn[o+2]=5; syn[o+3]=0; syn[o+4]=2; syn[o+5]=syn[o+6]=syn[o+7]=0; o+=8; memcpy(syn+o,"SS",2); o+=2;
    u32 l2=(u32)(o-b2); memcpy(syn+s2+4,&l2,4);
    syn[o++]=13; syn[o++]=1; syn[o++]=0; syn[o++]=0x51; syn[o++]=0x04; syn[o++]=0xFD; syn[o++]=0x77;
    lmp=0; CHECK(rtlbt_fw_type(syn,o,&lmp)==2&&lmp==0x8822,"synthetic V2 file recognised");
    int r=rtlbt_parse_v2(syn,o,0,0,out,(int)sizeof out);
    CHECK(r==4&&!memcmp(out,"AABB",4),"V2: only matching ECO, sorted by priority, security skipped with key 0");
    r=rtlbt_parse_v2(syn,o,0,5,out,(int)sizeof out);
    CHECK(r==6&&!memcmp(out,"SSAABB",6),"V2: security subsection with matching key id included");
    r=rtlbt_parse_v2(syn,o,0,6,out,(int)sizeof out);
    CHECK(r==4,"V2: security subsection with other key id skipped");
    syn[o-4]=0; CHECK(rtlbt_fw_type(syn,o,&lmp)==0,"bad extension signature rejected");
    dl_buf=malloc(80000); dl_rom=1; char msg[96];
    int sr=rtlbt_setup(mock_cmd,msg,(int)sizeof msg);
    printf("  setup: %s\n",msg);
    fw=0; fl=rtlbt_blob("rtl8821c_fw",&fw); int pl=rtlbt_parse_v1(fw,fl,1,out,(int)sizeof out); memcpy(out+pl,c1,10); pl+=10;
    CHECK(sr==1&&dl_tot==pl&&!memcmp(dl_buf,out,(size_t)pl),"download sends patch + config exactly");
    CHECK(dl_n==pl/252+1&&dl_bad==0&&(dl_last&0x80),"fragment count and index sequence 0..127,1.., end flag");
    CHECK(strstr(msg,"0x000c1234")!=0,"version re-read after download");
    dl_ver_reads=0; sr=rtlbt_setup(mock_cmd,msg,(int)sizeof msg);
    CHECK(sr==2&&strstr(msg,"already running"),"patched chip is recognised, no second download");
    free(dl_buf);
}
static int m2_hver, m2_hrev, m2_lmp, m2_rom, m2_fc6d, m2_n, m2_tot, m2_bad, m2_last, m2_patched; static u8* m2_buf;
static int mock2(u16 op,const u8* p,int n,u8* rp,int rcap){
    (void)rcap;
    if(op==0xFC61&&n==5&&p[1]==0x38){ rp[0]=0; rp[1]=(u8)m2_lmp; rp[2]=(u8)(m2_lmp>>8); return 3; }
    if(op==0xFC61){ rp[0]=0; rp[1]=0; rp[2]=0; return 3; }
    if(op==0x1001){ u8 v[9]={0,(u8)m2_hver,(u8)m2_hrev,(u8)(m2_hrev>>8),8,0x5d,0x00,(u8)m2_lmp,(u8)(m2_lmp>>8)}; if(m2_patched){ v[7]=0x99; v[8]=0x77; } memcpy(rp,v,9); return 9; }
    if(op==0xFC6D){ m2_fc6d++; rp[0]=0; rp[1]=(u8)m2_rom; return 2; }
    if(op==0xFC20){ int idx=p[0]; int want=m2_n<=0x7F?m2_n:((m2_n-0x80)%0x7F)+1; if((idx&0x7F)!=want)m2_bad++;
        memcpy(m2_buf+m2_tot,p+1,(size_t)(n-1)); m2_tot+=n-1; m2_last=idx; m2_n++; if(idx&0x80)m2_patched=1; rp[0]=0; rp[1]=(u8)idx; return 2; }
    rp[0]=0x01; return 1;
}
static void m2_chip(int hver,int hrev,int lmp,int rom){ m2_hver=hver; m2_hrev=hrev; m2_lmp=lmp; m2_rom=rom; m2_fc6d=m2_n=m2_tot=m2_bad=m2_last=m2_patched=0; host_loads=0; }
static void put(const char* nm,const u8* d,int n){ char fn[256]; snprintf(fn,sizeof fn,"%s/%s",host_fwdir,nm); FILE* f=fopen(fn,"wb"); fwrite(d,1,(size_t)n,f); fclose(f); }
static void del(const char* nm){ char fn[256]; snprintf(fn,sizeof fn,"%s/%s",host_fwdir,nm); remove(fn); }
static int synv2(u8* syn,u8 proj){
    int o=0; memcpy(syn,"RTBTCore",8); o=8; memset(syn+o,0x22,8); o+=8; syn[o]=1; syn[o+1]=syn[o+2]=syn[o+3]=0; o+=4;
    int s1=o; syn[o]=1; syn[o+1]=syn[o+2]=syn[o+3]=0; o+=8; int b1=o; syn[o]=2; syn[o+1]=0; syn[o+2]=syn[o+3]=0; o+=4;
    for(int i=0;i<2;i++){ syn[o]=2; syn[o+1]=(u8)(2-i); syn[o+2]=0; syn[o+3]=0; u32 l=300; memcpy(syn+o+4,&l,4); o+=8; memset(syn+o,i?'Q':'P',300); o+=300; }
    u32 l1=(u32)(o-b1); memcpy(syn+s1+4,&l1,4);
    syn[o++]=proj; syn[o++]=1; syn[o++]=0; syn[o++]=0x51; syn[o++]=0x04; syn[o++]=0xFD; syn[o++]=0x77;
    return o;
}
static void fw2_tests(void){
    char cmd[300]; snprintf(cmd,sizeof cmd,"rm -rf %s && mkdir -p %s",host_fwdir,host_fwdir); if(system(cmd)!=0){ CHECK(0,"temp firmware dir"); return; }
    unsigned nt=sizeof rtlbt_ic_tab/sizeof rtlbt_ic_tab[0];
    CHECK(nt==15,"chip table has the 15 FreeBSD rtlbtfw entries");
    int dup=0; for(unsigned i=0;i<nt;i++) for(unsigned k=i+1;k<nt;k++) if(rtlbt_ic_tab[i].lmp==rtlbt_ic_tab[k].lmp&&rtlbt_ic_tab[i].rev==rtlbt_ic_tab[k].rev&&rtlbt_ic_tab[i].ver==rtlbt_ic_tab[k].ver)dup++;
    CHECK(!dup,"no two chip entries share lmp/revision/version");
    const rtlbt_ic_t* a=rtlbt_get_ic(0x8821,0xc,8); const rtlbt_ic_t* b=rtlbt_get_ic(0x8822,0xc,0xa);
    CHECK(a&&b&&a->flags==(RTLBT_F_MSFT|RTLBT_F_EMB)&&b->flags==(RTLBT_F_MSFT|RTLBT_F_EMB)&&!strcmp(a->fw,"rtl8821c_fw")&&!strcmp(b->cfg,"rtl8822cu_config"),"8821C / 8822C stay embedded with the same blob names");
    int emb=0; for(unsigned i=0;i<nt;i++) if(rtlbt_ic_tab[i].flags&RTLBT_F_EMB)emb++;
    CHECK(emb==2,"only 8821C and 8822C use the embedded firmware");
    const rtlbt_ic_t* c=rtlbt_get_ic(0x8822,0xb,7); const rtlbt_ic_t* d=rtlbt_get_ic(0x8723,0xd,8); const rtlbt_ic_t* e=rtlbt_get_ic(0x1200,0xb,6); const rtlbt_ic_t* f=rtlbt_get_ic(0x8852,0xc,0xc); const rtlbt_ic_t* g=rtlbt_get_ic(0x8852,0x87,0xc);
    CHECK(c&&c->flags==(RTLBT_F_CONFIG|RTLBT_F_MSFT)&&d&&d->flags==RTLBT_F_CONFIG&&e&&e->flags==RTLBT_F_SIMPLE&&!e->cfg,"8822B config+MSFT, 8723D config required, 8723A simple without config");
    CHECK(f&&!strcmp(f->fw,"rtl8852cu_fw_v2.bin")&&g&&!strcmp(g->fw,"rtl8852btu_fw.bin"),"8852C uses _fw_v2.bin, 8852BT matched by revision 0x87");
    CHECK(sizeof rtlbt_usb_ids/sizeof rtlbt_usb_ids[0]==58,"58 non-Realtek-vendor USB ids");
    int vbda=0; for(unsigned i=0;i<58;i++) if(rtlbt_usb_ids[i][0]==0x0bda)vbda++;
    CHECK(!vbda&&rtlbt_usb_listed(0x13d3,0x3529)&&rtlbt_usb_listed(0x0cb5,0xc547)&&rtlbt_usb_listed(0x1358,0xc123),"list has no 0x0bda ids, first and last FreeBSD ids present");
    CHECK(!rtlbt_usb_listed(0x0bda,0xc123)&&!rtlbt_usb_listed(0x8087,0x0029)&&!rtlbt_usb_listed(0x13d3,0x0000),"owner 0BDA:C123, Intel and unknown ids not listed");
    static u8 syn[2000], exp[4000]; char msg[96]; int sr;
    m2_buf=malloc(1<<20);
    m2_chip(0xa,0xb,0x8761,0); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==-1&&strstr(msg,"/lib/firmware/rtl_bt/rtl8761bu_fw.bin")&&m2_n==0,"8761BU without firmware on disk: clear message naming the file, nothing sent");
    int sl=synv2(syn,14); put("rtl8761bu_fw.bin",syn,sl);
    m2_chip(0xa,0xb,0x8761,1); sr=rtlbt_setup(mock2,msg,(int)sizeof msg); printf("  setup: %s\n",msg);
    memset(exp,'Q',300); memset(exp+300,'P',300);
    CHECK(sr==1&&m2_tot==600&&!memcmp(m2_buf,exp,600)&&m2_bad==0&&(m2_last&0x80),"8761BU from disk, optional config absent: patch only, sorted by priority");
    u8 cfg[7]={0x55,0xAB,0x23,0x87,1,2,3}; put("rtl8761bu_config.bin",cfg,7);
    m2_chip(0xa,0xb,0x8761,1); sr=rtlbt_setup(mock2,msg,(int)sizeof msg); memcpy(exp+600,cfg,7);
    CHECK(sr==1&&m2_tot==607&&!memcmp(m2_buf,exp,607)&&m2_n==607/252+1,"8761BU: config file appended after the patch");
    m2_chip(0xa,0xb,0x8761,3); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==-1&&strstr(msg,"no patch for ROM")&&m2_n==0,"wrong ROM version: refused, nothing sent");
    sl=synv2(syn,13); put("rtl8822b_fw.bin",syn,sl);
    m2_chip(7,0xb,0x8822,1); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==-1&&strstr(msg,"config file missing: /lib/firmware/rtl_bt/rtl8822b_config.bin")&&m2_n==0,"8822B: required config missing, refused with the file name");
    put("rtl8822b_config.bin",cfg,7);
    m2_chip(7,0xb,0x8822,1); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==1&&m2_tot==607,"8822B: patch + required config sent");
    sl=synv2(syn,14); put("rtl8822b_fw.bin",syn,sl);
    m2_chip(7,0xb,0x8822,1); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==-1&&strstr(msg,"does not match")&&m2_n==0,"firmware file for another chip refused");
    u8 raw[600]; for(int i=0;i<600;i++)raw[i]=(u8)(i*7+1); put("rtl8723a_fw.bin",raw,600);
    m2_chip(6,0xb,0x1200,0); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==1&&m2_tot==600&&!memcmp(m2_buf,raw,600)&&m2_fc6d==0,"8723A simple: raw file sent unchanged, no ROM version query");
    put("rtl8723a_fw.bin",syn,sl);
    m2_chip(6,0xb,0x1200,0); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==-1&&m2_n==0,"8723A: epatch file refused");
    m2_chip(8,0xc,0x8821,1); dl_rom=1; sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(sr==1&&host_loads==0,"8821C never touches /lib/firmware (embedded path)");
    m2_chip(0xa,0xc,0x8822,1); sr=rtlbt_setup(mock2,msg,(int)sizeof msg);
    CHECK(host_loads==0,"8822C never touches /lib/firmware (embedded path)");
    free(m2_buf);
}
int main(void){
    printf("HCI event parser\n"); hci_tests();
    printf("Realtek firmware loader\n"); fw_tests();
    printf("Realtek chip table, USB ids, firmware from disk\n"); fw2_tests();
    printf("%d checks, %d failures\n%s\n",checks,fails,fails?"FAILURES":"ALL PASS");
    return fails?1:0;
}
