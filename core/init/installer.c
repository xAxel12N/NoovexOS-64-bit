#include "include/nvx/base.h"
#include "include/nvx/lib/base/mem.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/ethernet/nic_names.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/core/power.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/ntfs/ntfsk.h"
#include "include/nvx/apps/games/doom.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/core/init/splash.h"
#include "include/nvx/core/init/bootmenu.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/init/setup.h"
#include "include/nvx/core/init/main.h"
/* ================= Disk Manager (graphical partition editor + installer) ================= */
static int g_dm_open=0, g_dm_sel=-1, g_dm_editname=0, g_dm_ret=ST_BOOTMENU; static void ip_open_preset(int ret,int mode,int part); static char g_dm_name[16]="NoovexOS";
static u8 g_dm_mbr[512]; static int g_dm_msg=0; static const char* g_dm_msgtext=""; static int g_dm_confirm=0; static char g_dm_ctext[120];
#define DM_ROWS 8
#define DM_NB 9
static int dm_rowy[DM_ROWS], dm_bx[DM_NB], dm_by[DM_NB], dm_bw[DM_NB];
typedef struct { int used; u8 type; u32 start, size; int gi; char name[40]; char fs[24]; } dmpart;
static dmpart g_dm_p[DM_ROWS]; static int g_dm_np=0, g_dm_gpt=0;
static u8 g_dm_ghdr[512]; static u8* g_dm_gent=0; static u32 g_dm_gn=0, g_dm_gsz=0, g_dm_gsec=0; static u64 g_dm_gelba=0, g_dm_galt=0;
static u32 dm_crc32(const u8* p,u32 n){ u32 c=0xFFFFFFFFu; for(u32 i=0;i<n;i++){ c^=p[i]; for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&(0u-(c&1u))); } return ~c; }
static int dm_guid_is(const u8* g,u32 a,u16 b,u16 c,u8 d0,u8 d1,u8 d2,u8 d3,u8 d4,u8 d5,u8 d6,u8 d7){
    return *(const u32*)g==a&&*(const u16*)(g+4)==b&&*(const u16*)(g+6)==c&&g[8]==d0&&g[9]==d1&&g[10]==d2&&g[11]==d3&&g[12]==d4&&g[13]==d5&&g[14]==d6&&g[15]==d7; }
static const char* dm_gpt_type(const u8* g){
    if(dm_guid_is(g,0xC12A7328u,0xF81F,0x11D2,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B))return "EFI system";
    if(dm_guid_is(g,0xEBD0A0A2u,0xB9E5,0x4433,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7))return "Windows data";
    if(dm_guid_is(g,0xE3C9E316u,0x0B5C,0x4DB8,0x81,0x7D,0xF9,0x2D,0xF0,0x02,0x15,0xAE))return "Microsoft reserved";
    if(dm_guid_is(g,0xDE94BBA4u,0x06D1,0x4D40,0xA1,0x6A,0xBF,0xD5,0x01,0x79,0xD6,0xAC))return "Windows recovery";
    if(dm_guid_is(g,0x0FC63DAFu,0x8483,0x4772,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4))return "Linux";
    if(dm_guid_is(g,0x0657FD6Du,0xA4AB,0x43C4,0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F))return "Linux swap";
    if(dm_guid_is(g,0x21686148u,0x6449,0x6E6F,0x74,0x4E,0x65,0x65,0x64,0x45,0x46,0x49))return "BIOS boot";
    return "GPT partition"; }
static const char* dm_mbr_type(u8 t){ switch(t){ case 0x07: return "NTFS/exFAT"; case 0x0B: case 0x0C: return "FAT32"; case 0x0E: case 0x06: case 0x04: return "FAT16"; case 0x01: return "FAT12"; case 0x83: return "Linux"; case 0x82: return "Linux swap"; case 0x05: case 0x0F: return "Extended"; case 0xEF: return "EFI system"; case 0x27: return "Windows recovery"; case NVX_PTYPE: return "NoovexOS"; default: return "Partition"; } }
static void dm_cp(char* d,const char* s,int n){ int i=0; for(;s[i]&&i<n-1;i++)d[i]=s[i]; d[i]=0; }
static void dm_detect_fs(dmpart* d){
    d->fs[0]=0; u8 s[512]; if(gdisk_read(d->start,1,s))return;
    if(s[3]=='N'&&s[4]=='T'&&s[5]=='F'&&s[6]=='S'){ dm_cp(d->fs,"NTFS",24); return; }
    if(s[3]=='E'&&s[4]=='X'&&s[5]=='F'&&s[6]=='A'&&s[7]=='T'){ dm_cp(d->fs,"exFAT",24); return; }
    if(s[82]=='F'&&s[83]=='A'&&s[84]=='T'&&s[85]=='3'&&s[86]=='2'){ dm_cp(d->fs,"FAT32",24); char* p=d->fs+5; char v[12]; int k=0; for(;k<11;k++)v[k]=(char)s[71+k]; v[11]=0; int z=10; while(z>=0&&v[z]==' ')v[z--]=0; if(v[0]&&v[0]!=' '){ p=scopy(p," \""); p=scopy(p,v); *p++='"'; *p=0; } return; }
    if(s[54]=='F'&&s[55]=='A'&&s[56]=='T'){ dm_cp(d->fs,"FAT16",24); return; }
    if(*(u32*)s==0x3458564Eu){ dm_cp(d->fs,"NoovexOS NVX4",24); return; }
    if(d->size>4&&gdisk_read(d->start+2,1,s)==0&&s[56]==0x53&&s[57]==0xEF){ dm_cp(d->fs,"ext4",24); return; }
}
static void dm_load(void){
    gdisk_ensure(); g_dm_np=0; g_dm_gpt=0; for(int i=0;i<512;i++)g_dm_mbr[i]=0;
    for(int i=0;i<DM_ROWS;i++){ g_dm_p[i].used=0; g_dm_p[i].name[0]=0; g_dm_p[i].fs[0]=0; g_dm_p[i].gi=-1; }
    if(g_dktype==DK_NONE)return; gdisk_read(0,1,g_dm_mbr);
    int valid=(g_dm_mbr[510]==0x55&&g_dm_mbr[511]==0xAA), prot=0; u32 R=dk_ratio();
    if(valid) for(int e=0;e<4;e++) if(g_dm_mbr[446+e*16+4]==0xEE)prot=1;
    if(prot&&gdisk_read(1*R,1,g_dm_ghdr)==0&&g_dm_ghdr[0]=='E'&&g_dm_ghdr[1]=='F'&&g_dm_ghdr[2]=='I'&&g_dm_ghdr[3]==' '){
        g_dm_gpt=1; g_dm_gelba=*(u64*)(g_dm_ghdr+72); g_dm_gn=*(u32*)(g_dm_ghdr+80); g_dm_gsz=*(u32*)(g_dm_ghdr+84); g_dm_galt=*(u64*)(g_dm_ghdr+32);
        if(g_dm_gn>256)g_dm_gn=256; if(g_dm_gsz<128||g_dm_gsz>512)g_dm_gsz=128;
        g_dm_gsec=(g_dm_gn*g_dm_gsz+511)/512; if(g_dm_gent)kfree(g_dm_gent); g_dm_gent=(u8*)kmalloc((u64)g_dm_gsec*512); if(!g_dm_gent){ g_dm_gpt=0; return; }
        if(gdisk_readn((u32)(g_dm_gelba*R),(int)g_dm_gsec,g_dm_gent)){ g_dm_gpt=0; return; }
        for(u32 i=0;i<g_dm_gn&&g_dm_np<DM_ROWS;i++){ const u8* e=g_dm_gent+(u64)i*g_dm_gsz; int z=1; for(int k=0;k<16;k++) if(e[k])z=0; if(z)continue;
            u64 f=*(const u64*)(e+32), l=*(const u64*)(e+40); if(l<f)continue; dmpart* d=&g_dm_p[g_dm_np++];
            u64 st=f*R, sz=(l-f+1)*R; d->used=1; d->type=0xEE; d->start=st>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)st; d->size=sz>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)sz; d->gi=(int)i;
            int o=0; for(int k=0;k<36&&o<39;k++){ u16 ch=*(const u16*)(e+56+k*2); if(!ch)break; d->name[o++]=(ch>=32&&ch<127)?(char)ch:'?'; } d->name[o]=0;
            if(!d->name[0])dm_cp(d->name,dm_gpt_type(e),40); else { char t[40]; int q=0; const char* tn=dm_gpt_type(e); for(;tn[q]&&q<39;q++)t[q]=tn[q]; t[q]=0; (void)t; }
            if(st<0xFFFFFFFFULL)dm_detect_fs(d); }
        return; }
    for(int e=0;e<4;e++){ u8* pe=g_dm_mbr+446+e*16; u8 ty=valid?pe[4]:0; u32 st=*(u32*)(pe+8)*R, sz=*(u32*)(pe+12)*R;
        dmpart* d=&g_dm_p[e]; d->used=(valid&&ty!=0&&sz!=0); d->type=ty; d->start=st; d->size=sz; d->gi=e; if(d->used){ dm_cp(d->name,dm_mbr_type(ty),40); dm_detect_fs(d); } }
    g_dm_np=4;
}
static u32 dm_free_start(void){
    u32 top=2048; for(int e=0;e<4;e++) if(g_dm_p[e].used){ u32 end=g_dm_p[e].start+g_dm_p[e].size; if(end>top)top=end; }
    return (top+2047u)&~2047u;
}
static void dm_create(void){
    if(g_dm_gpt){ g_dm_msg=1; g_dm_msgtext="This disk uses GPT. Delete partitions or erase the disk, then Install whole disk."; return; }
    int slot=-1; for(int e=0;e<4;e++) if(!g_dm_p[e].used){ slot=e; break; } if(slot<0){ g_dm_msg=1; g_dm_msgtext="No free partition slots (max 4)."; return; }
    u32 st=dm_free_start(); if(st>=g_dksectors){ g_dm_msg=1; g_dm_msgtext="No free space on the disk."; return; }
    u32 sz=g_dksectors-st; sz-=sz%dk_ratio(); g_dm_p[slot].used=1; g_dm_p[slot].type=0x0C; g_dm_p[slot].start=st; g_dm_p[slot].size=sz;
    u8* pe=g_dm_mbr+446+slot*16; for(int i=0;i<16;i++)pe[i]=0; pe[4]=0x0C; pe[5]=0xFE;pe[6]=0xFF;pe[7]=0xFF; *(u32*)(pe+8)=st/dk_ratio(); *(u32*)(pe+12)=sz/dk_ratio();
    g_dm_mbr[510]=0x55; g_dm_mbr[511]=0xAA; gdisk_write(0,g_dm_mbr); gdisk_flush(); dm_load(); g_dm_sel=slot;
    g_dm_msg=1; g_dm_msgtext="Partition created.";
}
static int dm_gpt_write_side(u64 hdr_lba,const u8* hdr_in,u64 ent_lba){
    u32 R=dk_ratio(); u8 h[512]; for(int i=0;i<512;i++)h[i]=hdr_in[i];
    u32 crc=dm_crc32(g_dm_gent,g_dm_gn*g_dm_gsz); *(u32*)(h+88)=crc; u32 hs=*(u32*)(h+12); if(hs<92||hs>512)hs=92; *(u32*)(h+16)=0; *(u32*)(h+16)=dm_crc32(h,hs);
    for(u32 s=0;s<g_dm_gsec;s++) if(gdisk_write((u32)(ent_lba*R)+s,g_dm_gent+(u64)s*512))return -1;
    return gdisk_write((u32)(hdr_lba*R),h); }
