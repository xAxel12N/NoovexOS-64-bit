#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define FP_HOST
#include "lib/base/fmt.c"
#include "drivers/input/fingerprint/fp_usb.c"
#include "drivers/input/fingerprint/syna_moc.c"
#include "drivers/input/fingerprint/goodix_moc.c"
static int fails, checks;
#define CK(c,m) do{ checks++; if(c)printf("  %s ok\n",m); else { fails++; printf("  FAIL %s\n",m); } }while(0)
static u8 q[32][300]; static int ql[32], qh, qt, cur_len, sy_match=1, sy_full, sy_finger_evt=1, cmds; static u8 cur[300]; static char sim_uid[128]; static int sy_have;
static void sq(u8 seq,u8 id,const u8* p,int n){ u8* b=q[qt%32]; b[0]=0; b[1]=0; b[2]=0xFE; b[3]=seq; b[4]=id; b[5]=(u8)n; if(n)memcpy(b+6,p,(size_t)n); ql[qt%32]=n+6; qt++; }
static void syrep(u8 seq,u8 id,const u8* p,int n){ cur[0]=0; cur[1]=0; cur[2]=0xFE; cur[3]=seq; cur[4]=id; cur[5]=(u8)n; if(n)memcpy(cur+6,p,(size_t)n); cur_len=n+6; }
static int sy_out(void* c,int ep,const u8* p,int n,int ms){ (void)c; (void)ms; if(ep!=1)return -1; cmds++;
    if(n==1&&p[0]==0xA8){ if(qh==qt)return -1; memcpy(cur,q[qh%32],(size_t)ql[qh%32]); cur_len=ql[qh%32]; qh++; return 1; }
    if(n<5||p[0]!=0xA7||p[1]!=0xFE||p[4]!=n-5)return -1; u8 s=p[2], id=p[3]; const u8* pl=p+5; int pn=p[4];
    if(id==0x11){ u8 z=0; syrep(s,0x13,&z,1); }
    else if(id==0x51){ syrep(s,0x54,0,0); if(sy_full){ u8 e[2]={0x01,0xF5}; sq(s,0x58,e,2); return n; }
        if(sy_finger_evt){ u8 f=1; sq(0,0x91,&f,1); }
        for(int k=1;k<=4;k++){ u8 pc=(u8)(k*25); sq(s,0x55,&pc,1); } memcpy(sim_uid,pl+2,(size_t)(pn-2)); sim_uid[pn-2]=0; sy_have=1; sq(s,0x59,pl+1,pn-1); }
    else if(id==0x65){ syrep(s,0x66,0,0); sq(s,0x60,0,0);
        if(!sy_have||strlen(sim_uid)!=(size_t)pn||memcmp(sim_uid,pl,(size_t)pn)){ u8 e[2]={0x01,0xF8}; sq(s,0x67,e,2); }
        else if(!sy_match){ u8 e[2]={0x01,0x94}; sq(s,0x67,e,2); }
        else { u8 r[128]; r[0]=97; r[1]=50; r[2]=1; memcpy(r+3,pl,(size_t)pn); sq(s,0x68,r,pn+3); } }
    else if(id==0x81){ sy_have=0; syrep(s,0x83,0,0); }
    else return -1;
    return n; }
static int sy_in(void* c,int ep,u8* p,int cap,int ms){ (void)c; (void)ms;
    if(ep==0x83){ p[0]=qh!=qt?4:0; for(int i=1;i<7;i++)p[i]=0; return 7; }
    if(ep!=0x81||!cur_len)return -1; int n=cur_len<cap?cur_len:cap; memcpy(p,cur,(size_t)n); cur_len=0; return n; }
