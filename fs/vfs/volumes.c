#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/fs/vfs/volumes.h"

extern int gdisk_count(void); extern int gdisk_read_on(int i,u32 lba,int cnt,u8* buf); extern u32 gdisk_ratio_of(int i); extern u32 gdisk_size_of(int i);
extern const char* gdisk_name_of(int i); extern int gdisk_current(void); extern int usb_msd_read(u32 lba,int cnt,u8* out); extern u32 msd_lastlba; extern int ufat_ok;

nvol g_vol[VOL_MAX]; int g_vol_n=0; u32 g_vol_scans=0, g_vol_ms=0;
static u8* vb; static u8 vpop[256]; static int vpop_ok=0;
#define VB_SECS 128

static int vrd(int disk,u64 lba,int cnt,u8* buf){
    if(lba+(u64)cnt>0xFFFFFFFFULL)return -1;
    if(disk>=0)return gdisk_read_on(disk,(u32)lba,cnt,buf);
    while(cnt>0){ int c=cnt>8?8:cnt; if(usb_msd_read((u32)lba,c,buf))return -1; lba+=(u64)c; buf+=c*512; cnt-=c; } return 0; }
static u16 r16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u64 r64(const u8* p){ return (u64)r32(p)|((u64)r32(p+4)<<32); }
static void vcp(char* d,const char* s,int n){ int i=0; for(;s[i]&&i<n-1;i++)d[i]=s[i]; d[i]=0; }
static void vtrim(char* s){ int n=0; while(s[n])n++; while(n>0&&s[n-1]==' ')s[--n]=0; }
static void vutf16(char* d,const u8* s,int nch,int cap){ int o=0; for(int k=0;k<nch&&o<cap-1;k++){ u16 c=r16(s+2*k); if(!c)break; d[o++]=(c>=32&&c<127)?(char)c:'?'; } d[o]=0; }
static int vzero_bits(const u8* p,u64 nbytes,u64 maxbits,u64* bitpos){
    if(!vpop_ok){ for(int i=0;i<256;i++){ int c=0; for(int b=0;b<8;b++) if(i&(1<<b))c++; vpop[i]=(u8)c; } vpop_ok=1; }
    u64 z=0; for(u64 i=0;i<nbytes&&*bitpos<maxbits;i++){ u64 left=maxbits-*bitpos; if(left>=8){ z+=8-vpop[p[i]]; *bitpos+=8; } else { for(u64 b=0;b<left;b++) if(!(p[i]&(1u<<b)))z++; *bitpos+=left; } }
    return (int)0+(int)(z>0x7FFFFFFF?0x7FFFFFFF:z); }
static u64 vcount_free(int disk,u64 lba,u64 bytes,u64 maxbits){
    u64 bit=0, fr=0; while(bytes>0&&bit<maxbits){ u64 n=bytes>(u64)VB_SECS*512?(u64)VB_SECS*512:bytes; int secs=(int)((n+511)/512);
        if(vrd(disk,lba,secs,vb))return (u64)-1; u64 b0=bit; u64 z=0; { u64 bp=bit; for(u64 i=0;i<n&&bp<maxbits;i++){ u64 left=maxbits-bp; if(left>=8){ z+=8-vpop[vb[i]]; bp+=8; } else { for(u64 q=0;q<left;q++) if(!(vb[i]&(1u<<q)))z++; bp+=left; } } bit=bp; }
        (void)b0; fr+=z; lba+=(u64)secs; bytes-=n; }
    return fr; }