static void dm_delete_do(void){
    dmpart* d=&g_dm_p[g_dm_sel];
    if(g_dm_gpt){ u8* e=g_dm_gent+(u64)d->gi*g_dm_gsz; for(u32 k=0;k<g_dm_gsz;k++)e[k]=0;
        int r=dm_gpt_write_side(1,g_dm_ghdr,g_dm_gelba);
        u8 alt[512]; u32 R=dk_ratio(); if(g_dm_galt&&g_dm_galt*R<g_dksectors&&gdisk_read((u32)(g_dm_galt*R),1,alt)==0&&alt[0]=='E'&&alt[1]=='F'&&alt[2]=='I')r|=dm_gpt_write_side(g_dm_galt,alt,*(u64*)(alt+72));
        gdisk_flush(); dm_load(); g_dm_msg=1; g_dm_msgtext=r?"Writing the partition table failed.":"Partition deleted."; g_dm_sel=-1; return; }
    u8* pe=g_dm_mbr+446+d->gi*16; for(int i=0;i<16;i++)pe[i]=0; g_dm_mbr[510]=0x55; g_dm_mbr[511]=0xAA;
    gdisk_write(0,g_dm_mbr); gdisk_flush(); dm_load(); g_dm_msg=1; g_dm_msgtext="Partition deleted."; g_dm_sel=-1;
}
static void dm_erase_do(void){
    u8 z[512]; for(int i=0;i<512;i++)z[i]=0; u32 R=dk_ratio(); int bad=0;
    for(u32 s=0;s<64u*R&&s<g_dksectors;s++) if(gdisk_write(s,z))bad=1;
    if(g_dksectors>64u*R) for(u32 s=g_dksectors-64u*R;s<g_dksectors;s++) if(gdisk_write(s,z))bad=1;
    gdisk_flush(); dm_load(); g_dm_sel=-1; g_dm_msg=1; g_dm_msgtext=bad?"Erasing failed - the disk reported an error.":"Disk erased. It has no partitions now.";
}
static char* dm_size(char* p,u64 sec){ u64 mb=sec*512/1048576ULL; if(mb>=1024*1024){ p=udec(p,(u32)(mb/(1024*1024))); *p++='.'; p=udec(p,(u32)((mb%(1024*1024))*10/(1024*1024))); return scopy(p," TB"); }
    if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)mb); return scopy(p," MB"); }
static void dm_delete(void){
    if(g_dm_sel<0||g_dm_sel>=g_dm_np||!g_dm_p[g_dm_sel].used){ g_dm_msg=1; g_dm_msgtext="Select a partition to delete."; return; }
    char* p=g_dm_ctext; p=scopy(p,"Delete \""); p=scopy(p,g_dm_p[g_dm_sel].name); p=scopy(p,"\" ("); p=dm_size(p,g_dm_p[g_dm_sel].size); p=scopy(p,")? Its data will be lost."); *p=0; g_dm_confirm=1; }
static void dm_erase(void){ char* p=g_dm_ctext; p=scopy(p,"Erase the whole disk ("); p=dm_size(p,g_dksectors); p=scopy(p,")? All partitions and data will be lost."); *p=0; g_dm_confirm=2; }
static void dm_next_disk(void){
    extern int gdisk_count(void); extern int gdisk_current(void); extern int gdisk_pick(int); extern int disk_ok; extern int g_nvx_generic;
    int n=gdisk_count(); if(n<2){ g_dm_msg=1; g_dm_msgtext="Only one disk was found."; return; }
    int was=gdisk_current(); int nx=(was+1)%n; if(gdisk_pick(nx)>0){ if(disk_ok&&(g_nvx_generic||g_dktype==DK_IDE))disk_ok=0; }
    g_dm_sel=-1; dm_load(); }
void dm_present(void){
    extern int gdisk_count(void); extern int gdisk_current(void);
    for(u32 yy=0;yy<H;yy++)for(u32 xx=0;xx<W;xx++)SCENE[(u64)yy*SW+xx]=rgb(18,26,44);
    dstr((int)W*8/100,(int)H*5/100,"Disk Manager",rgb(240,246,252),3);
    char b[160],*p;
    p=b;p=scopy(p,"Disk ");p=udec(p,(u32)(gdisk_current()+1));p=scopy(p," of ");p=udec(p,(u32)gdisk_count());p=scopy(p,": ");p=scopy(p,(char*)gdisk_name());
    if(g_dksectors){ p=scopy(p,"   "); p=dm_size(p,g_dksectors); } else p=scopy(p,"  (none detected)");
    p=scopy(p,g_dktype==DK_NONE?"":g_dm_gpt?"   GPT partition table":(g_dm_mbr[510]==0x55&&g_dm_mbr[511]==0xAA)?"   MBR partition table":"   no partition table"); *p=0;
    dstr((int)W*8/100,(int)H*5/100+40,b,rgb(150,175,215),1);
    int lx=(int)W*8/100, ly=(int)H*14/100, lw=(int)W*84/100, rh=40;
    dstr(lx,ly-22,"Partitions:",rgb(150,175,215),1);
    int rows=g_dm_gpt?g_dm_np:4; if(rows<1&&g_dm_gpt)rows=1;
    for(int e=0;e<DM_ROWS;e++)dm_rowy[e]=-1000;
    for(int e=0;e<rows&&e<DM_ROWS;e++){ int ry=ly+e*(rh+6); dm_rowy[e]=ry; int selr=(g_dm_sel==e);
        int hov=(mx>=lx&&mx<lx+lw&&my>=ry&&my<ry+rh)||selr;
        fill_round(lx,ry,lw,rh,8,15, selr?rgb(40,90,150):hov?rgb(30,44,72):rgb(26,36,58));
        dmpart* d=&g_dm_p[e]; p=b; p=udec(p,(u32)(e+1)); p=scopy(p,".  ");
        if(d->used){ p=scopy(p,d->name); if(d->fs[0]){ p=scopy(p,"   ["); p=scopy(p,d->fs); *p++=']'; } p=scopy(p,"   "); p=dm_size(p,d->size); p=scopy(p,"   @ LBA "); p=udec(p,d->start);
            if(!g_dm_gpt&&(g_dm_mbr[446+d->gi*16]&0x80))p=scopy(p,"  [active]"); }
        else p=scopy(p,g_dm_gpt?"(no partitions)":"(empty)");
        *p=0; dstr(lx+16,ry+12,b, d->used?rgb(232,240,252):rgb(120,132,152),1); }
    int ny=ly+(rows<4?4:rows)*(rh+6)+14; dstr(lx,ny,"Volume name:",rgb(150,175,215),1);
    int nfx=lx+130, nfw=260; fill_round(nfx,ny-6,nfw,28,6,15, g_dm_editname?rgb(255,255,255):rgb(40,52,80));
    p=b;p=scopy(p,g_dm_name); if(g_dm_editname&&((g_ticks/50)&1))*p++='_'; *p=0; dstr(nfx+10,ny,b, g_dm_editname?rgb(20,28,44):rgb(232,240,252),1);
    dm_bx[5]=nfx;dm_by[5]=ny-6;dm_bw[5]=nfw;
    const char* bl[8]={"Create partition","Delete partition","Install to selected","Install whole disk","Back","","Erase disk","Next disk"};
    u32 bcol[8]={rgb(60,120,80),rgb(150,70,70),rgb(50,110,190),rgb(150,110,50),rgb(60,70,92),0,rgb(170,50,50),rgb(70,90,130)};
    static const int ord[7]={0,1,6,7,2,3,4};
    int byb=ny+46, bxs=lx;
    for(int q=0;q<7;q++){ int k=ord[q]; int bw2=(k==2||k==3)?200:(k==4?100:160); if(bxs+bw2>lx+lw){ bxs=lx; byb+=48; }
        int hov=(mx>=bxs&&mx<bxs+bw2&&my>=byb&&my<byb+38);
        fill_round(bxs,byb,bw2,38,8,15, hov?mix(bcol[k],rgb(255,255,255),50):bcol[k]); dstr_ctr(bxs+bw2/2,byb+11,(char*)bl[k],rgb(255,255,255),1);
        dm_bx[k]=bxs; dm_by[k]=byb; dm_bw[k]=bw2; bxs+=bw2+12; }
    if(g_dm_confirm){ int pw=620,ph=140,px=(int)W/2-pw/2,py=(int)H/2-ph/2; shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
        dstr_ctr((int)W/2,py+34,g_dm_ctext,rgb(150,30,40),1); int bw=140,gap=20,yb=py+ph-50, x1=(int)W/2-bw-gap/2, x2=(int)W/2+gap/2;
        fill_round(x1,yb,bw,34,8,15,rgb(190,50,50)); dstr_ctr(x1+bw/2,yb+10,g_dm_confirm==2?"Erase":"Delete",rgb(255,255,255),1);
        fill_round(x2,yb,bw,34,8,15,rgb(110,118,135)); dstr_ctr(x2+bw/2,yb+10,"Cancel",rgb(255,255,255),1);
        dm_bx[8]=x1; dm_by[8]=yb; dm_bw[8]=bw; dm_bx[4]=x2; dm_by[4]=yb; dm_bw[4]=bw; }
    else if(g_dm_msg){ int pw=640,ph=120,px=(int)W/2-pw/2,py=(int)H/2-ph/2; shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
        dstr_ctr((int)W/2,py+34,(char*)g_dm_msgtext,rgb(30,36,54),1); int okw=120,okx=(int)W/2-okw/2,oky=py+ph-46;
        fill_round(okx,oky,okw,32,8,15,ACC); dstr_ctr(okx+okw/2,oky+9,"OK",rgb(255,255,255),1); dm_bx[4]=okx;dm_by[4]=oky;dm_bw[4]=okw; }
    draw_cursor(); blit_all();
}
static void dm_do_install(int whole){
    int idx=0;
    if(!whole){ if(g_dm_gpt){ g_dm_msg=1; g_dm_msgtext="On a GPT disk use Install whole disk (it replaces all partitions)."; return; }
        if(g_dm_sel<0||!g_dm_p[g_dm_sel].used){ g_dm_msg=1; g_dm_msgtext="Select a partition first."; return; } idx=g_dm_p[g_dm_sel].gi; }
    ip_open_preset(g_dm_ret==ST_INSTALL?ST_BOOTMENU:g_dm_ret,whole?0:1,idx);
}
static void dm_close(void){ g_dm_open=0; if(g_dm_ret==ST_INSTALL){ ip_load(); g_state=ST_INSTALL; } else g_state=g_dm_ret; }
int dm_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(g_dm_confirm){ if(in_rect(mx,my,dm_bx[8],dm_by[8],dm_bw[8],34)){ int c=g_dm_confirm; g_dm_confirm=0; if(c==1)dm_delete_do(); else dm_erase_do(); } else if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],34)) g_dm_confirm=0; }
            else if(g_dm_msg){ if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],32)) g_dm_msg=0; }
            else { g_dm_editname=0;
                for(int e=0;e<DM_ROWS;e++) if(dm_rowy[e]>-1000&&in_rect(mx,my,(int)W*8/100,dm_rowy[e],(int)W*84/100,40)){ g_dm_sel=e; }
                if(in_rect(mx,my,dm_bx[5],dm_by[5],dm_bw[5],28)) g_dm_editname=1;
                else if(in_rect(mx,my,dm_bx[0],dm_by[0],dm_bw[0],38)) dm_create();
                else if(in_rect(mx,my,dm_bx[1],dm_by[1],dm_bw[1],38)) dm_delete();
                else if(in_rect(mx,my,dm_bx[6],dm_by[6],dm_bw[6],38)) dm_erase();
                else if(in_rect(mx,my,dm_bx[7],dm_by[7],dm_bw[7],38)) dm_next_disk();
                else if(in_rect(mx,my,dm_bx[2],dm_by[2],dm_bw[2],38)) dm_do_install(0);
                else if(in_rect(mx,my,dm_bx[3],dm_by[3],dm_bw[3],38)) dm_do_install(1);
                else if(in_rect(mx,my,dm_bx[4],dm_by[4],dm_bw[4],38)) dm_close();
            } }
        mouse_btn_prev=btn;
    }
    int c; while((c=kpop())>=0){ changed=1;
        if(g_dm_confirm){ if(c=='y'||c=='Y'){ int cf=g_dm_confirm; g_dm_confirm=0; if(cf==1)dm_delete_do(); else dm_erase_do(); } else if(c==27||c=='n'||c=='N')g_dm_confirm=0; continue; }
        if(g_dm_msg){ g_dm_msg=0; continue; }
        if(g_dm_editname){ int L=0; while(g_dm_name[L])L++;
            if(c=='\n'){ g_dm_editname=0; } else if(c=='\b'){ if(L>0)g_dm_name[L-1]=0; }
            else if(L<11 && ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')){ g_dm_name[L]=(char)c; g_dm_name[L+1]=0; }
            continue; }
        if(c==27){ dm_close(); }
        else if(c==19){ if(g_dm_sel>0)g_dm_sel--; else g_dm_sel=0; } else if(c==20){ int mxr=g_dm_gpt?g_dm_np:4; if(g_dm_sel<mxr-1)g_dm_sel++; }
    }
    return changed;
}
extern const unsigned char _binary_inst_efi_bin_start[], _binary_inst_efi_bin_end[];
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
void nvx_mark_file(const char* path,const char* txt){
    extern void fs_newslot(int); extern int fs_set(int,const char*,long); extern int fs_sync(int); extern int disk_ok;
    int k=k_find_file(path);
    if(k<0){ int d; char base[64]; if(k_split(path,&d,base)<0||!base[0]){ dt_mkdir_at(0,"etc"); if(k_split(path,&d,base)<0||!base[0])return; }
        for(int i=0;i<FS_N;i++) if(!FS[i].used){ k=i; break; } if(k<0)return;
        fs_newslot(k); int q=0; while(base[q]&&q<63){ FS[k].name[q]=base[q]; q++; } FS[k].name[q]=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=d; FS[k].mode=0100644u; FS[k].uid=0; FS[k].gid=0; }
    long n=0; while(txt[n])n++; fs_set(k,txt,n); g_fs_dirty[k]=1; if(disk_ok)fs_sync(k); }