static int pcts[16], npct; static void prog(int p){ if(npct<16)pcts[npct++]=p; }
static u8 gq[8][2048]; static int gl[8], gh, gt, gx_have, gx_match=1, gx_quality=80, gx_badack; static char gx_uid[64]; static int gx_cfgst; static u8 gx_tidv[32];
static void gpush(u8 c0,const u8* b,int n){ static u8 seqd; gl[gt%8]=gx_build(gq[gt%8],2048,c0,0,seqd++,b,n); gt++; }
static int g_out(void* c,int ep,const u8* p,int n,int ms){ (void)c; (void)ms; if(ep!=1)return -1; u8 c0; const u8* b; int bl;
    if(gx_parse(p,n,&c0,&b,&bl))return -1; if(p[7]!=(u8)~p[6]||gx_crc8(p,6)!=p[6])return -1;
    u8 ack[2]={0,gx_badack?0x11:c0}; gpush(0xAA,ack,2); u8 r[256]; int rn=1; r[0]=0;
    switch(c0){ case 0xD0: r[1]=1; rn=2; break; case 0xC0: gx_cfgst=b[6]; r[1]=0; r[2]=10; rn=3; if(bl!=128||gx_crc32(b,124)!=(u32)(b[124]|b[125]<<8|b[126]<<16|(u32)b[127]<<24))r[0]=0x80; break;
        case 0xA6: r[1]=0; rn=2; break; case 0xA1: for(int i=0;i<32;i++)r[1+i]=gx_tidv[i]=(u8)(0x40+i); rn=33; break;
        case 0xA2: r[1]=(u8)gx_quality; r[2]=90; rn=3; break; case 0xA0: r[1]=50; r[2]=10; rn=3; break; case 0xB0: break; case 0xA3: break;
        case 0xA4: { int fl=b[0]|b[1]<<8; if(fl+2!=bl||b[2]!=67||memcmp(b+38,gx_tidv,32)){ r[0]=0x80; break; } int ul=b[70]; memcpy(gx_uid,b+71,(size_t)ul); gx_uid[ul]=0; gx_have=1; } break;
        case 0xA5: if(!gx_have||!gx_match){ r[0]=1; break; } { int ul=(int)strlen(gx_uid), fs=69+ul+1; r[1]=0; r[2]=0; r[3]=100; r[4]=r[5]=r[6]=0; r[7]=0; r[8]=(u8)fs; r[9]=0; u8* t=r+10; memset(t,0,(size_t)fs); t[0]=67; t[1]=1; t[2]=1; memcpy(t+36,gx_tidv,32); t[68]=(u8)ul; memcpy(t+69,gx_uid,(size_t)ul); rn=10+fs; } break;
        case 0xA7: if(p[1]==0x04)gx_have=0; break; default: r[0]=0x80; }
    gpush(c0,r,rn); return n; }