static void vprobe_fat(nvol* v,int disk,u64 st,const u8* s){
    u32 bps=r16(s+11), spc=s[13], resv=r16(s+14), nf=s[16], rootent=r16(s+17), tot=r16(s+19)?r16(s+19):r32(s+32), fsz=r16(s+22)?r16(s+22):r32(s+36);
    if(bps<512||bps>4096||!spc||!nf||!fsz||!tot)return; u32 rs=(rootent*32+bps-1)/bps; if(tot<resv+nf*fsz+rs)return; u32 cl=(tot-(resv+nf*fsz+rs))/spc;
    int ty=cl<4085?12:cl<65525?16:32; vcp(v->fs,ty==32?"FAT32":ty==16?"FAT16":"FAT12",12); v->total=(u64)cl*spc*bps;
    char lb[12]; const u8* l=s+(ty==32?71:43); for(int k=0;k<11;k++)lb[k]=(char)l[k]; lb[11]=0; vtrim(lb); if(lb[0]&&!(lb[0]=='N'&&lb[1]=='O'&&lb[2]==' '&&lb[3]=='N'))vcp(v->label,lb,36);
    u64 rootlba=ty==32?st+((u64)resv+(u64)nf*fsz+(u64)(r32(s+44)-2)*spc)*bps/512:st+((u64)resv+(u64)nf*fsz)*bps/512;
    if(!vrd(disk,rootlba,1,vb)){ for(int e=0;e<16;e++){ const u8* d=vb+e*32; if(!d[0])break; if(d[0]==0xE5)continue; if((d[11]&0x3F)==0x08){ for(int k=0;k<11;k++)lb[k]=(char)d[k]; lb[11]=0; vtrim(lb); if(lb[0])vcp(v->label,lb,36); break; } } }
    if(ty==32){ u32 fi=r16(s+48); if(fi&&fi<resv&&!vrd(disk,st+(u64)fi*bps/512,1,vb)&&r32(vb)==0x41615252u&&r32(vb+484)==0x61417272u){ u32 f=r32(vb+488); if(f<=cl){ v->freeb=(u64)f*spc*bps; v->has_free=1; return; } } }
    u64 fatbytes=ty==12?((u64)cl+2)*3/2:((u64)cl+2)*(ty==16?2:4); if(fatbytes>(16u<<20))return;
    u64 lba=st+(u64)resv*bps/512, done=0, fr=0; u32 idx=0; u8 carry[2]; (void)carry;
    if(ty==12){ u8* fat=(u8*)kmalloc(fatbytes+1024); if(!fat)return; if(vrd(disk,lba,(int)((fatbytes+511)/512),fat)){ kfree(fat); return; }
        for(u32 c=2;c<cl+2;c++){ u32 o=c*3/2; u16 w=(u16)(fat[o]|(fat[o+1]<<8)); u16 e=(c&1)?(w>>4):(w&0xFFF); if(!e)fr++; } kfree(fat); }
    else { u32 es=ty==16?2:4; while(done<fatbytes){ u64 n=fatbytes-done; if(n>(u64)VB_SECS*512)n=(u64)VB_SECS*512; int secs=(int)((n+511)/512); if(vrd(disk,lba,secs,vb))return;
            for(u64 o=0;o+es<=n;o+=es,idx++){ if(idx<2)continue; if(idx>=cl+2)break; u32 e=es==2?r16(vb+o):(r32(vb+o)&0x0FFFFFFFu); if(!e)fr++; }
            lba+=(u64)secs; done+=n; } }
    v->freeb=fr*spc*bps; v->has_free=1; }
static int vntfs_rec(int disk,u64 st,u64 off,u32 rsz,u8* out){
    u64 lba=st+off/512; u32 o=(u32)(off%512); int secs=(int)((o+rsz+511)/512); if(secs>16)return -1;
    if(vrd(disk,lba,secs,vb))return -1; for(u32 i=0;i<rsz;i++)out[i]=vb[o+i];
    if(!(out[0]=='F'&&out[1]=='I'&&out[2]=='L'&&out[3]=='E'))return -1;
    u16 uo=r16(out+4), uc=r16(out+6); if(uo+uc*2>rsz)return -1; for(u16 i=1;i<uc;i++){ u32 p=(u32)i*512-2; if(p+1>=rsz)break; out[p]=out[uo+i*2]; out[p+1]=out[uo+i*2+1]; }
    return 0; }
