#include "include/nvx/base.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/fs/vfat.h"
#include "include/nvx/fs/ntfsk.h"
typedef struct { char name[256]; u8 sname[11]; u8 attr; u32 clus; u32 size; u32 sec; int off; int first_idx, idx; } vf_ent;
struct nvx_g_vfat_t g_vfat;
static u8 g_vfat_sec[512]; static u32 g_vfat_secno=0xFFFFFFFFu;
static int vf_rs(u32 rel,int n,void* b){ return g_vfat.rd(g_vfat.base+rel,n,b); }
static int vf_ws(u32 rel,int n,const void* b){ if(g_vfat.ro||!g_vfat.wr)return -1; return g_vfat.wr(g_vfat.base+rel,n,b); }
static u16 vf_r16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 vf_r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void vf_w16(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void vf_w32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static void vf_zero(void* p,u32 n){ u8* q=(u8*)p; while(n--)*q++=0; }
static void vf_copy(void* d,const void* s,u32 n){ u8* a=(u8*)d; const u8* b=(const u8*)s; while(n--)*a++=*b++; }
static u32 vf_eoc(void){ return g_vfat.type==32?0x0FFFFFF8u:0xFFF8u; }
static u32 vf_get(u32 c){ if(g_vfat.type==32)return vf_r32(g_vfat.fat+c*4)&0x0FFFFFFFu; return vf_r16(g_vfat.fat+c*2); }
static void vf_set(u32 c,u32 v){
    if(g_vfat.type==32){ u32 o=vf_r32(g_vfat.fat+c*4); vf_w32(g_vfat.fat+c*4,(o&0xF0000000u)|(v&0x0FFFFFFFu)); g_vfat.fdirty[(c*4)/512]=1; }
    else { vf_w16(g_vfat.fat+c*2,v&0xFFFF); g_vfat.fdirty[(c*2)/512]=1; }
}
static int vf_valid(u32 c){ return c>=2&&c<g_vfat.clusters+2; }
static int vf_iseoc(u32 v){ return v>=vf_eoc(); }
static u32 vf_c2s(u32 c){ return g_vfat.data_lba+(c-2)*g_vfat.spc; }
static int vf_flush(void){
    if(g_vfat.ro)return 0;
    for(u32 s=0;s<g_vfat.fatsz;s++) if(g_vfat.fdirty[s]){ for(u32 f=0;f<g_vfat.nfats;f++) if(vf_ws(g_vfat.rsvd+f*g_vfat.fatsz+s,1,g_vfat.fat+s*512))return -1; g_vfat.fdirty[s]=0; }
    if(g_vfat.type==32&&g_vfat.fsinfo){ u8 b[512]; if(vf_rs(g_vfat.fsinfo,1,b)==0&&vf_r32(b)==0x41615252u){ vf_w32(b+488,g_vfat.free_cnt); vf_w32(b+492,g_vfat.next_free); vf_ws(g_vfat.fsinfo,1,b); } }
    return 0;
}
static int vf_rsec(u32 rel){ if(g_vfat_secno==rel)return 0; if(vf_rs(rel,1,g_vfat_sec)){ g_vfat_secno=0xFFFFFFFFu; return -1; } g_vfat_secno=rel; return 0; }
static int vf_wsec(u32 rel){ if(vf_ws(rel,1,g_vfat_sec))return -1; g_vfat_secno=rel; return 0; }
static u32 vf_alloc1(u32 prev){
    if(!g_vfat.free_cnt)return 0;
    u32 n=g_vfat.clusters, s=g_vfat.next_free; if(!vf_valid(s))s=2;
    for(u32 i=0;i<n;i++){ u32 c=2+((s-2+i)%n); if(vf_get(c)==0){ vf_set(c,0x0FFFFFFFu); if(prev)vf_set(prev,c); g_vfat.free_cnt--; g_vfat.next_free=c+1; return c; } }
    return 0;
}
static void vf_freechain(u32 c){ int g=0; while(vf_valid(c)&&g++<0x400000){ u32 n=vf_get(c); vf_set(c,0); g_vfat.free_cnt++; if(vf_iseoc(n)||n<2)break; c=n; } }
static int vf_zclus(u32 c){ vf_zero(g_vfat.cbuf,g_vfat.cbytes); if(vf_ws(vf_c2s(c),(int)g_vfat.spc,g_vfat.cbuf))return -1; if(g_vfat_secno>=vf_c2s(c)&&g_vfat_secno<vf_c2s(c)+g_vfat.spc)g_vfat_secno=0xFFFFFFFFu; return 0; }
static int vf_loc(u32 dc,int idx,u32* sec,int* off){
    if(dc==0){ if((u32)idx>=g_vfat.rootent)return -1; *sec=g_vfat.root_lba+(u32)idx/16; *off=(idx%16)*32; return 0; }
    u32 epc=g_vfat.cbytes/32, step=(u32)idx/epc, c=dc;
    for(u32 i=0;i<step;i++){ u32 n=vf_get(c); if(!vf_valid(n))return -1; c=n; }
    u32 e=(u32)idx%epc; *sec=vf_c2s(c)+e/16; *off=(int)(e%16)*32; return 0;
}
static u8 vf_cksum(const u8* s){ u8 x=0; for(int i=0;i<11;i++)x=(u8)(((x&1)?0x80:0)+(x>>1)+s[i]); return x; }
static int vf_put_utf8(char* o,int n,u32 c){
    if(c<0x80){ o[n++]=(char)c; } else if(c<0x800){ o[n++]=(char)(0xC0|(c>>6)); o[n++]=(char)(0x80|(c&0x3F)); }
    else if(c<0x10000){ o[n++]=(char)(0xE0|(c>>12)); o[n++]=(char)(0x80|((c>>6)&0x3F)); o[n++]=(char)(0x80|(c&0x3F)); }
    else { o[n++]=(char)(0xF0|(c>>18)); o[n++]=(char)(0x80|((c>>12)&0x3F)); o[n++]=(char)(0x80|((c>>6)&0x3F)); o[n++]=(char)(0x80|(c&0x3F)); }
    return n;
}
static int vf_utf16(const char* s,u16* o,int cap){
    const u8* u=(const u8*)s; int n=0;
    while(*u&&n<cap){ u32 c; if(*u<0x80)c=*u++; else if((*u&0xE0)==0xC0&&u[1]){ c=((u32)(u[0]&0x1F)<<6)|(u[1]&0x3F); u+=2; }
        else if((*u&0xF0)==0xE0&&u[1]&&u[2]){ c=((u32)(u[0]&0x0F)<<12)|((u32)(u[1]&0x3F)<<6)|(u[2]&0x3F); u+=3; }
        else if((*u&0xF8)==0xF0&&u[1]&&u[2]&&u[3]){ c=((u32)(u[0]&0x07)<<18)|((u32)(u[1]&0x3F)<<12)|((u32)(u[2]&0x3F)<<6)|(u[3]&0x3F); u+=4; }
        else { u++; c='_'; }
        if(c>=0x10000){ if(n+1>=cap)break; c-=0x10000; o[n++]=(u16)(0xD800|(c>>10)); o[n++]=(u16)(0xDC00|(c&0x3FF)); } else o[n++]=(u16)c; }
    return n;
}
static void vf_shortname(const u8* sn,u8 nt,char* out){
    int n=0; for(int i=0;i<8&&sn[i]!=' ';i++){ char c=(char)(i==0&&sn[0]==0x05?0xE5:sn[i]); if((nt&0x08)&&c>='A'&&c<='Z')c+=32; out[n++]=c; }
    if(sn[8]!=' '){ out[n++]='.'; for(int i=8;i<11&&sn[i]!=' ';i++){ char c=(char)sn[i]; if((nt&0x10)&&c>='A'&&c<='Z')c+=32; out[n++]=c; } }
    out[n]=0;
}
static int vf_next(u32 dc,int* idx,vf_ent* e){
    u16 lfn[260]; int lnum=0, lchk=-1, lfirst=-1, lgot=0;
    for(;;){ u32 sec; int off; if(vf_loc(dc,*idx,&sec,&off))return 0; if(vf_rsec(sec))return -1;
        u8* d=g_vfat_sec+off; int me=*idx; (*idx)++;
        if(d[0]==0)return 0;
        if(d[0]==0xE5){ lnum=0; continue; }
        if((d[11]&0x3F)==0x0F){ int ord=d[0]&0x1F;
            if(d[0]&0x40){ lnum=ord; lchk=d[13]; lfirst=me; lgot=0; for(int i=0;i<260;i++)lfn[i]=0xFFFF; }
            else if(!lnum||d[13]!=lchk){ lnum=0; continue; }
            if(ord<1||ord>20){ lnum=0; continue; }
            static const int pos[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
            for(int i=0;i<13;i++) lfn[(ord-1)*13+i]=vf_r16(d+pos[i]); lgot++; continue; }
        if(d[11]&0x08){ lnum=0; continue; }
        e->attr=d[11]; e->clus=((u32)vf_r16(d+20)<<16)|vf_r16(d+26); if(g_vfat.type!=32)e->clus&=0xFFFF; e->size=vf_r32(d+28);
        vf_copy(e->sname,d,11); e->sec=sec; e->off=off; e->idx=me;
        if(lnum&&lgot==lnum&&vf_cksum(d)==lchk){ int n=0; for(int i=0;i<lnum*13&&lfn[i]&&lfn[i]!=0xFFFF&&n<250;i++){ u32 c=lfn[i];
                if(c>=0xD800&&c<0xDC00&&i+1<lnum*13){ c=0x10000+((c-0xD800)<<10)+(lfn[i+1]-0xDC00); i++; } n=vf_put_utf8(e->name,n,c); } e->name[n]=0; e->first_idx=lfirst; }
        else { vf_shortname(d,d[12],e->name); e->first_idx=me; }
        return 1; }
}
static int vf_ieq(const char* a,const char* b){ while(*a&&*b){ char x=*a,y=*b; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y)return 0; a++; b++; } return *a==*b; }
static u32 vf_rootdc(void){ return g_vfat.type==32?g_vfat.rootclus:0; }
static int vf_find(u32 dc,const char* name,vf_ent* e){ int i=0; for(;;){ int r=vf_next(dc,&i,e); if(r<=0)return -1; if(vf_ieq(e->name,name))return 0; } }
static int vf_walk(const char* path,vf_ent* e,u32* parent,char* leaf){
    u32 dc=vf_rootdc(); const char* p=path; while(*p=='/')p++;
    if(!*p){ if(parent)*parent=dc; if(leaf)leaf[0]=0; return 1; }
    for(;;){ char comp[256]; int n=0; while(*p&&*p!='/'&&n<255)comp[n++]=*p++; comp[n]=0; while(*p=='/')p++;
        if(parent)*parent=dc; if(leaf){ int i=0; for(;comp[i];i++)leaf[i]=comp[i]; leaf[i]=0; }
        if(vf_find(dc,comp,e))return *p?-2:-1;
        if(!*p)return 0;
        if(!(e->attr&0x10))return -2;
        dc=e->clus; if(dc==0)dc=vf_rootdc(); }
}
static void vf_now(u16* t,u16* d){ int hh=12,mm=0,ss=0,yr=2026,mo=1,dy=1; vf_clock(&hh,&mm,&ss,&yr,&mo,&dy); if(yr<1980)yr=1980;
    *t=(u16)((hh<<11)|(mm<<5)|(ss/2)); *d=(u16)(((yr-1980)<<9)|(mo<<5)|dy); }
static int vf_legal(char c){ if(c>='A'&&c<='Z')return 1; if(c>='0'&&c<='9')return 1; const char* s="$%'-_@~`!(){}^#&"; for(;*s;s++) if(*s==c)return 1; return 0; }
static int vf_make83(const char* name,u8* sn,u8* nt){
    int L=0; while(name[L])L++; if(L==0||L>12)return 0;
    int dot=-1; for(int i=0;i<L;i++) if(name[i]=='.'){ if(dot>=0)return 0; dot=i; }
    int bl=dot<0?L:dot, el=dot<0?0:L-dot-1; if(bl<1||bl>8||el>3||(dot>=0&&el==0))return 0;
    int bu=0,bl2=0,eu=0,el2=0; for(int i=0;i<11;i++)sn[i]=' ';
    for(int i=0;i<bl;i++){ char c=name[i]; if(c>='a'&&c<='z'){ bl2=1; c-=32; } else if(c>='A'&&c<='Z')bu=1; if(!vf_legal(c))return 0; sn[i]=(u8)c; }
    for(int i=0;i<el;i++){ char c=name[dot+1+i]; if(c>='a'&&c<='z'){ el2=1; c-=32; } else if(c>='A'&&c<='Z')eu=1; if(!vf_legal(c))return 0; sn[8+i]=(u8)c; }
    if((bu&&bl2)||(eu&&el2))return 0;
    *nt=(u8)((bl2?0x08:0)|(el2?0x10:0)); if(sn[0]==0xE5)sn[0]=0x05; return 1;
}
static int vf_snexists(u32 dc,const u8* sn){ int i=0; vf_ent e; for(;;){ int r=vf_next(dc,&i,&e); if(r<=0)return 0; int eq=1; for(int k=0;k<11;k++) if(e.sname[k]!=sn[k]){ eq=0; break; } if(eq)return 1; } }
static void vf_alias(u32 dc,const char* name,u8* sn){
    char base[16]; int bn=0; char ext[4]; int en=0; int L=0; while(name[L])L++; int dot=-1; for(int i=L-1;i>0;i--) if(name[i]=='.'){ dot=i; break; }
    for(int i=0;i<(dot<0?L:dot)&&bn<8;i++){ char c=name[i]; if(c==' '||c=='.')continue; if(c>='a'&&c<='z')c-=32; if(!vf_legal(c))c='_'; base[bn++]=c; }
    if(dot>=0) for(int i=dot+1;i<L&&en<3;i++){ char c=name[i]; if(c==' '||c=='.')continue; if(c>='a'&&c<='z')c-=32; if(!vf_legal(c))c='_'; ext[en++]=c; }
    if(!bn)base[bn++]='_';
    for(u32 n=1;n<1000000;n++){ char num[8]; int nl=0; u32 v=n; char t[8]; int tl=0; while(v){ t[tl++]=(char)('0'+v%10); v/=10; } num[nl++]='~'; while(tl)num[nl++]=t[--tl];
        int keep=bn; if(keep+nl>8)keep=8-nl; for(int i=0;i<11;i++)sn[i]=' '; for(int i=0;i<keep;i++)sn[i]=(u8)base[i]; for(int i=0;i<nl;i++)sn[keep+i]=(u8)num[i]; for(int i=0;i<en;i++)sn[8+i]=(u8)ext[i];
        if(!vf_snexists(dc,sn))return; }
}
static int vf_slot(u32 dc,int need){
    int i=0, run=0, start=0;
    for(;;){ u32 sec; int off;
        if(vf_loc(dc,i,&sec,&off)){
            if(dc==0)return -1;
            u32 c=dc,g=0; while(g++<0x400000){ u32 n=vf_get(c); if(!vf_valid(n))break; c=n; }
            u32 nc=vf_alloc1(c); if(!nc)return -1; if(vf_zclus(nc))return -1; continue; }
        if(vf_rsec(sec))return -1; u8 b=g_vfat_sec[off];
        if(b==0||b==0xE5){ if(!run)start=i; run++; if(run>=need)return start; } else run=0;
        i++; }
}
static int vf_mkent(u32 dc,const char* name,u8 attr,u32 clus,u32 size){
    u8 sn[11], nt=0; u16 u[256]; int ul=0, nl=0;
    if(!vf_make83(name,sn,&nt)){ ul=vf_utf16(name,u,255); if(ul<1)return -1; nl=(ul+12)/13; vf_alias(dc,name,sn); nt=0; }
    int st=vf_slot(dc,nl+1); if(st<0)return -1;
    u8 ck=vf_cksum(sn); static const int pos[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for(int k=0;k<nl;k++){ int ord=nl-k; u32 sec; int off; if(vf_loc(dc,st+k,&sec,&off)||vf_rsec(sec))return -1; u8* d=g_vfat_sec+off; vf_zero(d,32);
        d[0]=(u8)(ord|(k==0?0x40:0)); d[11]=0x0F; d[13]=ck;
        for(int i=0;i<13;i++){ int ci=(ord-1)*13+i; u16 v=ci<ul?u[ci]:(ci==ul?0:0xFFFF); vf_w16(d+pos[i],v); }
        if(vf_wsec(sec))return -1; }
    u32 sec; int off; if(vf_loc(dc,st+nl,&sec,&off)||vf_rsec(sec))return -1; u8* d=g_vfat_sec+off; vf_zero(d,32);
    vf_copy(d,sn,11); d[11]=attr; d[12]=nt; u16 t,dt; vf_now(&t,&dt); vf_w16(d+14,t); vf_w16(d+16,dt); vf_w16(d+18,dt);
    vf_w16(d+20,clus>>16); vf_w16(d+22,t); vf_w16(d+24,dt); vf_w16(d+26,clus&0xFFFF); vf_w32(d+28,size);
    return vf_wsec(sec);
}
static int vf_setent(const vf_ent* e,u32 clus,u32 size){ if(vf_rsec(e->sec))return -1; u8* d=g_vfat_sec+e->off; u16 t,dt; vf_now(&t,&dt);
    vf_w16(d+20,clus>>16); vf_w16(d+26,clus&0xFFFF); vf_w32(d+28,size); vf_w16(d+22,t); vf_w16(d+24,dt); vf_w16(d+18,dt); return vf_wsec(e->sec); }
static int vf_killent(u32 dc,const vf_ent* e){ for(int i=e->first_idx;i<=e->idx;i++){ u32 sec; int off; if(vf_loc(dc,i,&sec,&off)||vf_rsec(sec))return -1; g_vfat_sec[off]=0xE5; if(vf_wsec(sec))return -1; } return 0; }
static u32 vf_writechain(const char* buf,long len){
    if(len<=0)return 0; u32 n=(u32)((len+g_vfat.cbytes-1)/g_vfat.cbytes); if(n>g_vfat.free_cnt)return 0xFFFFFFFFu;
    u32 first=0, prev=0; long done=0;
    for(u32 i=0;i<n;i++){ u32 c=vf_alloc1(prev); if(!c){ if(first)vf_freechain(first); return 0xFFFFFFFFu; } if(!first)first=c; prev=c;
        long k=len-done; if(k>(long)g_vfat.cbytes)k=g_vfat.cbytes; vf_zero(g_vfat.cbuf,g_vfat.cbytes); vf_copy(g_vfat.cbuf,buf+done,(u32)k);
        if(vf_ws(vf_c2s(c),(int)g_vfat.spc,g_vfat.cbuf)){ vf_freechain(first); return 0xFFFFFFFFu; } done+=k; }
    if(g_vfat_secno>=g_vfat.data_lba)g_vfat_secno=0xFFFFFFFFu;
    return first;
}
int vf_mounted(void){ return g_vfat.mounted; }
static const char* vf_label(void){ return g_vfat.label; }
void vf_umount(void){ if(!g_vfat.mounted)return; vf_flush(); if(g_vfat.fat)kfree(g_vfat.fat); if(g_vfat.fdirty)kfree(g_vfat.fdirty); if(g_vfat.cbuf)kfree(g_vfat.cbuf); g_vfat.fat=0; g_vfat.fdirty=0; g_vfat.cbuf=0; g_vfat.mounted=0; g_vfat_secno=0xFFFFFFFFu; }
int vf_mount(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,int ro){
    vf_umount(); g_vfat.rd=rd; g_vfat.wr=ro?0:wr; g_vfat.base=base; g_vfat.nsect=nsect; g_vfat.ro=ro; g_vfat_secno=0xFFFFFFFFu;
    u8 b[512]; if(vf_rs(0,1,b))return -1; if(b[510]!=0x55||b[511]!=0xAA)return -1;
    if(vf_r16(b+11)!=512)return -1; u32 spc=b[13]; if(!spc||(spc&(spc-1)))return -1;
    u32 rsvd=vf_r16(b+14), nf=b[16], re=vf_r16(b+17), ts=vf_r16(b+19); if(!ts)ts=vf_r32(b+32); u32 fz=vf_r16(b+22); if(!fz)fz=vf_r32(b+36);
    if(!rsvd||!nf||!fz||ts>nsect)return -1;
    u32 rs=(re*32+511)/512, data=ts-rsvd-nf*fz-rs, cl=data/spc;
    if(cl<4085)return -2;
    g_vfat.type=cl<65525?16:32;
    { u32 maxe=fz*512/(g_vfat.type==32?4:2); if(maxe<3)return -1; if(cl+2>maxe)cl=maxe-2; } g_vfat.spc=spc; g_vfat.rsvd=rsvd; g_vfat.nfats=nf; g_vfat.fatsz=fz; g_vfat.rootent=re; g_vfat.rootsecs=rs;
    g_vfat.root_lba=rsvd+nf*fz; g_vfat.data_lba=g_vfat.root_lba+rs; g_vfat.clusters=cl; g_vfat.cbytes=spc*512;
    g_vfat.rootclus=g_vfat.type==32?vf_r32(b+44):0; g_vfat.fsinfo=g_vfat.type==32?vf_r16(b+48):0;
    const u8* lb=b+(g_vfat.type==32?71:43); int n=11; while(n>0&&lb[n-1]==' ')n--; for(int i=0;i<n;i++)g_vfat.label[i]=(char)lb[i]; g_vfat.label[n]=0;
    if((unsigned long long)fz*512>16ULL*1024*1024)return -3;
    g_vfat.fat=(u8*)kmalloc(fz*512); g_vfat.fdirty=(u8*)kmalloc(fz); g_vfat.cbuf=(u8*)kmalloc(g_vfat.cbytes);
    if(!g_vfat.fat||!g_vfat.fdirty||!g_vfat.cbuf){ g_vfat.mounted=1; vf_umount(); return -4; }
    for(u32 s=0;s<fz;s+=8){ int k=(int)(fz-s<8?fz-s:8); if(vf_rs(rsvd+s,k,g_vfat.fat+s*512)){ g_vfat.mounted=1; vf_umount(); return -1; } }
    vf_zero(g_vfat.fdirty,fz);
    g_vfat.free_cnt=0; for(u32 c=2;c<cl+2;c++) if(vf_get(c)==0)g_vfat.free_cnt++;
    g_vfat.next_free=2; if(g_vfat.type==32&&g_vfat.fsinfo){ u8 f[512]; if(vf_rs(g_vfat.fsinfo,1,f)==0&&vf_r32(f)==0x41615252u){ u32 h=vf_r32(f+492); if(vf_valid(h))g_vfat.next_free=h; } }
    g_vfat.mounted=1; return 0;
}
int vf_exists(const char* path){ vf_ent e; if(!g_vfat.mounted)return 0; return vf_walk(path,&e,0,0)>=0; }
long vf_read(const char* path,char* buf,long cap){
    vf_ent e; if(!g_vfat.mounted||vf_walk(path,&e,0,0)!=0||(e.attr&0x10))return -1;
    long n=e.size<(u32)cap?(long)e.size:cap, got=0; u32 c=e.clus; int g=0;
    while(got<n&&vf_valid(c)&&g++<0x400000){ if(vf_rs(vf_c2s(c),(int)g_vfat.spc,g_vfat.cbuf))return -1; long k=n-got; if(k>(long)g_vfat.cbytes)k=g_vfat.cbytes; vf_copy(buf+got,g_vfat.cbuf,(u32)k); got+=k; c=vf_get(c); }
    return got;
}
long vf_write(const char* path,const char* buf,long len){
    if(!g_vfat.mounted||g_vfat.ro||len<0)return -1; vf_ent e; u32 parent; char leaf[256]; int r=vf_walk(path,&e,&parent,leaf);
    if(r==1||r==-2||(r==0&&(e.attr&0x10)))return -1;
    u32 c=vf_writechain(buf,len); if(c==0xFFFFFFFFu){ vf_flush(); return -1; }
    if(r==0){ u32 old=e.clus; if(vf_setent(&e,c,(u32)len)){ vf_flush(); return -1; } if(vf_valid(old))vf_freechain(old); }
    else if(vf_mkent(parent,leaf,0x20,c,(u32)len)){ if(c)vf_freechain(c); vf_flush(); return -1; }
    return vf_flush()?-1:len;
}
int vf_mkdir(const char* path){
    if(!g_vfat.mounted||g_vfat.ro)return -1; vf_ent e; u32 parent; char leaf[256]; int r=vf_walk(path,&e,&parent,leaf); if(r!=-1)return -1;
    u32 c=vf_alloc1(0); if(!c)return -1; if(vf_zclus(c)){ vf_freechain(c); return -1; }
    u16 t,dt; vf_now(&t,&dt); vf_zero(g_vfat.cbuf,g_vfat.cbytes);
    for(int k=0;k<2;k++){ u8* d=g_vfat.cbuf+k*32; for(int i=0;i<11;i++)d[i]=' '; d[0]='.'; if(k)d[1]='.'; d[11]=0x10; u32 cc=k?(parent==vf_rootdc()?0:parent):c;
        vf_w16(d+14,t); vf_w16(d+16,dt); vf_w16(d+18,dt); vf_w16(d+20,cc>>16); vf_w16(d+22,t); vf_w16(d+24,dt); vf_w16(d+26,cc&0xFFFF); }
    if(vf_ws(vf_c2s(c),(int)g_vfat.spc,g_vfat.cbuf)){ vf_freechain(c); vf_flush(); return -1; }
    if(vf_mkent(parent,leaf,0x10,c,0)){ vf_freechain(c); vf_flush(); return -1; }
    return vf_flush();
}
int vf_delete(const char* path){
    if(!g_vfat.mounted||g_vfat.ro)return -1; vf_ent e; u32 parent; char leaf[256]; if(vf_walk(path,&e,&parent,leaf)!=0)return -1;
    if(e.attr&0x10){ int i=0; vf_ent x; for(;;){ int r=vf_next(e.clus,&i,&x); if(r<=0)break; if(!(x.sname[0]=='.'&&(x.sname[1]==' '||(x.sname[1]=='.'&&x.sname[2]==' '))))return -1; } }
    if(vf_killent(parent,&e)){ vf_flush(); return -1; }
    if(vf_valid(e.clus))vf_freechain(e.clus);
    return vf_flush();
}
int vf_ls(const char* path,nvxntfs_lscb cb,void* ctx){
    if(!g_vfat.mounted)return -1; vf_ent e; u32 dc; int r=vf_walk(path,&e,0,0);
    if(r==1)dc=vf_rootdc(); else if(r==0&&(e.attr&0x10)){ dc=e.clus; if(!dc)dc=vf_rootdc(); } else return -1;
    int i=0; for(;;){ int q=vf_next(dc,&i,&e); if(q<0)return -1; if(q==0)break;
        if(e.sname[0]=='.'&&(e.sname[1]==' '||(e.sname[1]=='.'&&e.sname[2]==' ')))continue;
        cb(e.name,(e.attr&0x10)?1:0,e.size,ctx); }
    return 0;
}
int vf_format(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,const char* label,int type){
    vf_umount(); if(nsect>0xFFFFFFFFULL)return -1; u32 ts=(u32)nsect, spc;
    if(type==16){ if(ts<8400)return -2; spc=ts<=32680?2:ts<=262144?4:ts<=524288?8:ts<=1048576?16:ts<=2097152?32:64; if(ts>4194304)return -3; }
    else { if(ts<66600)return -2; spc=ts<=532480?1:ts<=16777216?8:ts<=33554432?16:ts<=67108864?32:64; }
    u32 rsvd=type==32?32:1, nf=2, re=type==32?0:512, rs=(re*32+511)/512, es=type==32?4:2, fz=1;
    { u32 t1=ts-(rsvd+rs), t2=256*spc+nf; if(type==32)t2/=2; fz=(t1+t2-1)/t2; }
    u32 cl=0; for(int it=0;it<64;it++){ cl=(ts-rsvd-rs-nf*fz)/spc; if((unsigned long long)(cl+2)*es<=(unsigned long long)fz*512)break; fz++; }
    if((unsigned long long)(cl+2)*es>(unsigned long long)fz*512)return -4;
    if(type==16&&(cl<4085||cl>65524))return -4; if(type==32&&cl<65525)return -4;
    u8 b[512]; vf_zero(b,512);
    b[0]=0xEB; b[1]=type==32?0x58:0x3C; b[2]=0x90; vf_copy(b+3,"NOOVEXOS",8); vf_w16(b+11,512); b[13]=(u8)spc; vf_w16(b+14,rsvd); b[16]=(u8)nf; vf_w16(b+17,re);
    if(type==16&&ts<65536)vf_w16(b+19,ts); else vf_w32(b+32,ts); b[21]=0xF8; vf_w16(b+24,63); vf_w16(b+26,255); vf_w32(b+28,(u32)base);
    u8 lab[11]; for(int i=0;i<11;i++)lab[i]=' '; for(int i=0;label&&label[i]&&i<11;i++){ char c=label[i]; if(c>='a'&&c<='z')c-=32; lab[i]=(u8)c; }
    u32 vid=0x4E560000u^(u32)base^(ts<<7);
    if(type==32){ vf_w32(b+36,fz); vf_w32(b+44,2); vf_w16(b+48,1); vf_w16(b+50,6); b[64]=0x80; b[66]=0x29; vf_w32(b+67,vid); vf_copy(b+71,lab,11); vf_copy(b+82,"FAT32   ",8); }
    else { vf_w16(b+22,fz); b[36]=0x80; b[38]=0x29; vf_w32(b+39,vid); vf_copy(b+43,lab,11); vf_copy(b+54,"FAT16   ",8); }
    b[510]=0x55; b[511]=0xAA;
    g_vfat.rd=rd; g_vfat.wr=wr; g_vfat.base=base; g_vfat.ro=0;
    u8 z[4096]; vf_zero(z,4096);
    for(u32 s=0;s<rsvd;s+=8){ int k=(int)(rsvd-s<8?rsvd-s:8); if(vf_ws(s,k,z))return -5; }
    if(vf_ws(0,1,b))return -5;
    if(type==32){ if(vf_ws(6,1,b))return -5; u8 f[512]; vf_zero(f,512); vf_w32(f,0x41615252u); vf_w32(f+484,0x61417272u); vf_w32(f+488,cl-1); vf_w32(f+492,3); vf_w32(f+508,0xAA550000u);
        if(vf_ws(1,1,f)||vf_ws(7,1,f))return -5; }
    u32 zl=nf*fz+rs+(type==32?spc:0);
    for(u32 s=0;s<zl;s+=8){ int k=(int)(zl-s<8?zl-s:8); if(vf_ws(rsvd+s,k,z))return -5; }
    u8 f0[512]; vf_zero(f0,512);
    if(type==32){ vf_w32(f0,0x0FFFFFF8u); vf_w32(f0+4,0x0FFFFFFFu); vf_w32(f0+8,0x0FFFFFFFu); } else { vf_w16(f0,0xFFF8); vf_w16(f0+2,0xFFFF); }
    for(u32 f=0;f<nf;f++) if(vf_ws(rsvd+f*fz,1,f0))return -5;
    u8 ve[512]; vf_zero(ve,512); vf_copy(ve,lab,11); ve[11]=0x08;
    { u16 t,dt; vf_now(&t,&dt); vf_w16(ve+22,t); vf_w16(ve+24,dt); }
    u32 rootsec=type==32?(rsvd+nf*fz+rs):(rsvd+nf*fz);
    if(vf_ws(rootsec,1,ve))return -5;
    return 0;
}
