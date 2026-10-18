#include "include/kapi.h"
#include "include/drivers.h"
#include "drivers/sound/ring.h"
#define NBUF 32
#define BSZ 4096
#define MAXW 128
static volatile u8* R;
static u32* corb; static u64* rirb; static int corbn, rirbn; static u16 rirb_rp;
static int codec=-1, afg, osd, running;
static u8* pcm; static u64* bdl; static struct sring ring;
static u8 wtype[MAXW]; static u32 wcaps[MAXW]; static u8 conn[MAXW][16]; static u8 nconn[MAXW];
static int npaths; static u8 path[4][8]; static u8 plen[4]; static u8 pidx[4][8]; static u8 ppin[4]; static u8 pdev[4];
static char info[160];
static int isd=-1, irun=0, nin=0, in_adc=-1, in_pin=-1, in_dev=0, in_conn=0; static u8 in_path[8], in_idx[8]; static int in_len=0; static u8* ipcm; static u64* ibdl; static u32 in_rd=0; static char in_name[48];
static u8 r8(int o){ return R[o]; } static u16 r16(int o){ return *(volatile u16*)(R+o); } static u32 r32(int o){ return *(volatile u32*)(R+o); }
static void w8(int o,u8 v){ R[o]=v; } static void w16(int o,u16 v){ *(volatile u16*)(R+o)=v; } static void w32(int o,u32 v){ *(volatile u32*)(R+o)=v; }
static int verb(int nid,u32 v,u32* resp){
    u16 wp=(u16)((r16(0x48)+1)%corbn);
    corb[wp]=((u32)codec<<28)|((u32)nid<<20)|(v&0xFFFFF);
    kapi_mb(); w16(0x48,wp);
    for(int t=0;t<20000;t++){ u16 w=(u16)(r16(0x58)&0xFF); if(w!=rirb_rp){ rirb_rp=(u16)((rirb_rp+1)%rirbn); u64 e=rirb[rirb_rp]; if(resp)*resp=(u32)e; w8(0x5D,0x05); return 0; } kapi_delay_us(10); }
    return -1;
}
static u32 param(int nid,int p){ u32 r=0; if(verb(nid,0xF0000|(u32)p,&r))return 0; return r; }
static u32 cmd(int nid,int v,int payload){ u32 r=0; verb(nid,((u32)v<<8)|(u32)payload,&r); return r; }
static u32 cmd16(int nid,int v4,int payload){ u32 r=0; verb(nid,((u32)v4<<16)|(u32)payload,&r); return r; }
static int dfs(int nid,int depth,u8* p,u8* idx,int* len){
    if(depth>=8||nid<=0||nid>=MAXW)return 0;
    p[depth]=(u8)nid; *len=depth+1;
    if(wtype[nid]==0)return 1;
    if(depth>0&&wtype[nid]!=2&&wtype[nid]!=3)return 0;
    for(int i=0;i<nconn[nid];i++){ idx[depth]=(u8)i; if(dfs(conn[nid][i],depth+1,p,idx,len))return 1; }
    return 0;
}
static void scan(void){
    u32 sub=param(0,4); int start=(sub>>16)&0xFF, cnt=sub&0xFF; afg=-1;
    for(int n=start;n<start+cnt;n++){ if((param(n,5)&0xFF)==1){ afg=n; break; } }
    if(afg<0)return;
    cmd(afg,0x705,0); kapi_delay_us(10000);
    sub=param(afg,4); start=(sub>>16)&0xFF; cnt=sub&0xFF;
    for(int n=start;n<start+cnt&&n<MAXW;n++){
        u32 c=param(n,9); wcaps[n]=c; wtype[n]=(u8)((c>>20)&0xF); nconn[n]=0;
        if(c&(1u<<8)){ u32 cl=param(n,0x0E); int L=cl&0x7F; int lf=(cl>>7)&1; if(lf)L=0; if(L>16)L=16;
            for(int i=0;i<L;i+=4){ u32 e=cmd(n,0xF02,i); for(int k=0;k<4&&i+k<L;k++){ u8 x=(u8)((e>>(8*k))&0x7F); conn[n][nconn[n]++]=x; } } }
        if(wtype[n]!=0)cmd(n,0x705,0); }
    npaths=0;
    for(int pass=0;pass<3&&npaths<4;pass++)
    for(int n=start;n<start+cnt&&n<MAXW&&npaths<4;n++){
        if(wtype[n]!=4)continue;
        u32 pc=param(n,0x0C); if(!(pc&(1u<<4)))continue;
        u32 dc=cmd(n,0xF1C,0); int conn_t=(dc>>30)&3, dev=(dc>>20)&0xF;
        if(conn_t==1)continue;
        int want=pass==0?1:pass==1?2:0; if(dev!=want)continue;
        u8 p[8]={0},ix[8]={0}; int len=0; if(!dfs(n,0,p,ix,&len))continue;
        for(int k=0;k<len;k++){ path[npaths][k]=p[k]; pidx[npaths][k]=ix[k]; } plen[npaths]=(u8)len; ppin[npaths]=(u8)n; pdev[npaths]=(u8)dev; npaths++; }
}
static int dfs_in(int nid,int depth,int target,u8* p,u8* idx,int* len){
    if(depth>=8||nid<=0||nid>=MAXW)return 0;
    p[depth]=(u8)nid; *len=depth+1;
    if(nid==target)return 1;
    if(depth>0&&wtype[nid]!=2&&wtype[nid]!=3)return 0;
    for(int i=0;i<nconn[nid];i++){ idx[depth]=(u8)i; if(dfs_in(conn[nid][i],depth+1,target,p,idx,len))return 1; }
    return 0; }