static const u8* vntfs_attr(const u8* rec,u32 rsz,u32 type){ u32 a=r16(rec+20); while(a+16<=rsz){ u32 t=r32(rec+a), l=r32(rec+a+4); if(t==0xFFFFFFFFu||l<16||a+l>rsz)break; if(t==type)return rec+a; a+=l; } return 0; }
static void vprobe_ntfs(nvol* v,int disk,u64 st,const u8* s){
    vcp(v->fs,"NTFS",12); u32 bps=r16(s+11); u32 spc=s[13]; if(spc>128)spc=1u<<(256-spc); if(bps<512||bps>4096||!spc)return;
    u64 cs=(u64)bps*spc, tot=r64(s+40), mft=r64(s+48); int c=(int)(signed char)s[64]; u32 rsz=c>0?(u32)(c*cs):(1u<<(-c)); if(rsz<512||rsz>4096)return;
    v->total=tot*bps; u64 ncl=tot/spc;
    u8* rec=(u8*)kmalloc(4096); if(!rec)return;
    if(!vntfs_rec(disk,st,mft*cs+3ull*rsz,rsz,rec)){ const u8* a=vntfs_attr(rec,rsz,0x60); if(a&&!a[8]){ u32 vl=r32(a+16); u16 vo=r16(a+20); if(vl&&vl<72)vutf16(v->label,a+vo,(int)vl/2,36); } }
    if(!vntfs_rec(disk,st,mft*cs+6ull*rsz,rsz,rec)){ const u8* a=vntfs_attr(rec,rsz,0x80); if(a&&a[8]){
        const u8* rl=a+r16(a+32); const u8* end=a+r32(a+4); u64 lcn=0, bit=0, fr=0; int ok=1; u64 totbytes=(ncl+7)/8; if(totbytes>(64ull<<20))ok=0;
        while(ok&&rl<end&&*rl){ int ln=*rl&15, of=*rl>>4; rl++; u64 len=0; for(int k=0;k<ln;k++)len|=(u64)rl[k]<<(8*k); rl+=ln; long long d=0; if(of){ for(int k=0;k<of;k++)d|=(long long)rl[k]<<(8*k); if(of<8&&(rl[of-1]&0x80))d|=(long long)(~0ULL<<(8*of)); } rl+=of; lcn+=(u64)d;
            if(!of)continue; u64 bytes=len*cs; u64 remain_bits=ncl-bit; if(bytes*8>remain_bits)bytes=(remain_bits+7)/8; u64 f=vcount_free(disk,st+lcn*cs/512,bytes,remain_bits); if(f==(u64)-1){ ok=0; break; } fr+=f; bit+=bytes*8; if(bit>=ncl)break; }
        if(ok&&bit>=ncl){ v->freeb=fr*cs; v->has_free=1; } } }
    kfree(rec); }
static void vprobe_exfat(nvol* v,int disk,u64 st,const u8* s){
    vcp(v->fs,"exFAT",12); u32 bs=1u<<s[108], cs=bs<<s[109]; u32 heap=r32(s+88), cnt=r32(s+92), root=r32(s+96); if(bs<512||bs>4096)return; v->total=(u64)cnt*cs;
    u64 rl=st+((u64)heap*bs+(u64)(root-2)*cs)/512; if(vrd(disk,rl,8,vb))return; u32 bmc=0; u64 bml=0; char lb[36]; lb[0]=0;
    for(int e=0;e<128;e++){ const u8* d=vb+e*32; if(!d[0])break; if(d[0]==0x81){ bmc=r32(d+20); bml=r64(d+24); } else if(d[0]==0x83){ vutf16(lb,d+2,d[1],36); } }
    if(lb[0])vcp(v->label,lb,36);
    if(bmc>=2&&bml&&bml<=(64ull<<20)){ u64 f=vcount_free(disk,st+((u64)heap*bs+(u64)(bmc-2)*cs)/512,bml,cnt); if(f!=(u64)-1){ v->freeb=f*cs; v->has_free=1; } } }