#define INST_BOOTP (dk_ratio()>1?557056u:262144u)
#define INST_DATAP (((NVX_REGION_SECS)+2047u)&~2047u)
#define KSNAP_MAX 0x4000000u
#define KDATA_MAX 0x400000u
static u8 g_kdata_snap[KDATA_MAX]; static u8* g_kimg_snap=0; static u32 g_kimg_len=0, g_inst_datalen=0, g_kimm_len=0;
#define KPG_MAX (KSNAP_MAX/4096u)
static u32 g_kpg_sum[KPG_MAX];
static u32 kpg_hash(const u8* p,u32 n){ u32 h=2166136261u; for(u32 i=0;i<n;i++){ h^=p[i]; h*=16777619u; } return h; }
void inst_snapshot_data(void){
    extern char __immutable_end[], __file_end[];
    u32 klen=*(volatile u32*)(u64)(0x100000ULL+24); u32 flen=(u32)((u64)__file_end-0x100000ULL);
    if(klen<4096||klen>KSNAP_MAX)klen=flen; if(klen>KSNAP_MAX)klen=0;
    u32 imm=(u32)((u64)__immutable_end-0x100000ULL); if(imm>klen||klen-imm>KDATA_MAX){ g_kimg_len=0; return; }
    const u8* k=(const u8*)0x100000ULL; for(u32 i=imm;i<klen;i++)g_kdata_snap[i-imm]=k[i];
    for(u32 pg=0;pg*4096u<imm&&pg<KPG_MAX;pg++){ u32 n=imm-pg*4096u; if(n>4096u)n=4096u; g_kpg_sum[pg]=kpg_hash(k+pg*4096u,n); }
    g_kimg_len=klen; g_kimm_len=imm; g_inst_datalen=klen-imm;
}
static const u8* kimg_full(void){
    if(g_kimg_snap)return g_kimg_snap; if(!g_kimg_len)return 0;
    u8* t=(u8*)kmalloc(g_kimg_len); if(!t)return 0; const u8* k=(const u8*)0x100000ULL;
    for(u32 i=0;i<g_kimm_len;i++)t[i]=k[i]; for(u32 i=g_kimm_len;i<g_kimg_len;i++)t[i]=g_kdata_snap[i-g_kimm_len];
    g_kimg_snap=t; return t; }
static int kimg_check(const u8* img,u32 klen,int* imm_ok,int* full_ok){
    extern char __immutable_end[];
    u32 imm=(u32)((u64)__immutable_end-0x100000ULL); u32 so=(u32)((u64)&g_buildsig-0x100000ULL), ho=so+32, fo=so+88;
    *imm_ok=-1; *full_ok=-1; if(!klen||imm>klen||fo+32>klen)return -1;
    int zero=1; for(int i=0;i<32;i++) if(g_buildsig.sha256_full[i])zero=0; if(zero)return 1;
    u8* t=(u8*)kmalloc(klen); if(!t)return -1; for(u32 i=0;i<klen;i++)t[i]=img[i];
    for(int i=0;i<32;i++)t[ho+i]=0; u8 d[32]; sha256_hash(t,(int)imm,d);
    *imm_ok=1; for(int i=0;i<32;i++) if(d[i]!=g_buildsig.sha256[i])*imm_ok=0;
    for(int i=0;i<32;i++)t[fo+i]=0; sha256_hash(t,(int)klen,d);
    *full_ok=1; for(int i=0;i<32;i++) if(d[i]!=g_buildsig.sha256_full[i])*full_ok=0;
    kfree(t); return 0;
}
void kcheck_run(void){ char b[160],*p; extern char __immutable_end[];
        u32 imm=(u32)((u64)__immutable_end-0x100000ULL); u32 stamp=*(volatile u32*)(u64)(0x100000ULL+24);
        p=b; p=scopy(p,"kernel build "); p=scopy(p,(char*)g_buildsig.version); p=scopy(p,"  size stamp "); p=udec(p,stamp); p=scopy(p,"  captured at boot "); p=udec(p,g_kimg_len); p=scopy(p,"  code part "); p=udec(p,imm); *p=0; term_puts(b); term_putc('\n');
        int io=-1,fo=-1; const u8* full=kimg_full(); int r=(g_kimg_len&&full)?kimg_check(full,g_kimg_len,&io,&fo):-1;
        if(r<0) term_puts("  boot image: not captured\n");
        else if(r>0) term_puts("  boot image: this build has no signature hashes\n");
        else { p=b; p=scopy(p,"  boot image: code "); p=scopy(p,io?"MATCHES":"DIFFERS"); p=scopy(p,"   whole file "); p=scopy(p,fo?"MATCHES":"DIFFERS"); *p=0; tcol(io&&fo?GREEN:rgb(240,120,120)); term_puts(b); term_putc('\n'); tcol(WHT); klog2("kcheck:",b); }
        if(r==0&&!io){ term_puts("  boot image pages that differ from the file's own hash cannot be listed - comparing with memory now:\n"); }
        int nd=0; const u8* k=(const u8*)0x100000ULL;
        u32 lim=imm<g_kimg_len?imm:g_kimg_len;
        for(u32 pg=0;pg*4096u<lim&&pg<KPG_MAX;pg++){ u32 n=lim-pg*4096u; if(n>4096u)n=4096u; u32 first=0xFFFFFFFFu;
            if(kpg_hash(k+pg*4096u,n)!=g_kpg_sum[pg])first=pg*4096u;
            if(first!=0xFFFFFFFFu){ if(nd<12){ p=b; p=scopy(p,"  changed since boot: 0x"); p=hx(p,0x100000u+first,8); *p=0; term_puts(b); term_putc('\n'); } nd++; } }
        p=b; p=scopy(p,"  code pages changed since boot: "); p=udec(p,(u32)nd); *p=0; term_puts(b); term_putc('\n'); }

static char* hx64(char* p,u64 v){ if(v>>32)p=hx(p,(u32)(v>>32),8); return hx(p,(u32)v,8); }
typedef struct { const char* name; u64 s,e; int skips; } karea;
static int kareas(karea* a){ extern char __bss_end[]; int n=0;
    a[n].name="boot information"; a[n].s=0x5000; a[n].e=0x8000; a[n++].skips=0;
    a[n].name="kernel image"; a[n].s=0x100000; a[n].e=0x6500000; a[n++].skips=0;
    a[n].name="boot stack"; a[n].s=0x9800000; a[n].e=0x9A00000; a[n++].skips=0;
    { extern u32 g_devbuf; if(g_devbuf==0x09A00000u){ a[n].name="USB and disk buffers"; a[n].s=0x9A00000; a[n].e=0x9C00000; a[n++].skips=0; } }
    a[n].name="page tables and kernel data"; a[n].s=0x10000000; a[n].e=(u64)__bss_end; a[n++].skips=0;
    a[n].name="DMA arena"; a[n].s=DMA_ARENA_BASE; a[n].e=DMA_ARENA_TOP; a[n++].skips=1;
    a[n].name="page pool"; a[n].s=PF_BASE; a[n].e=PF_TOP; a[n++].skips=1;
    a[n].name="module area"; a[n].s=0x0E000000ULL; a[n].e=0x10000000ULL; a[n++].skips=0;
    a[n].name="heap"; a[n].s=heap_base; a[n].e=heap_top; a[n++].skips=0;
    if(pf_x_top){ a[n].name="page pool (from RAM)"; a[n].s=pf_x_base; a[n].e=pf_x_top; a[n++].skips=1; }
    return n; }
int mm_area_conflicts(void){ karea a[10]; int n=kareas(a), bad=0; for(int i=0;i<n;i++){ if(a[i].skips||(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL))continue; u64 bs,be; u32 bt; if(mm_first_bad(a[i].s,a[i].e,&bs,&be,&bt))bad++; } return bad; }
void mm_conflicts_log(void){ karea a[10]; int n=kareas(a); char b[160],*p;
    for(int i=0;i<n;i++){ if(a[i].skips||(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL))continue; u64 bs,be; u32 bt; if(!mm_first_bad(a[i].s,a[i].e,&bs,&be,&bt))continue;
        p=b; p=scopy(p,"memory:   "); p=scopy(p,a[i].name); p=scopy(p,": firmware "); p=scopy(p,bt==0xFFFFFFFFu?"hole (no RAM)":mm_tname(bt)); p=scopy(p," at 0x"); p=hx(p,(u32)bs,8); p=scopy(p,"-0x"); p=hx(p,(u32)be,8); *p=0; klog(b); } }