static void scan_in(void){
    u32 sub=param(afg,4); int start=(sub>>16)&0xFF, cnt=sub&0xFF; in_adc=-1; in_pin=-1; in_len=0;
    int best=-1, bscore=-1;
    for(int n=start;n<start+cnt&&n<MAXW;n++){ if(wtype[n]!=4)continue; u32 pc=param(n,0x0C); if(!(pc&(1u<<5)))continue;
        u32 dc=cmd(n,0xF1C,0); int ct=(dc>>30)&3, dev=(dc>>20)&0xF; if(ct==1)continue;
        int sc=-1; if(dev==0xA&&ct==2)sc=30; else if(dev==0xA)sc=20; else if(dev==0x8)sc=10; if(sc<0)continue;
        for(int a=start;a<start+cnt&&a<MAXW;a++){ if(wtype[a]!=1)continue; if(wcaps[a]&(1u<<9))continue; u8 p[8]={0},ix[8]={0}; int len=0;
            if(dfs_in(a,0,n,p,ix,&len)){ int s2=sc*10-len; if(s2>bscore){ bscore=s2; best=n; in_adc=a; in_len=len; for(int k=0;k<len;k++){ in_path[k]=p[k]; in_idx[k]=ix[k]; } in_dev=dev; in_conn=ct; } break; } } }
    in_pin=best;
    if(in_pin>=0){ char* o=in_name; const char* t=in_dev==0xA?(in_conn==2?"Built-in microphone":"Microphone jack"):"Line in"; while(*t)*o++=*t++; *o=0; } }
static void amp_unmute(int nid,int out,int index){
    u32 c=param(nid,out?0x12:0x0D); if(!c)c=param(afg,out?0x12:0x0D);
    int steps=(c>>8)&0x7F, off=c&0x7F; int g=off; if(g>steps)g=steps;
    u32 p=(out?0x8000u:0x4000u)|0x3000u|((u32)index<<8)|(u32)g;
    cmd16(nid,0x3,(int)p);
}
static void route(void){
    for(int i=0;i<npaths;i++){
        for(int k=0;k<plen[i];k++){
            int nid=path[i][k];
            if(wcaps[nid]&(1u<<2))amp_unmute(nid,1,0);
            if(k+1<plen[i]){ if(nconn[nid]>1&&wtype[nid]!=2)cmd(nid,0x701,pidx[i][k]); if((wcaps[nid]&(1u<<1))&&wtype[nid]==2)amp_unmute(nid,0,pidx[i][k]); }
            if(wtype[nid]==0){ cmd16(nid,0x2,0x0011); cmd(nid,0x706,0x10); } }
        int pin=ppin[i];
        cmd(pin,0x707,0x40|(pdev[i]==2?0x80:0));
        if(param(pin,0x0C)&(1u<<16))cmd(pin,0x70C,0x02);
    }
}
static u32 sd(int r){ return (u32)(0x80+osd*0x20+r); }
static u32 si(int r){ return (u32)(0x80+isd*0x20+r); }
static int in_gain_pct=70;
static void in_amp(int nid,int out,int index,int pct){
    u32 c=param(nid,out?0x12:0x0D); if(!c)c=param(afg,out?0x12:0x0D);
    int steps=(c>>8)&0x7F, off=c&0x7F; int g; if(pct<=50)g=off*pct/50; else g=off+(steps-off)*(pct-50)/50; if(g>steps)g=steps; if(g<0)g=0;
    cmd16(nid,0x3,(int)((out?0x8000u:0x4000u)|0x3000u|((u32)index<<8)|(u32)g)); }