static void vprobe_ext(nvol* v,const u8* sb){
    u32 bsz=1024u<<r32(sb+24); u64 bl=r32(sb+4), fb=r32(sb+12); if(r32(sb+96)&0x80){ bl|=(u64)r32(sb+0x150)<<32; fb|=(u64)r32(sb+0x158)<<32; }
    vcp(v->fs,(r32(sb+96)&0x40)?"ext4":(r32(sb+92)&4)?"ext3":"ext2",12); v->total=bl*bsz; v->freeb=fb*bsz; v->has_free=1;
    char lb[17]; for(int k=0;k<16;k++)lb[k]=(char)sb[120+k]; lb[16]=0; if(lb[0])vcp(v->label,lb,36); }
static int vprobe(nvol* v,int disk,u64 st,u64 sz){
    v->fs[0]=0; v->label[0]=0; v->total=sz*512; v->freeb=0; v->has_free=0;
    u8 s[512]; if(vrd(disk,st,1,vb))return 0; for(int i=0;i<512;i++)s[i]=vb[i];
    if(s[3]=='N'&&s[4]=='T'&&s[5]=='F'&&s[6]=='S'){ vprobe_ntfs(v,disk,st,s); return 1; }
    if(s[3]=='E'&&s[4]=='X'&&s[5]=='F'&&s[6]=='A'&&s[7]=='T'){ vprobe_exfat(v,disk,st,s); return 1; }
    if(s[510]==0x55&&s[511]==0xAA&&((s[82]=='F'&&s[83]=='A'&&s[84]=='T')||(s[54]=='F'&&s[55]=='A'&&s[56]=='T'))){ vprobe_fat(v,disk,st,s); return 1; }
    if(r32(s)==0x3458564Eu){ vcp(v->fs,"NVX4",12); vcp(v->label,"NoovexOS",36); return 1; }
    if(sz>4&&!vrd(disk,st+2,2,vb)&&vb[56]==0x53&&vb[57]==0xEF){ vprobe_ext(v,vb); return 1; }
    return 0; }
static int vguid_is(const u8* g,u32 a){ return r32(g)==a; }
static void vadd(int disk,int kind,int part,u64 st,u64 sz){
    if(g_vol_n>=VOL_MAX||!sz)return; nvol* v=&g_vol[g_vol_n]; v->disk=disk; v->kind=kind; v->part=part; v->start=(u32)st; v->letter=0;
    if(!vprobe(v,disk,st,sz))return;
    if(kind!=VOL_USB&&disk==gdisk_current()&&disk_ok&&v->fs[0]=='N'&&v->fs[1]=='V')return;
    if(disk>=0){ char* p=scopy(v->dev,(char*)gdisk_name_of(disk)); p=scopy(p,", partition "); p=udec(p,(u32)part); *p=0; } else { char* p=scopy(v->dev,"USB mass storage"); if(part){ p=scopy(p,", partition "); p=udec(p,(u32)part); } *p=0; }
    g_vol_n++; }