void memmap_run(void){ char b[200],*p;
    if(!mm_known()){ term_puts("firmware memory map: not available (started without the UEFI loader)\n"); }
    else { p=b; p=scopy(p,"firmware memory map: "); p=udec(p,(u32)g_mmr_n); p=scopy(p,g_mmr_over?" regions (list was cut short)":" regions"); *p=0; term_puts(b); term_putc('\n');
        int shown=0; for(int i=0;i<g_mmr_n&&shown<48;i++){ if(g_mmr[i].s>=0x100000000ULL)continue;
            p=b; p=scopy(p,"  "); p=hx64(p,g_mmr[i].s); *p++='-'; p=hx64(p,g_mmr[i].e-1); p=scopy(p,"  "); p=scopy(p,mm_tname(g_mmr[i].t)); *p=0;
            tcol(g_mmr[i].t==7?WHT:rgb(255,205,100)); term_puts(b); term_putc('\n'); shown++; }
        tcol(WHT); }
    term_puts("kernel memory areas:\n");
    karea a[10]; int n=kareas(a);
    for(int i=0;i<n;i++){ p=b; p=scopy(p,"  "); p=scopy(p,a[i].name); while(p-b<32)*p++=' '; p=hx64(p,a[i].s); *p++='-'; p=hx64(p,a[i].e-1);
        u64 c=a[i].s; int k=0; u64 bs,be; u32 bt;
        if(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL&&mm_known()){ p=scopy(p,"  unused"); }
        while(mm_known()&&c<a[i].e&&k<3&&mm_first_bad_ex(c,a[i].e,a[i].skips,&bs,&be,&bt)){ p=scopy(p,k?", ":a[i].skips?"  skips ":"  OVERLAPS "); p=scopy(p,mm_tname(bt)); *p++=' '; p=hx64(p,bs); *p++='-'; p=hx64(p,be-1); c=be; k++; }
        if(!k&&mm_known()&&!(a[i].s==0x0E000000ULL&&!*(volatile u32*)0x5010ULL))p=scopy(p,"  ok"); *p=0;
        tcol(k&&!a[i].skips?rgb(240,120,120):k?rgb(255,205,100):GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
    p=b; p=scopy(p,"heap in use: "); p=hx64(p,heap_base); *p++='-'; p=hx64(p,heap_cur); p=scopy(p,g_heap_mapped==2?"  (memory the firmware never used)":g_heap_mapped?"  (limited to firmware RAM)":"  (size from RAM probe)"); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"devices whose firmware DMA was stopped at boot: "); p=udec(p,(u32)g_pci_quiesced); *p=0; term_puts(b); term_putc('\n');
    p=b; p=scopy(p,"DMA arena next free: "); p=hx64(p,dma_cur); p=scopy(p,"   page pool free frames: "); p=udec(p,pframe_free_count()); *p=0; term_puts(b); term_putc('\n'); }
static u64 mt_pat(u64 a){ return (a*0x9E3779B97F4A7C15ULL)^0x5A5AC3C3A5A53C3CULL; }
static u32 g_mt_bad, g_mt_late; static u64 g_mt_first, g_mt_firstlate, g_mt_seen;
static void mt_fill(u64 s,u64 e){ for(u64 a=s;a+8<=e;a+=8){ *(volatile u64*)a=mt_pat(a); } }
static void mt_check(u64 s,u64 e,int late){ for(u64 a=s;a+8<=e;a+=8){ u64 v=*(volatile u64*)a; if(v!=mt_pat(a)){ if(late){ if(!g_mt_late){ g_mt_firstlate=a; g_mt_seen=v; } g_mt_late++; } else { if(!g_mt_bad){ g_mt_first=a; g_mt_seen=v; } g_mt_bad++; } *(volatile u64*)a=mt_pat(a); } } }
static void mt_wait(u32 ms){ u64 t0=g_ticks; u32 lim=ms/10u; cpu_irq_on(); for(u32 i=0;i<ms*3000u;i++){ if(g_ticks_live&&g_ticks-t0>=lim)break; cpu_pause(); } }
static int mt_ranges(u64* rs,u64* re,const char** rn){ int n=0;
    { u64 c=(dma_cur+4095)&~4095ULL; while(c<DMA_ARENA_TOP&&n<14){ u64 bs,be; u32 bt; if(mm_first_bad_ex(c,DMA_ARENA_TOP,1,&bs,&be,&bt)){ if(bs>c){ rs[n]=c; re[n]=bs; rn[n++]="DMA arena"; } c=be; } else { rs[n]=c; re[n]=DMA_ARENA_TOP; rn[n++]="DMA arena"; break; } } }
    { u32 i=0; while(i<pf_n&&n<20){ while(i<pf_n&&(pf_bitmap[i>>3]&(1u<<(i&7))))i++; u32 j=i; while(j<pf_n&&!(pf_bitmap[j>>3]&(1u<<(j&7)))&&(j==i||pf_pa(j)==pf_pa(j-1)+4096ULL))j++; if(j>i){ rs[n]=pf_pa(i); re[n]=pf_pa(j-1)+4096ULL; rn[n++]="page pool"; } i=j; } }
    if(!*(volatile u32*)0x5010ULL&&!g_doom_wad&&mm_is_usable(0x0E000000ULL,0x10000000ULL)&&n<22){ rs[n]=0x0E000000ULL; re[n]=0x10000000ULL; rn[n++]="module area"; }
    return n; }
void memtest_run(void){ char b[200],*p;
    { int nb=0,bad=0; u64 badat=0; for(hblk* h=heap_head; h&&nb<1000000; h=h->next){ u64 a=(u64)h; if(a<heap_base||a>=heap_top||h->magic!=HBLK_MAGIC){ bad=1; badat=a; break; } nb++; }
      p=b; p=scopy(p,"heap blocks: "); p=udec(p,(u32)nb); if(bad){ p=scopy(p,"  DAMAGED block header at "); p=hx64(p,badat); } else p=scopy(p,"  all headers intact"); *p=0; tcol(bad?rgb(240,120,120):GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
    u64 rs[24],re[24]; const char* rn[24]; int n=mt_ranges(rs,re,rn);
    u8* hb[16]; int hn=0; for(;hn<16;hn++){ hb[hn]=(u8*)kmalloc(16u*1024u*1024u); if(!hb[hn])break; if(n<24){ rs[n]=((u64)hb[hn]+7)&~7ULL; re[n]=(u64)hb[hn]+16u*1024u*1024u; rn[n++]="heap"; } }
    u64 tot=0; for(int i=0;i<n;i++)tot+=re[i]-rs[i];
    p=b; p=scopy(p,"testing "); p=udec(p,(u32)(tot>>20)); p=scopy(p," MB of free memory (fill, read back, wait 3 s, read again)..."); *p=0; term_puts(b); term_putc('\n'); blit_all();
    g_mt_bad=0; g_mt_late=0;
    for(int i=0;i<n;i++)mt_fill(rs[i],re[i]);
    for(int i=0;i<n;i++)mt_check(rs[i],re[i],0);
    u32 bad0=g_mt_bad; u64 f0=g_mt_first, s0=g_mt_seen;
    mt_wait(3000);
    for(int i=0;i<n;i++)mt_check(rs[i],re[i],1);
    for(int k=0;k<hn;k++)kfree(hb[k]);
    for(int i=0;i<n;i++){ p=b; p=scopy(p,"  "); p=scopy(p,rn[i]); while(p-b<16)*p++=' '; p=hx64(p,rs[i]); *p++='-'; p=hx64(p,re[i]-1); *p=0; term_puts(b); term_putc('\n'); if(i>=11&&n>13){ term_puts("  ...\n"); break; } }
    if(!bad0&&!g_mt_late){ tcol(GREEN); term_puts("memory test passed: every byte kept its value\n"); tcol(WHT); return; }
    tcol(rgb(240,120,120));
    if(bad0){ p=b; p=udec(p,bad0); p=scopy(p," words did not keep their value, first at "); p=hx64(p,f0); p=scopy(p,"  read "); p=hx64(p,s0); *p=0; term_puts(b); term_putc('\n'); }
    if(g_mt_late){ p=b; p=udec(p,g_mt_late); p=scopy(p," words were changed by something else within 3 s, first at "); p=hx64(p,g_mt_firstlate); p=scopy(p,"  read "); p=hx64(p,g_mt_seen); *p=0; term_puts(b); term_putc('\n');
        p=b; p=scopy(p,"  bytes there now:"); for(int i=0;i<32;i++){ *p++=' '; p=hx(p,*(volatile u8*)(g_mt_firstlate+(u64)i),2); } *p=0; term_puts(b); term_putc('\n'); }
    tcol(WHT); }
static char g_ip_e1[120], g_ip_e2[120];
static int ip_mem_verify(const u8* buf,const u8* ref,u32 len){
    u32 first=0xFFFFFFFFu, cnt=0; for(u32 i=0;i<len;i++) if(buf[i]!=ref[i]){ if(first==0xFFFFFFFFu)first=i; cnt++; }
    if(!cnt)return 0;
    u8* w=(u8*)buf; for(u32 i=0;i<len;i++)w[i]=ref[i];
    u32 now=0; for(u32 i=0;i<len;i++) if(buf[i]!=ref[i])now++;
    mt_wait(1000);
    u32 late=0, lf=0xFFFFFFFFu; for(u32 i=0;i<len;i++) if(buf[i]!=ref[i]){ if(lf==0xFFFFFFFFu)lf=i; late++; }
    u64 at=(u64)buf+first; u64 bs,be; u32 bt; int inmap=mm_known()&&mm_first_bad(at,at+1,&bs,&be,&bt);
    char* p=g_ip_e1; p=scopy(p,"Memory at "); p=hx64(p,at); p=scopy(p,now?" does not keep data":late?" was changed by something else":" was changed once"); *p=0;
    p=g_ip_e2; p=udec(p,cnt); p=scopy(p," differed"); if(late){ p=scopy(p,", "); p=udec(p,late); p=scopy(p," again at "); p=hx64(p,(u64)buf+lf); *p++=':'; for(u32 i=0;i<8&&lf+i<len;i++){ *p++=' '; p=hx(p,buf[lf+i],2); } } if(inmap){ p=scopy(p,", fw: "); p=scopy(p,mm_tname(bt)); } *p=0;
    return 1; }
typedef struct { u8* img; u32 bps,res,nf,fatsz,dstart,psec,next; int err; } ifat;
static u64 if_off(ifat* f,u32 c){ return (u64)(f->dstart+(c-2))*f->bps; }
static void if_set(ifat* f,u32 c,u32 v){ for(u32 i=0;i<f->nf;i++) *(u32*)(f->img+(u64)(f->res+i*f->fatsz)*f->bps+(u64)c*4)=v&0x0FFFFFFFu; }
static u32 if_alloc(ifat* f,u32 n){ if(!n)n=1; u32 first=f->next;
    if((u64)f->dstart+(u64)(f->next-2)+(u64)n>(u64)f->psec){ f->err=1; return 0; }
    for(u32 i=0;i<n;i++){ u32 c=f->next++; if_set(f,c,(i==n-1)?0x0FFFFFFFu:(c+1)); } return first; }
static u32 if_file(ifat* f,const u8* d,u32 len){ u32 c0=if_alloc(f,(len+f->bps-1)/f->bps); if(f->err)return 0; u8* p=f->img+if_off(f,c0); for(u32 i=0;i<len;i++)p[i]=d[i]; return c0; }
static void if_ent(ifat* f,u32 dirc,int slot,const char* nm,u8 attr,u32 clus,u32 size){
    u8* e=f->img+if_off(f,dirc)+(u64)slot*32; u8 n[11];
    if(nm[0]=='.'&&(nm[1]==0||(nm[1]=='.'&&nm[2]==0))){ for(int i=0;i<11;i++)n[i]=' '; n[0]='.'; if(nm[1])n[1]='.'; }
    else if(attr==0x08){ for(int i=0;i<11;i++){ char c=nm[i]; if(!c){ for(;i<11;i++)n[i]=' '; break; } if(c>='a'&&c<='z')c-=32; n[i]=(u8)c; } }
    else ins83(nm,n);
    for(int i=0;i<11;i++)e[i]=n[i]; e[11]=attr; *(u16*)(e+20)=(u16)(clus>>16); *(u16*)(e+26)=(u16)(clus&0xFFFF); *(u32*)(e+28)=size;
    *(u16*)(e+14)=0x6000; *(u16*)(e+16)=0x5B3B; *(u16*)(e+22)=0x6000; *(u16*)(e+24)=0x5B3B; *(u16*)(e+18)=0x5B3B; }
static int if_fits83(const char* nm){ int i=0, dot=-1; for(;nm[i];i++){ char c=nm[i]; if(c=='.'){ if(dot>=0)return 0; dot=i; continue; } if(c>='a'&&c<='z')return 0; if(c==' ')return 0; }
    if(dot<0)return i>=1&&i<=8; return dot>=1&&dot<=8&&i-dot-1>=1&&i-dot-1<=3; }
static void if_alias(const char* nm,int seq,u8 out[11]){ for(int i=0;i<11;i++)out[i]=' '; int o=0; const char* ext=0; for(const char* q=nm;*q;q++) if(*q=='.')ext=q+1;
    for(const char* q=nm;*q&&q!=ext-1&&o<6;q++){ char c=*q; if(c==' '||c=='.')continue; if(c>='a'&&c<='z')c-=32; out[o++]=(u8)c; }
    out[o++]='~'; out[o++]=(u8)('0'+(seq%10));
    if(ext){ int k=0; for(const char* q=ext;*q&&k<3;q++){ char c=*q; if(c>='a'&&c<='z')c-=32; out[8+k++]=(u8)c; } } }
static void if_add(ifat* f,u32 dirc,int* slot,const char* nm,u8 attr,u32 clus,u32 size){
    if(if_fits83(nm)){ if_ent(f,dirc,*slot,nm,attr,clus,size); (*slot)++; return; }
    static int seq=1; u8 sn[11]; if_alias(nm,seq++,sn); u8 ck=0; for(int i=0;i<11;i++)ck=(u8)(((ck&1)?0x80:0)+(ck>>1)+sn[i]);
    int len=0; while(nm[len])len++; int nl=(len+12)/13;
    for(int e=nl;e>=1;e--){ u8* d=f->img+if_off(f,dirc)+(u64)(*slot)*32; for(int i=0;i<32;i++)d[i]=0;
        d[0]=(u8)(e|(e==nl?0x40:0)); d[11]=0x0F; d[13]=ck;
        static const u8 pos[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
        for(int k=0;k<13;k++){ int ci=(e-1)*13+k; u16 v=ci<len?(u16)(u8)nm[ci]:ci==len?0:0xFFFF; d[pos[k]]=(u8)v; d[pos[k]+1]=(u8)(v>>8); }
        (*slot)++; }
    u8* e=f->img+if_off(f,dirc)+(u64)(*slot)*32; for(int i=0;i<11;i++)e[i]=sn[i]; e[11]=attr; *(u16*)(e+20)=(u16)(clus>>16); *(u16*)(e+26)=(u16)(clus&0xFFFF); *(u32*)(e+28)=size;
    *(u16*)(e+14)=0x6000; *(u16*)(e+16)=0x5B3B; *(u16*)(e+22)=0x6000; *(u16*)(e+24)=0x5B3B; *(u16*)(e+18)=0x5B3B; (*slot)++; }
static u32 if_mkdir(ifat* f,u32 parent,int* pslot,const char* nm,u8 attr){ u32 d=if_alloc(f,2); if(f->err)return 0; if_add(f,parent,pslot,nm,attr,d,0); if_ent(f,d,0,".",0x10,d,0); if_ent(f,d,1,"..",0x10,parent==2?0:parent,0); return d; }
static const char IF_INFO[]="NoovexOS 64 v9.83\r\n\r\nThis disk was set up by the NoovexOS installer.\r\n\r\nEFI\\BOOT\\BOOTX64.EFI       UEFI boot loader (started by the firmware)\r\nEFI\\NoovexOS\\              copy of the boot loader and its settings\r\nNoovexOS\\System\\          system files and drivers\r\nNoovexOS\\Boot\\            boot settings\r\nNoovexOS\\Logs\\            log files\r\nProgram Files\\            installed programs\r\nProgramData\\              settings shared by all users\r\nUsers\\                    user folders\r\nboot\\kernel64.bin, KERNEL64.BIN, boot.cfg   hidden system files used to start NoovexOS\r\n";
static u32 inst_fs_layout(u32 bps,u32 psec,u32 klen,u32* fatsz,u32* dstart){
    u32 res=32, clus=psec-res; u32 fs=((clus+2)*4+bps-1)/bps; u32 ds=res+2*fs;
    u32 cfglen=(u32)(_binary_inst_boot_cfg_end-_binary_inst_boot_cfg_start), efilen=(u32)(_binary_inst_efi_bin_end-_binary_inst_efi_bin_start);
    u32 need=3+2*((klen+bps-1)/bps)+2*((cfglen+bps-1)/bps)+2*((efilen+bps-1)/bps)+2+3+60+8;
    *fatsz=fs; *dstart=ds; return ds+need; }
static int inst_build_fs(u8* img,u32 bps,u32 plba,u32 psec,u32 cap,const char* label,const u8* k,u32 klen,u32* used,u64* ko1,u64* ko2){
    ifat F; F.img=img; F.bps=bps; F.res=32; F.nf=2; F.psec=cap; F.next=3; F.err=0;
    u32 ds=0; inst_fs_layout(bps,psec,klen,&F.fatsz,&ds); F.dstart=ds;
    if(psec-F.dstart<65525u)return -2;
    for(u64 i=0;i<(u64)cap*bps;i++)img[i]=0;
    u8* bs=img; bs[0]=0xEB;bs[1]=0x58;bs[2]=0x90; { const char* o="NOOVEXOS"; for(int i=0;i<8;i++)bs[3+i]=(u8)o[i]; }
    *(u16*)(bs+11)=(u16)bps; bs[13]=1; *(u16*)(bs+14)=(u16)F.res; bs[16]=(u8)F.nf; bs[21]=0xF8; *(u16*)(bs+24)=63; *(u16*)(bs+26)=255;
    *(u32*)(bs+28)=plba; *(u32*)(bs+32)=psec; *(u32*)(bs+36)=F.fatsz; *(u32*)(bs+44)=2; *(u16*)(bs+48)=1; *(u16*)(bs+50)=6;
    bs[64]=0x80; bs[66]=0x29; *(u32*)(bs+67)=0x4E56584Fu^(u32)g_ticks;
    { int e=0; for(int i=0;i<11;i++){ char c=(!e&&label&&label[i])?label[i]:(e=1,' '); if(c>='a'&&c<='z')c-=32; bs[71+i]=(u8)c; } }
    { const char* ft="FAT32   "; for(int i=0;i<8;i++)bs[82+i]=(u8)ft[i]; } *(u16*)(bs+510)=0xAA55; if(bps>512)*(u16*)(bs+bps-2)=0xAA55;
    u8* fsi=img+bps; *(u32*)fsi=0x41615252u; *(u32*)(fsi+484)=0x61417272u; *(u32*)(fsi+488)=0xFFFFFFFFu; *(u32*)(fsi+492)=0xFFFFFFFFu; *(u32*)(fsi+508)=0xAA550000u;
    if_set(&F,0,0x0FFFFFF8u); if_set(&F,1,0x0FFFFFFFu); if_set(&F,2,0x0FFFFFFFu);
    const u8* cfg=_binary_inst_boot_cfg_start; u32 cfglen=(u32)(_binary_inst_boot_cfg_end-_binary_inst_boot_cfg_start);
    const u8* efi=_binary_inst_efi_bin_start; u32 efilen=(u32)(_binary_inst_efi_bin_end-_binary_inst_efi_bin_start);
    { u32 rx=if_alloc(&F,3); if(F.err||rx!=3)return -1; if_set(&F,2,rx); }
    u32 dboot=if_alloc(&F,1), defi=if_alloc(&F,2), defib=if_alloc(&F,1);
    u32 ck=if_file(&F,k,klen), ck2=if_file(&F,k,klen), cc=if_file(&F,cfg,cfglen), ce=efilen?if_file(&F,efi,efilen):0;
    if(F.err)return -1;
    char vl[12]; { int i=0; for(;i<11&&label&&label[i];i++)vl[i]=label[i]; vl[i]=0; if(!i){ vl[0]='N';vl[1]='O';vl[2]='O';vl[3]='V';vl[4]='E';vl[5]='X';vl[6]=0; } }
    if_ent(&F,2,0,vl,0x08,0,0);
    if_ent(&F,2,1,"boot",0x16,dboot,0); if_ent(&F,2,2,"boot.cfg",0x26,cc,cfglen); if_ent(&F,2,3,"KERNEL64.BIN",0x26,ck2,klen);
    int rs=4;
    if(efilen){ if_ent(&F,2,rs++,"EFI",0x10,defi,0); }
    if_ent(&F,dboot,0,".",0x10,dboot,0); if_ent(&F,dboot,1,"..",0x10,0,0); if_ent(&F,dboot,2,"kernel64.bin",0x20,ck,klen);
    if(efilen){ if_ent(&F,defi,0,".",0x10,defi,0); if_ent(&F,defi,1,"..",0x10,0,0); if_ent(&F,defi,2,"BOOT",0x10,defib,0);
        if_ent(&F,defib,0,".",0x10,defib,0); if_ent(&F,defib,1,"..",0x10,defi,0); if_ent(&F,defib,2,"BOOTX64.EFI",0x20,ce,efilen);
        int es=3; u32 dnv=if_mkdir(&F,defi,&es,"NoovexOS",0x10); int ns=2; u32 ce2=if_file(&F,efi,efilen), cc2=if_file(&F,cfg,cfglen);
        if(!F.err&&dnv){ if_add(&F,dnv,&ns,"BOOTX64.EFI",0x20,ce2,efilen); if_add(&F,dnv,&ns,"boot.cfg",0x20,cc2,cfglen); } }
    { u32 ilen=sizeof(IF_INFO)-1;
      u32 dsys=if_mkdir(&F,2,&rs,"NoovexOS",0x10); int ss=2;
      u32 dsy2=if_mkdir(&F,dsys,&ss,"System",0x10); { int q=2; if_mkdir(&F,dsy2,&q,"Drivers",0x10); if_mkdir(&F,dsy2,&q,"Config",0x10); u32 ci=if_file(&F,(const u8*)IF_INFO,ilen); if_add(&F,dsy2,&q,"version.txt",0x21,ci,ilen); }
      { u32 dbt=if_mkdir(&F,dsys,&ss,"Boot",0x10); int q=2; u32 cc3=if_file(&F,cfg,cfglen); if_add(&F,dbt,&q,"boot.cfg",0x20,cc3,cfglen); }
      if_mkdir(&F,dsys,&ss,"Fonts",0x10); if_mkdir(&F,dsys,&ss,"Logs",0x10); if_mkdir(&F,dsys,&ss,"Temp",0x10);
      if_mkdir(&F,2,&rs,"Program Files",0x10); if_mkdir(&F,2,&rs,"ProgramData",0x10);
      u32 du=if_mkdir(&F,2,&rs,"Users",0x10); int us=2; if_mkdir(&F,du,&us,"Public",0x10);
      u32 dd=if_mkdir(&F,du,&us,"Default",0x10); { int q=2; const char* sub[6]={"Desktop","Documents","Downloads","Pictures","Music","Videos"}; for(int k2=0;k2<6;k2++)if_mkdir(&F,dd,&q,sub[k2],0x10); }
      u32 cr=if_file(&F,(const u8*)IF_INFO,ilen); if_add(&F,2,&rs,"README.txt",0x21,cr,ilen); }
    if(F.err)return -1;
    { u32 tot=psec-F.dstart; *(u32*)(fsi+488)=tot>(F.next-2)?tot-(F.next-2):0; *(u32*)(fsi+492)=F.next; }
    for(u32 i=0;i<2*bps;i++)img[6*bps+i]=img[i];
    *used=F.dstart+(F.next-2); *ko1=if_off(&F,ck); *ko2=if_off(&F,ck2);
    return 0;
}

enum { IP_WELCOME, IP_TARGET, IP_CONFIRM, IP_RUN, IP_DONE, IP_ERROR, IP_FS };
static int g_ip_fs=0;
static int ip_page=IP_WELCOME, ip_mode=0, ip_part=-1, ip_copy=1, ip_ack=0, ip_ret=ST_BOOTMENU, ip_stage=-1, ip_prog=0;
static const char* ip_err=""; static const char* ip_err2="";
static u8 ip_mbr[512]; static int ip_ptype[4]; static u32 ip_pst[4], ip_psz[4]; static int ip_gpt=0;
static int ip_bx[24], ip_by[24], ip_bw[24], ip_bh[24], ip_bid[24], ip_bn=0;
static const char* IP_STAGES[8]={"Checking the kernel image","Preparing the boot partition","Writing the partition table","Installing the boot loader","Writing system files","Creating the data partition","Copying your files","Verifying the installation"};
static void ip_btnreg(int x,int y,int w,int h,int id){ if(ip_bn<24){ ip_bx[ip_bn]=x; ip_by[ip_bn]=y; ip_bw[ip_bn]=w; ip_bh[ip_bn]=h; ip_bid[ip_bn]=id; ip_bn++; } }
void ip_load(void){
    gdisk_ensure(); for(int i=0;i<512;i++)ip_mbr[i]=0; ip_gpt=0;
    if(g_dktype!=DK_NONE)gdisk_read(0,1,ip_mbr);
    int valid=(ip_mbr[510]==0x55&&ip_mbr[511]==0xAA);
    for(int e=0;e<4;e++){ u8* pe=ip_mbr+446+e*16; ip_ptype[e]=valid?pe[4]:0; ip_pst[e]=*(u32*)(pe+8)*dk_ratio(); ip_psz[e]=*(u32*)(pe+12)*dk_ratio();
        if(!ip_psz[e])ip_ptype[e]=0; if(ip_ptype[e]==0xEE)ip_gpt=1; }
}
static int g_ip_focus=2, g_ip_lastpg=-1;
void dm_open(int ret){ dm_load(); g_dm_open=1; g_dm_sel=-1; g_dm_editname=0; g_dm_msg=0; g_dm_ret=ret; g_state=ST_DISKMGR; }
void ip_open(int ret){ dm_load(); g_ip_lastpg=-1; g_ip_focus=2; ip_ret=ret; ip_page=IP_FS; g_ip_fs=0; ip_mode=0; ip_part=-1; ip_ack=0; ip_copy=1; ip_stage=-1; ip_prog=0; ip_load(); g_state=ST_INSTALL; }
static u32 ip_need(void){ return INST_BOOTP+INST_DATAP; }
static int ip_disk_ok(void){ return g_dktype!=DK_NONE&&g_dksectors>=2048u+ip_need()+2048u&&!(g_dktype==DK_NVME&&!g_nvme_bsz); }
static int ip_free_slot(int except){ for(int e=0;e<4;e++) if(e!=except&&!ip_ptype[e])return e; return -1; }
static int ip_part_ok(int e){ if(e<0||e>3||!ip_ptype[e]||ip_gpt)return 0; if(ip_psz[e]<ip_need())return 0; if(ip_pst[e]<64)return 0; if(ip_free_slot(e)<0)return 0;
    for(int q=0;q<4;q++) if(q!=e&&ip_ptype[q]&&ip_pst[q]<64)return 0; return 1; }
static const char* ip_tname(int t){ switch(t){ case 0x0B: case 0x0C: return "FAT32"; case 0x07: return "NTFS/exFAT"; case 0x83: return "Linux"; case 0x82: return "Linux swap"; case 0xEE: return "GPT"; case 0xEF: return "EFI"; case NVX_PTYPE: return "NoovexOS"; case 0x05: case 0x0F: return "Extended"; default: return "Other"; } }
static char* ip_size(char* p,u64 secs){ u64 mb=secs*512/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)mb); return scopy(p," MB"); }
static u32 ip_pcol(int t){ if(t==0x0B||t==0x0C)return rgb(80,140,230); if(t==0x07)return rgb(90,170,220); if(t==0x83)return rgb(230,150,60); if(t==NVX_PTYPE)return rgb(110,190,120); if(t==0xEE)return rgb(150,120,200); return rgb(160,168,184); }
static void ip_bar(int x,int y,int w,int h,int preview){
    fill_round(x,y,w,h,8,15,rgb(226,230,238)); if(!g_dksectors)return; u64 tot=g_dksectors;
    if(preview){ u32 bs=2048u,bl=INST_BOOTP,ds=bs+bl,dl=INST_DATAP;
        if(ip_mode==1&&ip_part>=0){ bs=ip_pst[ip_part]; ds=bs+bl; dl=ip_psz[ip_part]-bl;
            for(int e=0;e<4;e++) if(ip_ptype[e]&&e!=ip_part){ int sx=x+(int)((u64)ip_pst[e]*w/tot), sw=(int)((u64)ip_psz[e]*w/tot); if(sw<3)sw=3; fill_round(sx,y,sw,h,4,15,ip_pcol(ip_ptype[e])); } }
        int sx=x+(int)((u64)bs*w/tot), sw=(int)((u64)bl*w/tot); if(sw<6)sw=6; fill_round(sx,y,sw,h,4,15,ACC);
        int dx=x+(int)((u64)ds*w/tot), dw=(int)((u64)dl*w/tot); if(dw<6)dw=6; if(dx<sx+sw)dx=sx+sw; fill_round(dx,y,dw,h,4,15,rgb(110,190,120)); return; }
    for(int e=0;e<4;e++) if(ip_ptype[e]){ int sx=x+(int)((u64)ip_pst[e]*w/tot), sw=(int)((u64)ip_psz[e]*w/tot); if(sw<4)sw=4; if(sx+sw>x+w)sw=x+w-sx;
        fill_round(sx,y,sw,h,4,15,(ip_mode==1&&ip_part==e)?ACC:ip_pcol(ip_ptype[e]));
}
}
static void ip_button(int x,int y,int w,const char* t,int id,int kind,int enabled){
    int hov=enabled&&in_rect(mx,my,x,y,w,40); u32 bg,fg;
    if(kind==1){ bg=enabled?(hov?ACC2:ACC):rgb(196,204,222); fg=rgb(255,255,255); }
    else if(kind==2){ bg=enabled?(hov?rgb(196,52,52):rgb(216,64,64)):rgb(226,190,190); fg=rgb(255,255,255); }
    else { bg=hov?rgb(226,230,238):rgb(238,241,246); fg=enabled?INK:rgb(170,176,190); }
    fill_round(x,y,w,40,10,15,bg); dstr_ctr(x+w/2,y+13,t,fg,1); if(enabled)ip_btnreg(x,y,w,40,id); }
static void ip_check(int x,int y,int on,const char* t,int id,u32 col){
    int hov=in_rect(mx,my,x,y,slen(t)*8+34,24); fill_round(x,y+2,20,20,5,15,on?ACC:(hov?rgb(214,220,232):rgb(226,230,238)));
    if(on){ for(int k=0;k<5;k++){ putpx(x+5+k,y+11+k,rgb(255,255,255)); putpx(x+5+k,y+12+k,rgb(255,255,255)); } for(int k=0;k<8;k++){ putpx(x+10+k,y+15-k,rgb(255,255,255)); putpx(x+10+k,y+16-k,rgb(255,255,255)); } }
    dstr(x+30,y+6,t,col,1); ip_btnreg(x,y,slen(t)*8+34,24,id); }

static int ip_btns(int* o){
    if(ip_page==IP_FS){ o[0]=2; return 1; }
    if(ip_page==IP_WELCOME||ip_page==IP_TARGET){ o[0]=2; o[1]=6; return 2; }
    if(ip_page==IP_CONFIRM){ o[0]=1; o[1]=2; return 2; }
    if(ip_page==IP_DONE){ o[0]=5; return 1; }
    if(ip_page==IP_ERROR){ o[0]=3; return 1; }
    return 0;
}
static void ip_focus_move(int dir){ int b[4]; int n=ip_btns(b); if(n<2)return; int k=0; for(int i=0;i<n;i++) if(b[i]==g_ip_focus)k=i; k=(k+dir+n)%n; g_ip_focus=b[k]; }
static void ip_cbtn(int cx,int by,const char* t,int id){ int w=slen(t)*8+32; int x=cx-w/2;
    int foc=(id==g_ip_focus), hov=in_rect(mx,my,x,by,w,20);
    if(foc){ fill_rect(x,by,w,20,hov?rgb(230,80,90):rgb(210,60,70)); }
    u32 c=foc?rgb(255,255,255):(hov?rgb(255,120,130):rgb(210,60,70));
    dstr(x+4,by+3,"<",c,1); dstr(x+16,by+3,t,c,1); dstr(x+16+slen(t)*8+4,by+3,">",c,1); ip_btnreg(x,by,w,20,id); }
void ip_box(int* ox,int* oy,int* ow,int bw,int bh,const char* title){
    int bx=((int)W-bw)/2, by=((int)H-bh)/2;
    fill_rect(bx+10,by+10,bw,bh,rgb(0,0,0));                 /* hard black shadow */
    fill_rect(bx,by,bw,bh,rgb(248,249,251));                /* white box */
    fill_rect(bx+3,by+3,bw-6,2,rgb(20,24,34)); fill_rect(bx+3,by+bh-5,bw-6,2,rgb(20,24,34));
    fill_rect(bx+3,by+3,2,bh-6,rgb(20,24,34)); fill_rect(bx+bw-5,by+3,2,bh-6,rgb(20,24,34));
    { int tw=slen(title)*8; int full=tw+32; int fx=bx+(bw-full)/2;   /* "[ " + title + " ]" */
      for(int gx=bx+10;gx<fx-4;gx+=8)dstr(gx,by+6,"-",rgb(20,24,34),1);
      for(int gx=fx+full+4;gx<bx+bw-10;gx+=8)dstr(gx,by+6,"-",rgb(20,24,34),1);
      dstr(fx,by+6,"[",rgb(20,24,34),1); dstr(fx+16,by+6,(char*)title,rgb(210,60,70),1); dstr(fx+16+tw,by+6,"]",rgb(20,24,34),1); }
    *ox=bx+18; *oy=by+30; *ow=bw-36;
}
int g_ip_curmoved=0;
static int ip_hover(void){ for(int b=ip_bn-1;b>=0;b--) if(in_rect(mx,my,ip_bx[b],ip_by[b],ip_bw[b],ip_bh[b])) return b; return -1; }
void ip_present(void){
    if(ip_page==IP_RUN){ static u64 lt=0; static int lp=-1; if(ip_prog/10==lp/10 && g_ticks-lt<10 && ip_prog<1000) return; lt=g_ticks; lp=ip_prog; }
    g_ip_curmoved=0;
    if(ip_page!=g_ip_lastpg){ g_ip_lastpg=ip_page; g_ip_focus=(ip_page==IP_DONE)?5:(ip_page==IP_ERROR)?3:2; }
    ip_bn=0;
    fill_rect(0,0,(int)W,(int)H,rgb(72,130,200));
    int ox,oy,ow;
    if(ip_page==IP_FS){
        ip_box(&ox,&oy,&ow,560,216,"Select File System");
        dstr(ox,oy,"Choose the file system for NoovexOS:",rgb(20,24,34),1); oy+=28;
        { const char* nm[4]={"NoovexFS","NTFS","FAT32","FAT16"}; const char* ds[4]={"native, recommended","also readable by Windows","readable almost everywhere","classic, partitions up to 2 GB"};
          for(int i=0;i<4;i++){ int sel=(g_ip_fs==i); fill_rect(ox,oy-2,ow,20,sel?rgb(20,24,34):rgb(248,249,251));
            char b[80],*p=b; p=scopy(p,sel?"(*) ":"( ) "); p=scopy(p,nm[i]); while(p-b<16)*p++=' '; p=scopy(p,ds[i]); *p=0;
            dstr(ox+8,oy+2,b,sel?rgb(255,255,255):rgb(20,24,34),1); ip_btnreg(ox,oy-2,ow,20,40+i); oy+=22; } }
        oy+=18;
        ip_cbtn(ox+ow/2, oy, "Continue", 2);
    }
    else if(ip_page==IP_WELCOME || ip_page==IP_TARGET){
        extern int gdisk_count(void); extern int gdisk_current(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int);
        int nd=gdisk_count(); if(nd<1)nd=1; if(nd>6)nd=6;
        ip_box(&ox,&oy,&ow,660,172+(nd-1)*24,"Select Target Disk");
        dstr(ox,oy,"Choose the disk to install NoovexOS to (click it or use Up/Down):",rgb(20,24,34),1); oy+=18;
        dstr(ox,oy,"Or Skip to set up NoovexOS without installing.  Press P to edit partitions.",rgb(110,118,135),1); oy+=28;
        for(int i=0;i<nd;i++){ int sel=(i==gdisk_current()); int bx2=ox; int rww=ow;
          fill_rect(bx2,oy-2,rww,20, sel?rgb(20,24,34):rgb(238,240,244));
          char b[80],*p=b; p=scopy(p,sel?"(*) ":"( ) "); p=scopy(p,gdisk_count()?(char*)gdisk_name_of(i):"/dev/hda"); p=scopy(p,"   -   "); u32 sz=gdisk_count()?gdisk_size_of(i):0; if(sz)p=ip_size(p,sz); else p=scopy(p,"no disk found"); *p=0;
          dstr(bx2+8,oy+2,b, sel?rgb(255,255,255):rgb(20,24,34),1);
          ip_btnreg(bx2,oy-2,rww,20,60+i); oy+=24; }
        if(g_dm_np>0){ char b[200],*p=b; int any=0; p=scopy(p,"Partitions: ");
            for(int i=0;i<g_dm_np&&i<4;i++){ if(!g_dm_p[i].used)continue; if(any)p=scopy(p,", "); any=1; p=scopy(p,g_dm_p[i].name); p=scopy(p," "); p=dm_size(p,g_dm_p[i].size); if(g_dm_p[i].fs[0]){ p=scopy(p," "); int k=0; while(g_dm_p[i].fs[k]&&k<8){ *p++=g_dm_p[i].fs[k]; k++; } } }
            if(!any)p=scopy(p,"none (empty disk)"); *p=0; dstr(ox,oy,b,rgb(70,78,95),1); }
        if(!gdisk_count()){ extern const char* g_vmd_note; extern u32 g_vmd_dev; char b[160],*p=b; if(g_vmd_dev){ p=scopy(p,g_vmd_note); if(nvme_found&&!nvme_ok){ p=scopy(p," - NVMe start failed at stage "); p=udec(p,(u32)nvme_failstage); } } else p=scopy(p,"No disk found. Run disks in the terminal and send a photo."); *p=0; dstr(ox,oy,b,rgb(190,60,50),1); }
        oy+=16;
        ip_cbtn(ox+ow/3, oy, "Continue", 2); ip_cbtn(ox+ow*2/3, oy, "Skip", 6);
    }
    else if(ip_page==IP_CONFIRM){
        ip_box(&ox,&oy,&ow,560,182,"Confirm Installation");
        dstr(ox,oy,"NoovexOS will be installed to:",rgb(20,24,34),1); oy+=24;
        { char b[80],*p=b; p=scopy(p,"  "); p=scopy(p,gdisk_name()[0]?gdisk_name():"/dev/hda"); *p=0; dstr(ox,oy,b,rgb(20,24,34),1); } oy+=22;
        { char b[80],*p=b; p=scopy(p,"File system: "); { const char* fsn[4]={"NoovexFS","NTFS","FAT32","FAT16"}; p=scopy(p,fsn[g_ip_fs&3]); } *p=0; dstr(ox,oy,b,rgb(20,24,34),1); } oy+=24;
        dstr(ox,oy,"All data on this disk will be erased.",rgb(190,40,50),1); oy+=36;
        ip_cbtn(ox+ow/3, oy, "Back", 1); ip_cbtn(ox+ow*2/3, oy, "Install", 2);
    }
    else if(ip_page==IP_RUN){
        ip_box(&ox,&oy,&ow,560,130,"Installing");
        { const char* st[8]={"Preparing disk","Partitioning","Formatting","Copying system","Writing bootloader","Configuring","Syncing","Finishing"}; int si=ip_stage; if(si<0)si=0; if(si>7)si=7;
          dstr(ox,oy,(char*)st[si],rgb(20,24,34),1); } oy+=28;
        int bw2=ow; fill_rect(ox,oy,bw2,18,rgb(220,222,228)); int fw=bw2*ip_prog/1000; fill_rect(ox,oy,fw,18,rgb(40,120,80));
        for(int t=0;t<1;t++){ fill_rect(ox,oy,bw2,1,rgb(20,24,34)); fill_rect(ox,oy+17,bw2,1,rgb(20,24,34)); fill_rect(ox,oy,1,18,rgb(20,24,34)); fill_rect(ox+bw2-1,oy,1,18,rgb(20,24,34)); }
        { char b[8],*p=b; p=udec(p,(u32)(ip_prog/10)); *p++='%'; *p=0; dstr_ctr(ox+bw2/2,oy+3,b,rgb(255,255,255),1); }
    }
    else if(ip_page==IP_DONE){
        ip_box(&ox,&oy,&ow,520,130,"Installation Complete");
        dstr(ox,oy,"NoovexOS has been installed successfully.",rgb(20,24,34),1); oy+=24;
        dstr(ox,oy,"Remove the install media and reboot.",rgb(90,100,120),1); oy+=36;
        ip_cbtn(ox+ow/2, oy, "Reboot", 5);
    }
    else { /* IP_ERROR */
        { int bw=560, l1=ip_err?slen(ip_err):0, l2=ip_err2?slen(ip_err2):0; int lm=l1>l2?l1:l2; if(lm*8+60>bw)bw=lm*8+60; if(bw>(int)W-20)bw=(int)W-20; ip_box(&ox,&oy,&ow,bw,150,"Error"); }
        dstr(ox,oy,ip_err?ip_err:"Installation failed.",rgb(190,40,50),1); oy+=24;
        if(ip_err2)dstr(ox,oy,ip_err2,rgb(90,100,120),1); oy+=36;
        ip_cbtn(ox+ow/2, oy, "Back", 3);
    }
    memcpy(SCENE_NC,SCENE,(u64)W*H*4); draw_cursor(); blit_all();
    nc_restore(mx-CURBB/2,my-CURBB/2,CURBB,CURBB); cxlast=mx; cylast=my;
}
static void ip_fail(const char* a,const char* b){ ip_err=a; ip_err2=b; ip_page=IP_ERROR; ip_present(); }
static void ip_stage_go(int s,int prog){ ip_stage=s; ip_prog=prog; ip_present(); }
static void ip_nk_prog(int done,int total){ ip_stage=6; ip_prog=760+done*100/(total?total:1); ip_present(); }
static void ip_run(void){
    ip_page=IP_RUN; ip_stage_go(0,0);
    u32 klen=*(volatile u32*)(u64)(0x100000ULL+24); if(klen<4096||klen>KSNAP_MAX){ ip_fail("The kernel size stamp is invalid.",""); return; }
    if(!g_kimg_len||klen!=g_kimg_len){ ip_fail("The kernel image could not be captured at boot.","Run kcheck in the terminal and send a photo of the result."); return; }
    if(!kimg_full()){ ip_fail("Not enough memory to prepare the kernel image.",""); return; }
    { int io=-1,fo=-1; int kr=kimg_check(g_kimg_snap,g_kimg_len,&io,&fo);
      if(kr<0||(kr==0&&(!io||!fo))){ ip_fail(kr<0?"The kernel image could not be captured at boot.":!io?"The kernel code loaded at boot is not the original build.":"The kernel data loaded at boot is not the original build.","Run kcheck in the terminal and send a photo of the result."); return; } }
    if(mm_area_conflicts()){ ip_fail("The firmware uses memory that NoovexOS also uses.","Run memmap in the terminal and send a photo of the result."); return; }
    const u8* kimg=g_kimg_snap;
    { extern int disk_ok; extern volatile int g_fs_wb_pending; disk_ok=0; g_fs_wb_pending=0; }
    u32 bst,bsz=INST_BOOTP,dst,dsz; int bslot,dslot;
    if(ip_mode==0){ bst=2048u; dst=bst+bsz; dsz=INST_DATAP; bslot=0; dslot=1;
        if(g_dksectors>dst+INST_DATAP+4096u){ u32 avail=g_dksectors-dst-4096u; u32 cap=g_ip_fs==0?0x8000000u:g_ip_fs==3?4186112u:g_ip_fs==2?0x4000000u:0xFFFFF000u; dsz=avail<cap?avail:cap; } }
    else { if(!ip_part_ok(ip_part)){ ip_fail("The chosen partition can't be used.",""); return; }
        bst=ip_pst[ip_part]; dst=bst+bsz; dsz=ip_psz[ip_part]-bsz; bslot=ip_part; dslot=ip_free_slot(ip_part); }
    if(dsz%dk_ratio())dsz-=dsz%dk_ratio();
    if((u64)dst+dsz>g_dksectors){ char* p=g_ip_e2; p=scopy(p,gdisk_name()); p=scopy(p," disk: "); p=udec(p,(u32)((u64)g_dksectors*512/1048576ULL)); p=scopy(p," MB, needed: "); p=udec(p,(u32)(((u64)dst+dsz)*512/1048576ULL)); p=scopy(p," MB. Use Next disk in the Disk Manager (press P) if this is the wrong disk."); *p=0; ip_fail("The disk is too small.",g_ip_e2); return; }
    if(g_ip_fs==3&&dsz>4186112u){ dsz=4186112u; }
    if(g_ip_fs==3&&dsz<8400u){ ip_fail("The partition is too small for FAT16.","FAT16 needs at least 4 MB."); return; }
    if(g_ip_fs==1&&dsz<32768u){ ip_fail("The partition is too small for NTFS.","NTFS needs at least 16 MB - choose FAT16 instead."); return; }
    if(g_ip_fs==2&&dsz<66600u){ ip_fail("The partition is too small for FAT32.","FAT32 needs at least 33 MB - choose FAT16 instead."); return; }
    ip_stage_go(1,40);
    u32 R=dk_ratio(), bps=512*R, fz=0, dso=0, cap=inst_fs_layout(bps,bsz/R,klen,&fz,&dso);
    if(cap>bsz/R){ ip_fail("The kernel does not fit in the boot partition.",""); return; }
    u8* img=(u8*)kmalloc((u64)cap*bps); if(!img){ ip_fail("Not enough memory to build the boot partition.",""); return; }
    u32 usedn=0; u64 ko1=0,ko2=0; int br=inst_build_fs(img,bps,bst/R,bsz/R,cap,g_dm_name,kimg,klen,&usedn,&ko1,&ko2);
    if(br){ kfree(img); ip_fail("The boot partition image could not be built.",br==-2?"The partition is too small for FAT32 on this disk.":""); return; }
    if(ip_mem_verify(img+ko1,kimg,klen)||ip_mem_verify(img+ko2,kimg,klen)){ kfree(img); ip_fail(g_ip_e1,g_ip_e2); return; }
    u32 used=usedn*R;
    ip_stage_go(2,90);
    u8 mbr[512]; for(int i=0;i<512;i++)mbr[i]=ip_mode==0?0:ip_mbr[i];
    int s1=(int)(_binary_inst_boot_bin_end-_binary_inst_boot_bin_start); for(int i=0;i<446;i++)mbr[i]=i<s1?_binary_inst_boot_bin_start[i]:0;
    for(int e=0;e<4;e++)mbr[446+e*16]=0;
    { u8* pe=mbr+446+bslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[0]=0x80; pe[2]=2; pe[4]=0x0C; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=bst/R; *(u32*)(pe+12)=bsz/R; }
    { u8* pe=mbr+446+dslot*16; for(int i=0;i<16;i++)pe[i]=0; pe[2]=2; pe[4]=g_ip_fs==1?0x07:g_ip_fs==2?0x0C:g_ip_fs==3?0x0E:NVX_PTYPE; pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF; *(u32*)(pe+8)=dst/R; *(u32*)(pe+12)=dsz/R; }
    if(ip_mode==0){ *(u32*)(mbr+440)=0x4E565849u^(u32)g_ticks; } mbr[510]=0x55; mbr[511]=0xAA;
    if(gdisk_write(0,mbr)){ kfree(img); ip_fail("Writing the partition table failed.","The disk reported an error."); return; }
    if(ip_mode==0){ u8 z[512]; for(int i=0;i<512;i++)z[i]=0; for(u32 q=1;q<34u*R;q++)gdisk_write(q,z); if(g_dksectors>34u*R) for(u32 q=g_dksectors-34u*R;q<g_dksectors;q++)gdisk_write(q,z); }
    ip_stage_go(3,110);
    u32 s2len=(u32)(_binary_inst_stage2_bin_end-_binary_inst_stage2_bin_start), s2s=(s2len+511)/512;
    if(R>1)s2s=0;
    for(u32 s=0;s<s2s;s++){ u8 sec[512]; for(int i=0;i<512;i++){ u32 o=s*512+i; sec[i]=o<s2len?_binary_inst_stage2_bin_start[o]:0; } if(gdisk_write(1+s,sec)){ kfree(img); ip_fail("Writing the boot loader failed.",""); return; } }
    ip_stage_go(4,140);
    for(u32 s=0;s<used;s++){ if(gdisk_write(bst+s,img+(u64)s*512)){ kfree(img); ip_fail("Writing the boot partition failed.","The disk reported an error."); return; }
        if((s&127)==0){ ip_prog=140+(int)((u64)s*560/used); ip_present(); } }
    ip_stage_go(5,700);
    { char mk[96],*q=mk; q=scopy(q,"installed="); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,"\n"); *q=0; nvx_mark_file("/etc/nvx-installed",mk); k_unlink("/etc/nvx-setup-done"); }
    if(g_ip_fs){
        const rfs_ops* fo=g_ip_fs==1?&g_rfs_ntfs:g_ip_fs==2?&g_rfs_fat32:&g_rfs_fat16;
        int nr=nk_install(fo,(unsigned long long)dst,(unsigned long long)dsz,ip_copy,ip_nk_prog);
        if(nr){ kfree(img); ip_fail(nr==-1?"Formatting the partition failed.":"Writing files to the new partition failed.",fo->name); return; }
        g_nvx_note=g_ip_fs==1?"NoovexOS NTFS partition":g_ip_fs==2?"NoovexOS FAT32 partition":"NoovexOS FAT16 partition";
    } else {
    g_nvx_generic=1; g_nvx_base=dst; g_nvx_secs=dsz; g_rootfs=0; g_n4_nres=0; disk_ok=1;
    ip_stage_go(6,760);
    if(n4_format(dsz)){ disk_ok=0; kfree(img); ip_fail("Formatting the data partition failed.",g_n4_err); return; }
    g_nvx_note="NoovexOS data partition";
    }
    ip_stage_go(7,870);
    { u8 rb[512]; int bad=0;
      if(gdisk_read(0,1,rb)){ bad=1; } else for(int i=0;i<512;i++) if(rb[i]!=mbr[i]){ bad=1; break; }
      for(u32 s=0;s<s2s&&!bad;s++){ if(gdisk_read(1+s,1,rb)){ bad=1; break; } for(int i=0;i<512;i++){ u32 o=s*512+i; if(rb[i]!=(o<s2len?_binary_inst_stage2_bin_start[o]:0)){ bad=1; break; } } }
      u32 bads=0;
      for(u32 s=0;s<used&&!bad;s++){ if(gdisk_read(bst+s,1,rb)){ bad=2; bads=s; break; } const u8* w=img+(u64)s*512; for(int i=0;i<512;i++) if(rb[i]!=w[i]){ bad=1; bads=s; break; }
          if((s&255)==0){ ip_prog=870+(int)((u64)s*130/used); ip_present(); } }
      if(!bad&&(ip_mem_verify(img+ko1,kimg,klen)||ip_mem_verify(img+ko2,kimg,klen))){ kfree(img); ip_fail(g_ip_e1,g_ip_e2); return; }
      kfree(img);
      if(gdisk_flush()&&!bad){ ip_fail("The disk did not confirm that the data was saved.","Try again, or use another disk."); return; }
      if(bad){ char* p=g_ip_e2; p=scopy(p,bad==2?"Reading sector ":"Sector "); p=udec(p,bst+bads); p=scopy(p,bad==2?" failed (":" differs ("); p=scopy(p,gdisk_name()); if(g_dktype==DK_NVME&&nvme_lasterr){ p=scopy(p,nvme_lasterr==2?", controller fault":", timeout"); } *p++=')'; *p=0;
          ip_fail("Verification failed - the disk returned different data.",g_ip_e2); return; } }
    { char mk[96],*q=mk; q=scopy(q,"installed="); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,"\n"); *q=0; nvx_mark_file("/etc/nvx-installed",mk); }
    ip_prog=1000; ip_stage=8; ip_page=IP_DONE; ip_load(); ip_present();
}
static void ip_pick_disk(int i){ extern int gdisk_count(void); extern int gdisk_pick(int); extern int disk_ok; extern int g_nvx_generic;
    if(i<0||i>=gdisk_count())return; int pr=gdisk_pick(i); if(pr>0){ if(disk_ok&&(g_nvx_generic||g_dktype==DK_IDE))disk_ok=0; } dm_load(); ip_load(); ip_mode=0; ip_part=-1; }
