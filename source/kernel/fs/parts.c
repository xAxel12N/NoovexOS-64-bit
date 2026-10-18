#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/fs/parts.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/drivers/storage/gdisk.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/core/klog.h"

enum { BK_MAIN=1, BK_IDE, BK_AHCI, BK_USB };
blkdev g_bd[BD_MAX]; int g_bd_n=0;
partent g_pt[PT_MAX]; int g_pt_n=0;
extern int ahci_port_io(int port,u64 lba,u8* out,u64* sectors);
extern u32 ahci_ports_list(u32* m);
extern int ahci_cur_port(void);
extern int usb_msd_read512(u32 lba,u8* out);
extern u64 usb_msd_sectors(void);
extern int msd_autoinit(void);

static int pio_wait(u16 io,int drq){ for(int i=0;i<2000000;i++){ u8 s=inb(io+7); if(s==0xFF)return -1; if(s&0x80)continue; if(s&0x21)return -1; if(!drq||(s&0x08))return 0; } return -1; }
static int pio_cmd(int unit,u64 lba,int ident,u8* buf){
    u16 io=(unit&2)?0x170:0x1F0; int drv=unit&1;
    if(inb(io+7)==0xFF)return -1;
    if(ident){ outb(io+6,(u8)(0xA0|(drv<<4))); for(int i=0;i<4;i++)inb(io+7); outb(io+2,0); outb(io+3,0); outb(io+4,0); outb(io+5,0); outb(io+7,0xEC);
        if(!inb(io+7))return -1; for(int i=0;i<2000000;i++){ if(!(inb(io+7)&0x80))break; } if(inb(io+4)||inb(io+5))return -1; }
    else { if(pio_wait(io,0))return -1; outb(io+6,(u8)(0x40|(drv<<4))); for(int i=0;i<4;i++)inb(io+7);
        outb(io+2,0); outb(io+3,(u8)(lba>>24)); outb(io+4,(u8)(lba>>32)); outb(io+5,(u8)(lba>>40));
        outb(io+2,1); outb(io+3,(u8)lba); outb(io+4,(u8)(lba>>8)); outb(io+5,(u8)(lba>>16)); outb(io+7,0x24); }
    if(pio_wait(io,1))return -1;
    for(int w=0;w<256;w++){ u16 d=inw(io); buf[w*2]=(u8)d; buf[w*2+1]=(u8)(d>>8); }
    return 0; }
int bd_read(int d,u64 lba,u8* buf){
    if(d<0||d>=g_bd_n)return -1; blkdev* b=&g_bd[d]; if(lba>=b->sectors)return -1;
    switch(b->kind){
        case BK_MAIN: if(lba>0xFFFFFFFFULL)return -1; return gdisk_read((u32)lba,1,buf);
        case BK_IDE: return pio_cmd(b->unit,lba,0,buf);
        case BK_AHCI: return ahci_port_io(b->unit,lba,buf,0);
        case BK_USB: if(lba>0xFFFFFFFFULL)return -1; return usb_msd_read512((u32)lba,buf);
    }
    return -1; }