static void vscan_disk(int disk,int kind,u64 dsz){
    u8 m[512]; if(vrd(disk,0,1,vb))return; for(int i=0;i<512;i++)m[i]=vb[i];
    u32 R=disk>=0?gdisk_ratio_of(disk):1; int valid=m[510]==0x55&&m[511]==0xAA, gpt=0, any=0;
    if(valid) for(int e=0;e<4;e++) if(m[446+e*16+4]==0xEE)gpt=1;
    if(gpt&&!vrd(disk,(u64)R,1,vb)&&vb[0]=='E'&&vb[1]=='F'&&vb[2]=='I'&&vb[3]==' '){
        u64 el=r64(vb+72); u32 n=r32(vb+80), es=r32(vb+84); if(n>128)n=128; if(es<128||es>512)es=128; u32 secs=(n*es+511)/512; if(secs>VB_SECS)secs=VB_SECS;
        u8* ent=(u8*)kmalloc((u64)secs*512); if(!ent)return; if(vrd(disk,el*R,(int)secs,ent)){ kfree(ent); return; }
        for(u32 i=0;i<n;i++){ const u8* e=ent+(u64)i*es; int z=1; for(int k=0;k<16;k++) if(e[k])z=0; if(z)continue; u64 at=r64(e+48);
            if(vguid_is(e,0xC12A7328u)||vguid_is(e,0xE3C9E316u)||vguid_is(e,0xDE94BBA4u)||vguid_is(e,0x21686148u)||vguid_is(e,0x0657FD6Du))continue; if(at&(3ULL<<62))continue;
            u64 f=r64(e+32), l=r64(e+40); if(l<f)continue; vadd(disk,kind,(int)i+1,f*R,(l-f+1)*R); any=1; }
        kfree(ent); return; }
    if(valid){ int pn=0; for(int e=0;e<4;e++){ const u8* pe=m+446+e*16; u8 ty=pe[4]; u64 st=(u64)r32(pe+8)*R, sz=(u64)r32(pe+12)*R; if(!ty||!sz)continue; pn++;
            if(ty==0x05||ty==0x0F||ty==0x85){ u64 eb=st, cur=st; for(int g=0;g<16;g++){ u8 x[512]; if(vrd(disk,cur,1,vb))break; for(int i=0;i<512;i++)x[i]=vb[i]; if(x[510]!=0x55||x[511]!=0xAA)break;
                    if(x[446+4]&&r32(x+446+12)){ vadd(disk,kind,5+g,cur+(u64)r32(x+446+8)*R,(u64)r32(x+446+12)*R); any=1; } if(!x[462+4])break; cur=eb+(u64)r32(x+462+8)*R; } continue; }
            if(ty==0xEF||ty==0x82||ty==0x27||ty==0x12||ty==0x84)continue; vadd(disk,kind,e+1,st,sz); any=1; } if(pn)return; }
    if(!any)vadd(disk,kind,0,0,dsz); }
static int g_vs_msd=-1; static u32 g_vs_lba=0, g_vs_gen=0;
void vol_scan(int force){
    extern int msd_autoinit_safe(void); extern u32 g_usbdev_gen;
    if(force||!g_vol_scans)msd_autoinit_safe();
    int usbnow=msd_ok?1:0; if(!force&&g_vol_scans&&usbnow==g_vs_msd&&msd_lastlba==g_vs_lba&&g_usbdev_gen==g_vs_gen)return;
    if(!vb)vb=(u8*)kmalloc(VB_SECS*512+512); if(!vb)return; if(!vpop_ok){ u64 bp=0; vzero_bits(vb,0,0,&bp); }
    u64 t0=hr_ns(); g_vol_n=0;
    nvol* c=&g_vol[g_vol_n++]; c->kind=VOL_SYS; c->disk=disk_ok?gdisk_current():-2; c->part=0; c->start=0; vcp(c->label,disk_ok?"NoovexOS":"Live RAM disk",36); vcp(c->fs,disk_ok?"NVX4":"RAM",12); vcp(c->dev,disk_ok?"NoovexOS system disk":"memory",28); c->total=0; c->freeb=0; c->has_free=1;
    int nd=gdisk_count(); for(int i=0;i<nd&&g_vol_n<VOL_MAX;i++)vscan_disk(i,VOL_FIXED,gdisk_size_of(i));
    if(msd_ok&&g_vol_n<VOL_MAX)vscan_disk(-1,VOL_USB,(u64)msd_lastlba+1);
    char L='C'; for(int i=0;i<g_vol_n;i++)g_vol[i].letter=L<='Z'?L++:'?';
    g_vs_msd=usbnow; g_vs_lba=msd_lastlba; g_vs_gen=g_usbdev_gen; g_vol_scans++; g_vol_ms=(u32)((hr_ns()-t0)/1000000ULL); }
void vol_name(const nvol* v,char* out,int cap){ char b[64],*p=b;
    if(v->label[0])p=scopy(p,(char*)v->label); else p=scopy(p,v->kind==VOL_USB?"USB Drive":"Local Disk"); p=scopy(p," ("); *p++=v->letter; p=scopy(p,":)"); *p=0; vcp(out,b,cap); }