static void ip_act(int id){
    if(ip_page==IP_FS){ if(id>=40&&id<=43){ g_ip_fs=id-40; return; } if(id==2){ ip_page=IP_WELCOME; return; } return; }
    /* compact classic flow:
       disk-select(Continue)=2 -> CONFIRM ; CONFIRM Back=1 Install=2 ; DONE Reboot=5 ; ERROR Back=3 */
    if(ip_page==IP_WELCOME||ip_page==IP_TARGET){
        if(id==10){ return; }
        if(id>=60&&id<66){ ip_pick_disk(id-60); return; }
        if(id==2){ ip_mode=0; ip_part=-1; ip_page=IP_CONFIRM; ip_ack=1; return; }
        if(id==6){ setup_begin(); return; }
        return;
    }
    if(ip_page==IP_CONFIRM){
        if(id==1){ ip_page=IP_TARGET; return; }
        if(id==2){ ip_run(); return; }
        return;
    }
    if(ip_page==IP_DONE){
        if(id==5){ power_reboot(); }
        return;
    }
    if(ip_page==IP_ERROR){
        if(id==3){ ip_page=IP_WELCOME; ip_load(); return; }
        return;
    }
}
int ip_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ int h0=ip_hover(); mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; if(ip_hover()!=h0)changed=1; else g_ip_curmoved=1; }
        if(btn&&!mouse_btn_prev&&ip_page!=IP_RUN){ changed=1; for(int b=ip_bn-1;b>=0;b--) if(in_rect(mx,my,ip_bx[b],ip_by[b],ip_bw[b],ip_bh[b])){ ip_act(ip_bid[b]); break; } }
        mouse_btn_prev=btn; }
    if(xhid_n)xhci_mouse_poll();
    int c; while((c=kpop())>=0){ changed=1;
        if(c==27){ if(ip_page==IP_FS||ip_page==IP_DONE) g_state=ip_ret; else if(ip_page==IP_WELCOME||ip_page==IP_TARGET) ip_page=IP_FS; else if(ip_page==IP_CONFIRM) ip_page=IP_TARGET; else if(ip_page==IP_ERROR){ ip_page=IP_FS; ip_load(); } }
        else if(ip_page==IP_FS&&(c==19||c==20)){ if(c==20&&g_ip_fs<3)g_ip_fs++; else if(c==19&&g_ip_fs>0)g_ip_fs--; }
        else if(c=='\n'){ if(ip_page!=IP_RUN) ip_act(g_ip_focus); }
        else if(c==17||c==18||c=='\t'){ if(ip_page!=IP_RUN) ip_focus_move(c==17?-1:1); }
        else if((ip_page==IP_TARGET||ip_page==IP_WELCOME)&&(c==19||c==20)){ extern int gdisk_current(void); ip_pick_disk(gdisk_current()+(c==20?1:-1)); }
        else if((ip_page==IP_TARGET||ip_page==IP_WELCOME)&&(c=='p'||c=='P')){ dm_open(ST_INSTALL); } }
    return changed;
}
static void ip_open_preset(int ret,int mode,int part){ ip_open(ret); if(!ip_disk_ok())return; ip_mode=mode; ip_part=part; ip_page=(mode==0||ip_part_ok(part))?IP_CONFIRM:IP_TARGET; }
void bootmenu_present(void){
    synthwave_scene();
    if(g_bootcfg){
        int pw=540, ph=310, px=(int)W/2-pw/2, py=(int)H*38/100; shadow_soft(px,py,pw,ph,16); fill_round(px,py,pw,ph,16,15,rgb(250,251,254));
        int x=px+30,y=py+26; dstr(x,y,"Boot Configuration",INK,2); y+=46;
        char b[96],*p; p=b;p=scopy(p,"Resolution:  ");p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p," @ ");p=udec(p,BPP);p=scopy(p,"-bit");*p=0; dstr(x,y,b,INK,1); y+=26;
        p=b;p=scopy(p,"CPU:  ");p=scopy(p,g_brand[0]?g_brand:"x86-64");*p=0; dstr(x,y,b,INK,1); y+=26;
        { int k=nic_index(); p=b;p=scopy(p,"Network:  ");p=scopy(p,k>=0?(char*)nic_name(pci_ven[k],pci_dev[k]):"none detected");*p=0; dstr(x,y,b,INK,1); y+=34; }
        dstr(x,y+7,"Safe mode (plain wallpaper, no animations)",INK,1);
        cfg_tgx=px+pw-76; cfg_tgy=y; cfg_tgw=48; cfg_tgh=28; fill_round(cfg_tgx,cfg_tgy,cfg_tgw,cfg_tgh,7,15,g_safemode?ACC:rgb(224,228,238));
        dstr_ctr(cfg_tgx+cfg_tgw/2,cfg_tgy+7,g_safemode?"ON":"OFF",g_safemode?rgb(255,255,255):rgb(120,128,146),1);
        cfg_bkx=x; cfg_bky=py+ph-54; cfg_bkw=130; cfg_bkh=38; fill_round(cfg_bkx,cfg_bky,cfg_bkw,cfg_bkh,8,15,ACC); dstr_ctr(cfg_bkx+cfg_bkw/2,cfg_bky+9,"Back",rgb(255,255,255),1);
        draw_cursor(); blit_all(); return;
    }
    bm_bw=380; bm_bh=36; bm_bx=(int)W/2-bm_bw/2; int y0=(int)H*60/100;
    for(int k=0;k<5;k++){ int by=y0+k*(bm_bh+10); bm_by[k]=by; int sel=(k==g_bootsel);
        int hov=(mx>=bm_bx&&mx<bm_bx+bm_bw&&my>=by&&my<by+bm_bh);
        if(sel||hov){ shadow_soft(bm_bx,by,bm_bw,bm_bh,10); fill_round(bm_bx,by,bm_bw,bm_bh,10,15,rgb(250,250,255)); dstr(bm_bx+22,by+10,BM_ITEMS[k],rgb(28,18,42),1); }
        else { fill_round(bm_bx,by,bm_bw,bm_bh,10,15,mix(rgb(58,20,80),rgb(255,255,255),44)); dstr(bm_bx+22,by+10,BM_ITEMS[k],rgb(232,216,242),1); } }
    dstr_ctr((int)W/2,(int)H-42,"Up / Down to choose, Enter to select - or click",rgb(212,182,226),1);
    if(g_bootmsg){ int pw=470,ph=160,px=(int)W/2-pw/2,py=(int)H/2-ph/2; shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
        dstr_ctr((int)W/2,py+30,"Disk installer not available",INK,2);
        dstr_ctr((int)W/2,py+70,"This is a read-only build - your disks are untouched.",rgb(90,100,120),1);
        msg_okw=110; msg_okx=(int)W/2-msg_okw/2; msg_oky=py+ph-48; msg_okh=34; fill_round(msg_okx,msg_oky,msg_okw,msg_okh,8,15,ACC); dstr_ctr(msg_okx+msg_okw/2,msg_oky+8,"OK",rgb(255,255,255),1); }
    draw_cursor(); blit_all();
}
static void bootmenu_activate(void){
    if(g_bootsel==0){ g_state=ST_SETUP; }
    else if(g_bootsel==1){ g_safemode=1; g_state=ST_SETUP; }
    else if(g_bootsel==2){ g_state=ST_RECOVERY; g_recmode=0; g_recsel=0; }
    else if(g_bootsel==3){ ip_open(ST_BOOTMENU); }
    else { g_bootcfg=1; }
}
int bootmenu_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(g_bootmsg){ if(in_rect(mx,my,msg_okx,msg_oky,msg_okw,msg_okh)||1) g_bootmsg=0; }
            else if(g_bootcfg){ if(in_rect(mx,my,cfg_tgx,cfg_tgy,cfg_tgw,cfg_tgh)) g_safemode^=1;
                else if(in_rect(mx,my,cfg_bkx,cfg_bky,cfg_bkw,cfg_bkh)) g_bootcfg=0; }
            else { for(int k=0;k<5;k++) if(in_rect(mx,my,bm_bx,bm_by[k],bm_bw,bm_bh)){ g_bootsel=k; bootmenu_activate(); break; } } }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1;
        if(g_bootmsg){ g_bootmsg=0; continue; }
        if(g_bootcfg){ if(c=='\n'||c==27)g_bootcfg=0; else if(c==' ')g_safemode^=1; continue; }
        if(c==19){ g_bootsel=(g_bootsel+4)%5; } else if(c==20){ g_bootsel=(g_bootsel+1)%5; }
        else if(c=='\n'){ bootmenu_activate(); } }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    return changed;
}