static void bd_add(int kind,int unit,int ratio,u64 sec,const char* nm){ if(g_bd_n>=BD_MAX||!sec)return; blkdev* b=&g_bd[g_bd_n++]; b->kind=kind; b->unit=unit; b->ratio=ratio<1?1:ratio; b->sectors=sec; int i=0; for(;nm[i]&&i<23;i++)b->name[i]=nm[i]; b->name[i]=0; b->label[0]=0; }
static void ata_model(const u8* id,char* out){ int n=0; for(int i=27;i<47;i++){ out[n++]=(char)id[i*2+1]; out[n++]=(char)id[i*2]; } while(n>0&&out[n-1]==' ')n--; out[n]=0; }
int bd_scan(int usb){
    g_bd_n=0; gdisk_ensure();
    int mtype=g_dktype, ide_main=-1, ahci_main=-1;
    if(mtype!=DK_NONE&&g_dksectors){ const char* nm=mtype==DK_IDE?"ide (main)":mtype==DK_AHCI?"sata (main)":mtype==DK_NVME?"nvme0":"virtio0"; u32 r=dk_ratio(); bd_add(BK_MAIN,0,(int)r,(u64)g_dksectors*r,nm);
        if(mtype==DK_IDE)ide_main=g_ata_drv; if(mtype==DK_AHCI)ahci_main=ahci_cur_port(); }
    for(int u=0;u<4;u++){ if(u==ide_main)continue; u8 id[512]; if(pio_cmd(u,0,1,id))continue; const u16* w=(const u16*)id; u64 s48=w[100]|((u64)w[101]<<16)|((u64)w[102]<<32)|((u64)w[103]<<48); u64 sec=(w[83]&(1u<<10))&&s48?s48:(w[60]|((u32)w[61]<<16));
        char nm[24],*p=nm; p=scopy(p,"ide"); p=udec(p,(u32)u); *p=0; bd_add(BK_IDE,u,1,sec,nm); ata_model(id,g_bd[g_bd_n-1].label); }
    { u32 m=0; if(!ahci_ports_list(&m)&&mtype!=DK_AHCI){ ahci_init(); ahci_ports_list(&m); } for(int p=0;p<32;p++){ if(!(m&(1u<<p))||p==ahci_main)continue; u64 sec=0; if(ahci_port_io(p,0,0,&sec)||!sec)continue; char nm[24],*q=nm; q=scopy(q,"sata"); q=udec(q,(u32)p); *q=0; bd_add(BK_AHCI,p,1,sec,nm); } }
    if(usb&&msd_autoinit()){ u64 s=usb_msd_sectors(); if(s)bd_add(BK_USB,0,1,s,"usb0"); }
    return g_bd_n; }

static void cpyn(char* d,const char* s,int n){ int i=0; for(;s[i]&&i<n-1;i++)d[i]=s[i]; d[i]=0; }
static const char* mbr_tname(u8 t){ switch(t){ case 0x01: return "FAT12"; case 0x04: case 0x06: case 0x0E: return "FAT16"; case 0x0B: case 0x0C: return "FAT32"; case 0x07: return "NTFS/exFAT"; case 0x05: case 0x0F: case 0x85: return "Extended"; case 0x82: return "Linux swap"; case 0x83: return "Linux"; case 0x8E: return "Linux LVM"; case 0xFD: return "Linux RAID"; case 0xA5: return "FreeBSD"; case 0xA6: return "OpenBSD"; case 0xA9: return "NetBSD"; case 0xAF: return "HFS+"; case 0xEE: return "GPT protective"; case 0xEF: return "EFI system"; case 0x27: return "Windows recovery"; case 0x12: return "OEM diagnostics"; case 0x17: return "Hidden NTFS"; case 0x1B: case 0x1C: return "Hidden FAT32"; case 0x7F: return "NoovexOS data"; case 0x00: return "Empty"; default: return "Other"; } }
static void guid_str(const u8* g,char* o){ static const char* H="0123456789ABCDEF"; static const int ord[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15}; int n=0;
    for(int i=0;i<16;i++){ if(i==4||i==6||i==8||i==10)o[n++]='-'; u8 b=g[ord[i]]; o[n++]=H[b>>4]; o[n++]=H[b&15]; } o[n]=0; }