static int gx_hold, gx_holds; static int g_in(void* c,int ep,u8* p,int cap,int ms){ (void)c; (void)ms; if(ep!=0x83||gh==gt)return -1; if(gx_hold&&gq[gh%8][0]!=0xAA&&(gq[gh%8][0]==0xA2||gq[gh%8][0]==0xB0)){ gx_hold--; gx_holds++; return -1; } int n=gl[gh%8]<cap?gl[gh%8]:cap; memcpy(p,gq[gh%8],(size_t)n); gh++; return n; }
static u8 fz[300]; static int fz_n;
static int fz_out(void* c,int ep,const u8* p,int n,int ms){ (void)c; (void)ep; (void)p; (void)ms; return n; }
static int fz_in(void* c,int ep,u8* p,int cap,int ms){ (void)c; (void)ms; if(ep==0x83){ p[0]=(u8)(rand()&4); return 7; } int n=fz_n<cap?fz_n:cap; memcpy(p,fz,(size_t)n); return n; }
int main(void){
    fp_xport_t x={0,sy_out,sy_in,0};
    CK(fprint_lookup(0x06cb,0x00bd)->kind==FPK_SYNA_MOC&&fprint_lookup(0x06cb,0x00f0)->kind==FPK_SYNA_MOC&&fprint_lookup(0x06cb,0x009a)->kind==FPK_VALIDITY,"Synaptics Prometheus IDs from libfprint; other 06cb readers stay unsupported");
    CK(fprint_lookup(0x27c6,0x5840)->kind==FPK_GOODIX_MOC&&fprint_lookup(0x27c6,0x63ac)->kind==FPK_GOODIX_MOC&&fprint_lookup(0x27c6,0x5110)->kind==FPK_GOODIX,"Goodix MOC IDs from libfprint; Goodix image sensors stay unsupported");
    CK(fprint_supported(FPK_SYNA_MOC)&&fprint_supported(FPK_GOODIX_MOC)&&!fprint_supported(FPK_GOODIX)&&fprint_is_moc(FPK_SYNA_MOC)&&!fprint_is_moc(FPK_ELAN),"support flags");
    synafp_open(&x); CK(synafp_init()==1,"Synaptics FPS_INIT answered");
    npct=0; CK(synafp_enroll("nvx:alice",1,prog)==1&&!strcmp(sim_uid,"nvx:alice")&&npct==4&&pcts[3]==100,"Synaptics enroll: ready, 4 progress reports via interrupt+async read, finger event skipped, ENROLL_OK");
    CK(synafp_verify("nvx:alice")==1,"Synaptics verify: match");
    sy_match=0; CK(synafp_verify("nvx:alice")==0,"Synaptics verify: no match (404)"); sy_match=1;
    CK(synafp_verify("nvx:bob")==-2,"Synaptics verify: unknown user (504)");
    CK(synafp_delete("nvx:alice",1)==1&&synafp_verify("nvx:alice")==-2,"Synaptics delete");
    sy_full=1; CK(synafp_enroll("nvx:carol",1,0)==-2,"Synaptics enroll: database full (501)"); sy_full=0;
    CK(synafp_enroll("nvx:alice",1,0)==1,"Synaptics re-enroll after delete");
    { u8 r[16]={97,50,1}; memcpy(r+3,"nvx:alice",9); sq(sy.seq,0x68,r,12); sy_match=0; CK(synafp_verify("nvx:alice")==0,"Synaptics verify: a late VERIFY_OK left over from an earlier command (old sequence number) is not taken as a match"); sy_match=1; qh=qt; }
    { synafp_delete("nvx:alice",1); int r, st=0, pc=0, mx=0, lastpc=-1, mono=1; CK(synafp_enroll_begin("nvx:alice",1)==0,"Synaptics step enrol: begin sends ENROLL and returns without waiting for the finger");
      while(!(r=synafp_enroll_step(&pc))&&st<100){ st++; if(pc<lastpc)mono=0; lastpc=pc; if(pc>mx)mx=pc; }
      CK(r==1&&st>=4&&mx==100&&mono&&!strcmp(sim_uid,"nvx:alice"),"Synaptics step enrol: one interrupt poll per step, progress 25..100, ENROLL_OK"); CK(syc.pms==50,"Synaptics step enrol polls in 50 ms slices"); }
    { sy_full=1; int r, st=0, pc; CK(synafp_enroll_begin("nvx:carol",1)==0,"Synaptics step enrol (full): begin"); while(!(r=synafp_enroll_step(&pc))&&st<100)st++; CK(r==-2,"Synaptics step enrol: storage full (501) reported as -2"); sy_full=0; }
    { qh=qt; int r, st=0, pc; CK(synafp_enroll_begin("nvx:dan",1)==0,"Synaptics step enrol (silent reader): begin"); qh=qt; while(!(r=synafp_enroll_step(&pc))&&st<5000)st++; CK(r==-1&&st<=1200,"Synaptics step enrol: a reader that never answers fails after the 60 s budget (1200 polls of 50 ms)"); }
    { int s0=sy.seq; sy.seq=255; synafp_init(); CK(sy.cur==1,"sequence number skips 0 after 255"); sy.seq=(u8)s0; }
    { fp_xport_t f={0,fz_out,fz_in,0}; synafp_open(&f); int bad=0; srand(7); for(int it=0;it<20000;it++){ fz_n=rand()%300; for(int i=0;i<fz_n;i++)fz[i]=(u8)rand(); if(rand()&1&&fz_n>6){ fz[2]=0xFE; fz[5]=(u8)(rand()%(fz_n-5)); } int r=synafp_verify("nvx:z"); if(r>1||r<-3)bad=1; } CK(!bad,"Synaptics: 20000 random replies handled without overruns"); }
    CK(gx_crc32((const u8*)"123456789",9)==0xCBF43926u,"Goodix CRC-32 matches the standard check value");
    { u8 b[64]; u8 c0; const u8* bd; int bl; int n=gx_build(b,64,0xA2,0x00,5,(const u8*)"\x01\x02\x03",3); CK(n==15&&b[4]==7&&b[7]==(u8)~b[6]&&!gx_parse(b,n,&c0,&bd,&bl)&&c0==0xA2&&bl==3,"Goodix package build/parse round trip");
      b[9]^=1; CK(gx_parse(b,n,&c0,&bd,&bl)==-1,"Goodix corrupted package rejected by CRC"); b[9]^=1; b[4]=200; CK(gx_parse(b,n,&c0,&bd,&bl)==-1,"Goodix oversize length rejected"); }
    fp_xport_t g={0,g_out,g_in,0}; goodixfp_open(&g);
    CK(goodixfp_init()==1,"Goodix init: version + sensor config with CRC");
    npct=0; CK(goodixfp_enroll("nvx:alice",prog)==1&&!strcmp(gx_uid,"nvx:alice")&&npct==8&&pcts[7]==100&&!memcmp(goodixfp_tid(),gx_tidv,32),"Goodix enroll: 8 stages, duplicate check, commit with template id + user id");
    CK(goodixfp_verify("nvx:alice")==1,"Goodix verify: match and template user id checked");
    CK(goodixfp_verify("nvx:bob")==0,"Goodix verify: other user's template is not a match");
    gx_match=0; CK(goodixfp_verify("nvx:alice")==0,"Goodix verify: no match"); gx_match=1;
    goodixfp_set_stages(fprint_gx_stages(0x63ac)); CK(goodixfp_init()==1&&gx_cfgst==12,"Goodix 12-stage reader (27c6:63ac): sensor config byte 6 = 12");
    npct=0; CK(goodixfp_enroll("nvx:erin",prog)==1&&npct==12&&pcts[11]==100&&pcts[5]==50,"Goodix 12-stage enroll reports 12 steps");
    goodixfp_set_stages(8); goodixfp_init(); CK(gx_cfgst==8,"Goodix default reader: config byte 6 = 8");
    gx_quality=5; npct=0; CK(goodixfp_enroll("nvx:dave",prog)==0&&npct==0,"Goodix enroll: poor samples never complete (bounded retries)"); gx_quality=80;
    { goodixfp_set_stages(8); goodixfp_init(); gx_have=0; int r, st=0, pc=0, lastpc=0, mono=1; gx_hold=3; gx_holds=0; CK(goodixfp_enroll_begin("nvx:fred")==0,"Goodix step enrol: begin (A6 duplicate check, A1 template id) without a capture");
      while(!(r=goodixfp_enroll_step(&pc))&&st<1000){ st++; if(pc<lastpc)mono=0; lastpc=pc; }
      CK(r==1&&!strcmp(gx_uid,"nvx:fred")&&gx_holds==3&&pc==100&&mono&&!memcmp(goodixfp_tid(),gx_tidv,32),"Goodix step enrol: no-finger polls wait, 8 stages, commit with template id + user id"); }
    { gx_quality=5; int r, st=0, pc; CK(goodixfp_enroll_begin("nvx:gus")==0,"Goodix step enrol (poor samples): begin"); while(!(r=goodixfp_enroll_step(&pc))&&st<1000)st++; CK(r==-1&&st<1000,"Goodix step enrol: poor samples fail after bounded retries"); gx_quality=80; }
    { int r, st=0, pc; CK(goodixfp_enroll_begin("nvx:hal")==0,"Goodix step enrol (no finger): begin"); gx_hold=100000; while(!(r=goodixfp_enroll_step(&pc))&&st<10000)st++; CK(r==-1&&st<=15000/50+2,"Goodix step enrol: no finger for 15 s fails (300 polls of 50 ms)"); gx_hold=0; gh=gt=0; }
    gx_badack=1; CK(goodixfp_verify("nvx:alice")==-1,"Goodix: ACK for the wrong command rejected"); gx_badack=0; gh=gt=0;
    CK(goodixfp_delete("nvx:alice",gx_tidv)==1&&goodixfp_verify("nvx:alice")==0,"Goodix delete one template");
    { fp_xport_t f={0,fz_out,fz_in,0}; goodixfp_open(&f); int bad=0; for(int it=0;it<20000;it++){ fz_n=rand()%300; for(int i=0;i<fz_n;i++)fz[i]=(u8)rand(); int r=goodixfp_verify("nvx:z"); if(r>1)bad=1; } CK(!bad,"Goodix: 20000 random replies handled without overruns"); }
    printf(fails?"%d FAILED of %d\n":"ALL PASS (%d checks)\n",fails?fails:checks,checks); return fails!=0; }