static void hda_rec_gain(int pct){
    if(pct<0)pct=0; if(pct>100)pct=100; in_gain_pct=pct; if(in_pin<0)return;
    for(int k=0;k<in_len;k++){ int nid=in_path[k];
        if((wtype[nid]==1||wtype[nid]==2)&&(wcaps[nid]&(1u<<1))&&k+1<in_len) in_amp(nid,0,in_idx[k],pct);
        if(wtype[nid]==3&&(wcaps[nid]&(1u<<2))) in_amp(nid,1,0,pct);
        if(wtype[nid]==4&&(wcaps[nid]&(1u<<1))) in_amp(nid,0,0,pct>=50?pct-20:pct/2); } }
static void route_in(void){
    if(in_pin<0)return;
    for(int k=0;k<in_len;k++){ int nid=in_path[k];
        if(k+1<in_len&&nconn[nid]>1&&wtype[nid]!=2)cmd(nid,0x701,in_idx[k]); }
    u32 pc=param(in_pin,0x0C); int vr=(pc>>8)&0xFF; int vref=(vr&0x10)?4:(vr&0x02)?1:(vr&0x20)?5:0;
    cmd(in_pin,0x707,0x20|(in_dev==0xA?vref:0));
    cmd16(in_adc,0x2,0x0011); cmd(in_adc,0x706,0x20);
    hda_rec_gain(in_gain_pct); }
static u32 ihw(void){ return r32((int)si(0x04)); }
static int hda_rec_start(void){
    if(irun)return 1; if(isd<0||in_pin<0||!ipcm||!ibdl)return 0;
    route_in();
    w8((int)si(0),1); for(int t=0;t<1000&&!(r8((int)si(0))&1);t++)kapi_delay_us(10);
    w8((int)si(0),0); for(int t=0;t<1000&&(r8((int)si(0))&1);t++)kapi_delay_us(10);
    for(int i=0;i<NBUF;i++){ ibdl[i*2]=kapi_phys(ipcm+i*BSZ); ibdl[i*2+1]=(u64)BSZ; }
    for(int i=0;i<NBUF*BSZ;i++)ipcm[i]=0;
    kapi_mb();
    w32((int)si(0x18),(u32)kapi_phys(ibdl)); w32((int)si(0x1C),(u32)(kapi_phys(ibdl)>>32));
    w32((int)si(0x08),NBUF*BSZ); w16((int)si(0x0C),NBUF-1); w16((int)si(0x12),0x0011);
    w8((int)si(2),0x20); w8((int)si(3),0x1C); w8((int)si(0),0x02);
    in_rd=0; irun=1; return 1; }
static int hda_rec_read(u8* d,int len){
    if(!irun)return 0; u32 sz=NBUF*BSZ; u32 hw=ihw()%sz; u32 av=(hw+sz-in_rd)%sz; av&=~3u; if((u32)len>av)len=(int)av; len&=~3;
    for(int i=0;i<len;i++)d[i]=ipcm[(in_rd+(u32)i)%sz];
    in_rd=(in_rd+(u32)len)%sz; return len; }