static const char* gpt_tname(const char* g){
    static const char* T[][2]={{"C12A7328-F81F-11D2-BA4B-00A0C93EC93B","EFI system"},{"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7","Basic data"},{"E3C9E316-0B5C-4DB8-817D-F92DF00215AE","MS reserved"},{"DE94BBA4-06D1-4D40-A16A-BFD50179D6AC","Windows recovery"},
        {"5808C8AA-7E8F-42E0-85D2-E1E90434CFB3","LDM metadata"},{"AF9B60A0-1431-4F62-BC68-3311714A69AD","LDM data"},{"0FC63DAF-8483-4772-8E79-3D69D8477DE4","Linux"},{"0657FD6D-A4AB-43C4-84E5-0933C84B4F4F","Linux swap"},{"E6D6D379-F507-44C2-A23C-238F2A3DF928","Linux LVM"},
        {"A19D880F-05FC-4D3B-A006-743F0F84911E","Linux RAID"},{"4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709","Linux root"},{"933AC7E1-2EB4-4F13-B844-0E14E2AEF915","Linux home"},{"BC13C2FF-59E6-4262-A352-B275FD6F7172","Linux /boot"},{"CA7D7CCB-63ED-4C53-861C-1742536059CC","LUKS"},
        {"21686148-6449-6E6F-744E-656564454649","BIOS boot"},{"7C3457EF-0000-11AA-AA11-00306543ECAC","Apple APFS"},{"48465300-0000-11AA-AA11-00306543ECAC","Apple HFS+"},{"516E7CB4-6ECF-11D6-8FF8-00022D09712B","FreeBSD"},{"83BD6B9D-7F41-11DC-BE0B-001560B84F0F","FreeBSD boot"},
        {"6A898CC3-1DD2-11B2-99A6-080020736631","ZFS"},{"3B8F8425-20E0-4F3B-907F-1A25A76F98E8","Linux /srv"},{"FE3A2A5D-4F32-41A7-B725-ACCC3285A309","ChromeOS kernel"},{"3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC","ChromeOS root"},{"37AFFC90-EF7D-4E96-91C3-2D7AE055B174","IBM GPFS"},
        {"2DB519C4-B10F-11DC-B99B-0019D1879648","FreeBSD UFS"},{"9E1A2D38-C612-4316-AA26-8B49521E5A8B","PReP boot"}};
    for(unsigned i=0;i<sizeof(T)/sizeof(T[0]);i++) if(seq(g,T[i][0]))return T[i][1]; return "Other"; }
static int sig_at(const u8* b,int off,const char* s){ for(int i=0;s[i];i++) if(b[off+i]!=(u8)s[i])return 0; return 1; }
static void fs_probe(int d,u64 lba,u64 cnt,char* out){
    u8 b[512]; out[0]=0; if(bd_read(d,lba,b))return;
    if(sig_at(b,3,"NTFS    ")){ cpyn(out,"NTFS",12); return; } if(sig_at(b,3,"EXFAT   ")){ cpyn(out,"exFAT",12); return; } if(sig_at(b,3,"-FVE-FS-")){ cpyn(out,"BitLocker",12); return; }
    if(sig_at(b,0,"LUKS\xBA\xBE")){ cpyn(out,"LUKS",12); return; } if(sig_at(b,0,"XFSB")){ cpyn(out,"XFS",12); return; }
    if(b[510]==0x55&&b[511]==0xAA){ if(sig_at(b,82,"FAT32")){ cpyn(out,"FAT32",12); return; } if(sig_at(b,54,"FAT16")){ cpyn(out,"FAT16",12); return; } if(sig_at(b,54,"FAT12")){ cpyn(out,"FAT12",12); return; } if(sig_at(b,54,"FAT")){ cpyn(out,"FAT",12); return; } }
    if(*(u32*)b==0x3458564Eu){ cpyn(out,"NVX4",12); return; }
    if(cnt>3&&!bd_read(d,lba+2,b)){ if(b[56]==0x53&&b[57]==0xEF){ u32 cf=*(u32*)(b+92), inc=*(u32*)(b+96); cpyn(out,(inc&0x40)||(inc&0x80)?"ext4":(cf&4)?"ext3":"ext2",12); return; }
        if(sig_at(b,0,"H+")||sig_at(b,0,"HX")){ cpyn(out,"HFS+",12); return; } }
    if(cnt>8&&!bd_read(d,lba+7,b)&&sig_at(b,502,"SWAPSPACE2")){ cpyn(out,"swap",12); return; }
    if(cnt>66&&!bd_read(d,lba+64,b)&&sig_at(b,1,"CD001")){ cpyn(out,"ISO9660",12); return; }
    if(cnt>130&&!bd_read(d,lba+128,b)&&sig_at(b,0x40,"_BHRfS_M")){ cpyn(out,"btrfs",12); return; }
    if(!bd_read(d,lba,b)&&sig_at(b,32,"NXSB")){ cpyn(out,"APFS",12); return; }
}
static int padd(partent* out,int cap,int* n,int d,int num,int logical,int scheme,u64 st,u64 cnt,u8 t,const char* ty){
    if(*n>=cap||!cnt)return 0; partent* p=&out[(*n)++]; p->disk=d; p->num=num; p->logical=logical; p->scheme=scheme; p->start=st; p->count=cnt; p->mbrtype=t; cpyn(p->type,ty,28); p->name[0]=0; p->fs[0]=0; return 1; }
int part_scan_disk(int d,partent* out,int cap){
    int n=0; if(d<0||d>=g_bd_n)return 0; u64 R=(u64)g_bd[d].ratio, total=g_bd[d].sectors; u8 m[512]; if(bd_read(d,0,m))return 0;
    int gpt=0, mbr=(m[510]==0x55&&m[511]==0xAA);
    if(mbr) for(int e=0;e<4;e++){ const u8* pe=m+446+e*16; if(pe[4]==0xEE)gpt=1; }
    if(!mbr){ u8 h[512]; if(!bd_read(d,R,h)&&sig_at(h,0,"EFI PART"))gpt=1; }
    if(gpt){ u8 h[512]; int ok=0; u64 hl=R; if(!bd_read(d,hl,h)&&sig_at(h,0,"EFI PART"))ok=1;
        if(!ok){ hl=total-R; if(!bd_read(d,hl,h)&&sig_at(h,0,"EFI PART"))ok=1; }
        if(ok){ u64 elba=*(u64*)(h+72); u32 ne=*(u32*)(h+80), esz=*(u32*)(h+84); if(ne>1024)ne=1024;
            if(esz>=128&&esz<=4096&&!(esz&7)){ u8 s[512]; u64 last=~0ULL; int empty_run=0;
                for(u32 i=0;i<ne&&n<cap;i++){ u64 byte=(u64)i*esz, l=elba*R+byte/512; int off=(int)(byte%512); if(off+128>512)continue;
                    if(l!=last){ if(bd_read(d,l,s))break; last=l; }
                    int z=1; for(int k=0;k<16;k++) if(s[off+k]){ z=0; break; } if(z){ if(++empty_run>128&&i>=128)break; continue; } empty_run=0;
                    u64 f=*(u64*)(s+off+32), lz=*(u64*)(s+off+40); if(lz<f)continue; char g[40]; guid_str(s+off,g);
                    if(padd(out,cap,&n,d,(int)i+1,0,2,f*R,(lz-f+1)*R,0,gpt_tname(g))){ char* nm=out[n-1].name; int q=0; for(int c=0;c<36&&q<39;c++){ u16 ch=*(u16*)(s+off+56+c*2); if(!ch)break; nm[q++]=(ch>=32&&ch<127)?(char)ch:'?'; } nm[q]=0; } } }
            for(int i=0;i<n;i++)fs_probe(d,out[i].start,out[i].count,out[i].fs);
            return n; } }
    if(!mbr){ char f[12]; fs_probe(d,0,total,f); if(f[0]){ padd(out,cap,&n,d,0,0,0,0,total,0,"Whole disk"); cpyn(out[n-1].fs,f,12); } return n; }
    { char f[12]; fs_probe(d,0,total,f); if(f[0]&&(seq(f,"FAT32")||seq(f,"FAT16")||seq(f,"FAT12")||seq(f,"NTFS")||seq(f,"exFAT"))){ int sane=0; for(int e=0;e<4;e++){ const u8* pe=m+446+e*16; if((pe[0]==0||pe[0]==0x80)&&pe[4]&&*(u32*)(pe+12))sane=1; } if(!sane){ padd(out,cap,&n,d,0,0,0,0,total,0,"Whole disk"); cpyn(out[n-1].fs,f,12); return n; } } }
    u64 ext0=0; int lnum=5;
    for(int e=0;e<4;e++){ const u8* pe=m+446+e*16; u8 t=pe[4]; u64 st=(u64)*(u32*)(pe+8)*R, sz=(u64)*(u32*)(pe+12)*R; if(!t||!sz)continue;
        padd(out,cap,&n,d,e+1,0,1,st,sz,t,mbr_tname(t)); if((t==0x05||t==0x0F||t==0x85)&&!ext0)ext0=st; }
    if(ext0){ u64 cur=ext0; for(int guard=0;guard<128&&n<cap;guard++){ u8 eb[512]; if(bd_read(d,cur,eb)||eb[510]!=0x55||eb[511]!=0xAA)break;
            const u8* a=eb+446; const u8* b=eb+462; if(a[4]&&*(u32*)(a+12)){ padd(out,cap,&n,d,lnum++,1,1,cur+(u64)*(u32*)(a+8)*R,(u64)*(u32*)(a+12)*R,a[4],mbr_tname(a[4])); }
            if(!(b[4]==0x05||b[4]==0x0F||b[4]==0x85)||!*(u32*)(b+8))break; u64 nx=ext0+(u64)*(u32*)(b+8)*R; if(nx<=cur)break; cur=nx; } }
    for(int i=0;i<n;i++) if(!(out[i].mbrtype==0x05||out[i].mbrtype==0x0F||out[i].mbrtype==0x85))fs_probe(d,out[i].start,out[i].count,out[i].fs);
    return n; }