static void hda_rec_stop(void){ if(!irun)return; w8((int)si(0),0); irun=0; }
static u32 hwpos(void){ return r32((int)sd(0x04)); }
static void hda_poll(void){ if(running)sring_hw(&ring,hwpos()%(NBUF*BSZ)); }
static int hda_start(void){
    if(running)return 1;
    w8((int)sd(0),1); for(int t=0;t<1000&&!(r8((int)sd(0))&1);t++)kapi_delay_us(10);
    w8((int)sd(0),0); for(int t=0;t<1000&&(r8((int)sd(0))&1);t++)kapi_delay_us(10);
    for(int i=0;i<NBUF;i++){ bdl[i*2]=kapi_phys(pcm+i*BSZ); bdl[i*2+1]=(u64)BSZ; }
    sring_init(&ring,pcm,NBUF*BSZ);
    kapi_mb();
    w32((int)sd(0x18),(u32)kapi_phys(bdl)); w32((int)sd(0x1C),(u32)(kapi_phys(bdl)>>32));
    w32((int)sd(0x08),NBUF*BSZ); w16((int)sd(0x0C),NBUF-1); w16((int)sd(0x12),0x0011);
    w8((int)sd(2),0x10);
    w8((int)sd(3),0x1C);
    w8((int)sd(0),0x02);
    running=1; return 1;
}
static int hda_free(void){ hda_poll(); return running?sring_free(&ring):0; }
static int hda_write(const u8* d,int len){ hda_poll(); return running?sring_write(&ring,d,len):0; }
static void hda_stop(void){ if(!running)return; w8((int)sd(0),0); running=0; }
static void hda_volume(int pct){
    if(pct<0)pct=0; if(pct>100)pct=100;
    for(int i=0;i<npaths;i++) for(int k=0;k<plen[i];k++){ int nid=path[i][k]; if(!(wcaps[nid]&(1u<<2)))continue;
        u32 c=param(nid,0x12); if(!c)c=param(afg,0x12); int steps=(c>>8)&0x7F, off=c&0x7F; if(!steps&&!off)continue;
        int g=pct==0?0:off*pct/100; if(g>steps)g=steps; cmd16(nid,0x3,(int)(0xB000u|(pct==0?0x80u:0u)|(u32)g)); }
}
const char* hda_info(void){ return info; }
static char* ap(char* o,const char* s){ while(*s)*o++=*s++; *o=0; return o; }
static char* apn(char* o,u32 v,int base){ char t[12]; int n=0; if(!v)t[n++]='0'; while(v){ u32 d=v%(u32)base; t[n++]=(char)(d<10?'0'+d:'a'+d-10); v/=(u32)base; } while(n)*o++=t[--n]; *o=0; return o; }
int hda_probe(struct nvx_snd* out){
    codec=-1; afg=-1; npaths=0; running=0; info[0]=0;
    int b,d,f; if(!kapi_pci_find_class(0x04,0x03,0,&b,&d,&f))return 0;
    u32 lo=kapi_pci_rd32(b,d,f,0x10); if(lo&1)return 0;
    u64 bar=lo&0xFFFFFFF0u; if(((lo>>1)&3)==2)bar|=(u64)kapi_pci_rd32(b,d,f,0x14)<<32;
    if(!bar||bar>=0x100000000ULL){ kapi_log("hda: BAR above 4 GB, not mapped"); return 0; }
    kapi_pci_enable(b,d,f);
    R=(volatile u8*)(u64)bar;
    w32(0x08,r32(0x08)&~1u); for(int t=0;t<1000&&(r32(0x08)&1);t++)kapi_delay_us(10);
    kapi_delay_us(100);
    w32(0x08,r32(0x08)|1u); int ok=0; for(int t=0;t<1000;t++){ if(r32(0x08)&1){ ok=1; break; } kapi_delay_us(10); }
    if(!ok){ kapi_log("hda: controller reset failed"); return 0; }
    kapi_delay_us(1000);
    u16 st=r16(0x0E); for(int i=0;i<15;i++) if(st&(1u<<i)){ codec=i; break; }
    if(codec<0){ kapi_log("hda: no codec"); return 0; }
    u16 gcap=r16(0x00); int iss=(gcap>>8)&0xF, oss=(gcap>>12)&0xF; if(!oss){ kapi_log("hda: no output streams"); return 0; }
    osd=iss; isd=iss?0:-1; nin=iss;
    corb=(u32*)kapi_dma_pages(1); rirb=(u64*)kapi_dma_pages(1); bdl=(u64*)kapi_dma_pages(1); pcm=(u8*)kapi_dma_pages(NBUF*BSZ/4096);
    if(!corb||!rirb||!bdl||!pcm)return 0;
    w8(0x4C,0); w8(0x5C,0); kapi_delay_us(100);
    u8 cs=r8(0x4E); corbn=(cs&0x40)?256:(cs&0x20)?16:2; w8(0x4E,(u8)((cs&0xFC)|(corbn==256?2:corbn==16?1:0)));
    u8 rs=r8(0x5E); rirbn=(rs&0x40)?256:(rs&0x20)?16:2; w8(0x5E,(u8)((rs&0xFC)|(rirbn==256?2:rirbn==16?1:0)));
    w32(0x40,(u32)kapi_phys(corb)); w32(0x44,(u32)(kapi_phys(corb)>>32));
    w32(0x50,(u32)kapi_phys(rirb)); w32(0x54,(u32)(kapi_phys(rirb)>>32));
    w16(0x4A,0x8000); for(int t=0;t<1000&&!(r16(0x4A)&0x8000);t++)kapi_delay_us(10);
    w16(0x4A,0); for(int t=0;t<1000&&(r16(0x4A)&0x8000);t++)kapi_delay_us(10);
    w16(0x48,0);
    w16(0x58,0x8000); rirb_rp=0; w16(0x5A,1);
    w8(0x4C,0x02); w8(0x5C,0x03);
    u32 vid=param(0,0);
    if(!vid||vid==0xFFFFFFFFu){ kapi_log("hda: codec does not answer"); return 0; }
    scan();
    if(afg<0||!npaths){ char b[160],*o=b; o=ap(o,"hda: no usable output path (afg "); o=apn(o,(u32)(afg<0?255:afg),10); o=ap(o," root4="); o=apn(o,param(0,4),16); o=ap(o," n1p5="); o=apn(o,param(1,5),16); o=ap(o," vid="); o=apn(o,param(0,0),16); o=ap(o," widgets"); for(int n=1;n<MAXW&&n<12;n++){ if(!wcaps[n])continue; o=ap(o," "); o=apn(o,(u32)n,10); o=ap(o,":t"); o=apn(o,wtype[n],10); o=ap(o,"/c"); o=apn(o,nconn[n],10); } *o=0; kapi_log(b); return 0; }
    route(); hda_volume(80);
    if(isd>=0){ scan_in(); if(in_pin>=0){ ibdl=(u64*)kapi_dma_pages(1); ipcm=(u8*)kapi_dma_pages(NBUF*BSZ/4096); if(!ibdl||!ipcm)in_pin=-1; } }
    char* o=info; o=ap(o,"HD Audio codec "); o=apn(o,vid>>16,16); o=ap(o,":"); o=apn(o,vid&0xFFFF,16); o=ap(o,", outputs:");
    for(int i=0;i<npaths;i++){ o=ap(o," "); o=ap(o,pdev[i]==1?"speaker":pdev[i]==2?"headphones":"line-out"); o=ap(o,"(pin "); o=apn(o,ppin[i],16); o=ap(o," dac "); o=apn(o,path[i][plen[i]-1],16); o=ap(o,")"); }
    running=0;
    if(in_pin>=0){ o=ap(o,", input: "); o=ap(o,in_name); o=ap(o," (pin "); o=apn(o,(u32)in_pin,16); o=ap(o," adc "); o=apn(o,(u32)in_adc,16); o=ap(o,")"); out->rec_start=hda_rec_start; out->rec_read=hda_rec_read; out->rec_stop=hda_rec_stop; out->rec_gain=hda_rec_gain; out->rec_name=in_name; }
    else { o=ap(o,", no microphone input"); out->rec_start=0; out->rec_read=0; out->rec_stop=0; out->rec_gain=0; out->rec_name=0; }
    out->name="Intel HD Audio"; out->rate=48000; out->start=hda_start; out->free_bytes=hda_free; out->write=hda_write; out->poll=hda_poll; out->stop=hda_stop; out->volume=hda_volume;
    kapi_log(info);
    return 1;
}