int parts_scan_main(partent* out,int cap){ g_bd_n=0; gdisk_ensure(); if(g_dktype==DK_NONE||!g_dksectors)return 0; u32 r=dk_ratio(); bd_add(BK_MAIN,0,(int)r,(u64)g_dksectors*r,"main"); return part_scan_disk(0,out,cap); }
int parts_scan_all(int usb){ bd_scan(usb); g_pt_n=0; for(int d=0;d<g_bd_n;d++)g_pt_n+=part_scan_disk(d,g_pt+g_pt_n,PT_MAX-g_pt_n); return g_pt_n; }
static char* sz_fmt(char* p,u64 sectors){ u64 mb=sectors>>11; if(mb>=1024*1024){ p=udec(p,(u32)(mb>>20)); *p++='.'; *p++=(char)('0'+(int)((mb&0xFFFFF)*10>>20)); return scopy(p," TB"); } if(mb>=1024){ p=udec(p,(u32)(mb>>10)); *p++='.'; *p++=(char)('0'+(int)((mb&1023)*10>>10)); return scopy(p," GB"); } if(mb){ p=udec(p,(u32)mb); return scopy(p," MB"); } p=udec(p,(u32)(sectors/2)); return scopy(p," KB"); }
static char* u64dec(char* p,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
void parts_print(void){
    parts_scan_all(1);
    if(!g_bd_n){ tcol(rgb(240,150,120)); term_puts("no disks found\n"); tcol(WHT); return; }
    for(int d=0;d<g_bd_n;d++){ char b[120],*p=b; p=scopy(p,g_bd[d].name); p=scopy(p,"  "); p=sz_fmt(p,g_bd[d].sectors); if(g_bd[d].ratio>1)p=scopy(p,"  4K sectors"); if(g_bd[d].label[0]){ p=scopy(p,"  "); p=scopy(p,g_bd[d].label); } *p=0;
        tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); klog2("parts: ",b); int any=0;
        for(int i=0;i<g_pt_n;i++){ partent* e=&g_pt[i]; if(e->disk!=d)continue; any=1; p=b; p=scopy(p,e->logical?"    `- ":"  "); p=scopy(p,e->scheme==2?"gpt":e->scheme==1?"mbr":"raw"); if(e->num){ p=udec(p,(u32)e->num); } p=scopy(p,"  "); p=sz_fmt(p,e->count); p=scopy(p,"  @"); p=u64dec(p,e->start); p=scopy(p,"  "); p=scopy(p,e->type);
            if(e->fs[0]){ p=scopy(p,"  ["); p=scopy(p,e->fs); *p++=']'; } if(e->name[0]){ p=scopy(p,"  \""); int k=0; while(e->name[k]&&k<24)*p++=e->name[k++]; *p++='"'; } *p=0;
            tcol(e->type[0]=='E'&&e->type[1]=='x'?GREY:CYAN); term_puts(b); klog2("parts: ",b); term_putc('\n'); tcol(WHT); }
        if(!any){ tcol(GREY); term_puts("  (no partitions)\n"); tcol(WHT); } }
    { char b[64],*p=b; p=udec(p,(u32)g_bd_n); p=scopy(p," disk(s), "); p=udec(p,(u32)g_pt_n); p=scopy(p," partition(s)"); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT); }
}
