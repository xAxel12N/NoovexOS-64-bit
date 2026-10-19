#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"

char g_nv_log[65536]; int g_nv_log_n=0; int g_nv_step=0; char g_nv_why[160]="";
static char g_nl_line[200]; static int g_nl_n=0;
static u8* g_rom=0; static u32 g_rom_len=0, g_img0=0, g_imgd=0;

void nvm32(u32 off,u32 mask,u32 v){ nvw32(off,(nvr32(off)&~mask)|v); }
int nv_poll(u32 off,u32 mask,u32 val,u32 ms){ u64 t0=hr_ns(); for(;;){ if((nvr32(off)&mask)==val)return 1; if(hr_ns()-t0>(u64)ms*1000000ULL)return 0; cpu_pause(); } }

static void nl_put(char c){ if(g_nl_n<(int)sizeof(g_nl_line)-1)g_nl_line[g_nl_n++]=c; }
void nvl_s(const char* s){ while(*s)nl_put(*s++); }
void nvl_x(u64 v){ char b[24],*p=b; p=scopy(p,"0x"); if(v>>32)p=hx(p,(u32)(v>>32),8),p=hx(p,(u32)v,8); else p=hx(p,(u32)v,8); *p=0; nvl_s(b); }
void nvl_d(u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)nl_put(t[--n]); }
void nvl_nl(void){ g_nl_line[g_nl_n]=0; if(g_nl_n)klog(g_nl_line); for(int i=0;i<g_nl_n&&g_nv_log_n<(int)sizeof(g_nv_log)-2;i++)g_nv_log[g_nv_log_n++]=g_nl_line[i]; if(g_nv_log_n<(int)sizeof(g_nv_log)-1)g_nv_log[g_nv_log_n++]='\n'; g_nv_log[g_nv_log_n]=0; g_nl_n=0; }
void nvl_kv(const char* k,u64 v){ nvl_s("  "); nvl_s(k); nvl_s(" = "); nvl_x(v); nvl_nl(); }
void nvl_reset(void){ g_nv_log_n=0; g_nv_log[0]=0; g_nl_n=0; g_nv_step=0; g_nv_why[0]=0; }
int nv_fail(int step,const char* why){ g_nv_step=step; char* p=g_nv_why; p=scopy(p,why); *p=0; nvl_s("FAILED at step "); nvl_d((u64)step); nvl_s(": "); nvl_s(why); nvl_nl(); return -step; }

int nv_gfw_wait(void){
    u64 t0=hr_ns();
    for(;;){ if((nvr32(0x118128)&1u)&&(nvr32(0x118234)&0xFFu)==0xFFu)return 1; if(hr_ns()-t0>2100000000ULL)break; hr_delay_us(1000); }
    nvl_kv("0x118128 (GFW priv mask)",nvr32(0x118128)); nvl_kv("0x118234 (GFW boot progress)",nvr32(0x118234)); return 0; }

int nvf_reset(u32 f){
    nvm32(f+0x048,3u,0u); nvw32(f+0x014,0xFFFFFFFFu);
    for(int k=0;k<2;k++){ nvm32(f+0x3C0,1u,1u); hr_delay_us(10); nvm32(f+0x3C0,1u,0u); nvm32(f+0x040,0,0); if(!nv_poll(f+0x10C,6u,0u,10))return 0; }
    nvw32(f+0x084,nvr32(0)); return 1; }
void nvf_imem(u32 f,u32 dst,const u8* src,u32 len,u32 tag,int secure){
    nvw32(f+0x180,(secure?(1u<<28):0u)|(1u<<24)|dst);
    for(u32 c=0;c<len;c+=256){ nvw32(f+0x188,tag++); for(u32 i=0;i<256;i+=4){ u32 w=0; for(int b=0;b<4;b++){ u32 o=c+i+(u32)b; if(o<len)w|=(u32)src[o]<<(8*b); } nvw32(f+0x184,w); } } }
void nvf_dmem(u32 f,u32 dst,const u8* src,u32 len){
    nvw32(f+0x1C0,(1u<<24)|dst);
    for(u32 i=0;i<len;i+=4){ u32 w=0; for(int b=0;b<4;b++){ u32 o=i+(u32)b; if(o<len)w|=(u32)src[o]<<(8*b); } nvw32(f+0x1C4,w); } }
int nvf_boot(u32 f,u32 bootvec,u32 mb0,u32 mb1,u32* o0,u32* o1,u32 ms){
    nvw32(f+0x040,mb0); nvw32(f+0x044,mb1); nvw32(f+0x104,bootvec); nvw32(f+0x100,2u);
    int ok=nv_poll(f+0x100,0x10u,0x10u,ms); if(o0)*o0=nvr32(f+0x040); if(o1)*o1=nvr32(f+0x044); return ok; }
void nvf_dump(u32 f,const char* name){
    nvl_s("  "); nvl_s(name); nvl_s(": mbox0 "); nvl_x(nvr32(f+0x040)); nvl_s(" mbox1 "); nvl_x(nvr32(f+0x044)); nvl_s(" cpuctl "); nvl_x(nvr32(f+0x100)); nvl_s(" bootvec "); nvl_x(nvr32(f+0x104)); nvl_nl();
    nvl_s("    hwcfg "); nvl_x(nvr32(f+0x108)); nvl_s(" dmactl "); nvl_x(nvr32(f+0x10C)); nvl_s(" hwcfg1 "); nvl_x(nvr32(f+0x12C)); nvl_s(" irqstat "); nvl_x(nvr32(f+0x008)); nvl_s(" os "); nvl_x(nvr32(f+0x080)); nvl_s(" sctl "); nvl_x(nvr32(f+0x240)); nvl_nl();
    u32 ti=nvr32(f+0x148); u32 n=(ti>>16)&0xFFu; if(n>8)n=8; if(n){ nvl_s("    tracepc"); for(u32 s=0;s<n;s++){ nvw32(f+0x148,s); nvl_s(" "); nvl_x(nvr32(f+0x14C)); } nvl_nl(); } }

static u32 rom_fix(u32 a){ if(g_imgd&&a>=g_img0)a=a-g_img0+g_imgd; return a; }
static u8 rb8(u32 a){ a=rom_fix(a); return a<g_rom_len?g_rom[a]:0; }
static u16 rb16(u32 a){ return (u16)(rb8(a)|(rb8(a+1)<<8)); }
static u32 rb32(u32 a){ return (u32)rb16(a)|((u32)rb16(a+2)<<16); }
static u8 raw8(u32 a){ return a<g_rom_len?g_rom[a]:0; }
static u16 raw16(u32 a){ return (u16)(raw8(a)|(raw8(a+1)<<8)); }
static u32 raw32(u32 a){ return (u32)raw16(a)|((u32)raw16(a+2)<<16); }

int nvb_read_rom(void){
    if(g_rom&&g_rom_len)return 1;
    if(!g_rom){ g_rom=(u8*)kmalloc(0x100000); if(!g_rom)return 0; }
    u32 sh=nvr32(0x088050); nvw32(0x088050,sh&~1u);
    u32 base=0, f0=nvr32(0x300000);
    if(f0==0x4947564Eu){ u32 f1=nvr32(0x300004); u32 ver=(f1>>8)&0xFFu;
        if(ver==1||ver==2){ u32 ds=(f1>>16)&0x7FFFu; base=nvr32(0x300000+ds+4); }
        else if(ver==3){ u32 ds=nvr32(0x300008)&0xFFFFFu; u32 dir=nvr32(0x300000+ds)+4096u; if(nvr32(0x300000+dir)==0x44524652u)base=nvr32(0x300000+dir+8); }
        nvl_kv("ROM has an IFR header, PCI ROM at",base); }
    if(base>=0x100000u||(nvr32(0x300000+base)&0xFFFFu)!=0xAA55u){ nvw32(0x088050,sh); nvl_kv("no 0xAA55 at PROM offset",base); return 0; }
    u32 len=0x100000u-base; for(u32 i=0;i<len;i+=4){ u32 w=nvr32(0x300000+base+i); g_rom[i]=(u8)w; g_rom[i+1]=(u8)(w>>8); g_rom[i+2]=(u8)(w>>16); g_rom[i+3]=(u8)(w>>24); }
    nvw32(0x088050,sh);
    g_rom_len=len; g_img0=0; g_imgd=0;
    u32 b=0; for(int n=0;n<16&&b+0x20<len;n++){
        u16 sig=raw16(b); if(sig!=0xAA55u&&sig!=0xBB77u&&sig!=0x4E56u)break;
        u32 pc=b+raw16(b+0x18); u32 ps=raw32(pc); if(ps!=0x52494350u&&ps!=0x53494752u&&ps!=0x5344504Eu)break;
        u32 hl=raw16(pc+0x0A), sz=(u32)raw16(pc+0x10)*512u; u8 ty=raw8(pc+0x14); int last=(raw8(pc+0x15)&0x80)!=0;
        if(ty!=0x70){ u32 np=(pc+hl+15)&~15u; if(raw32(np)==0x4544504Eu){ sz=(u32)raw16(np+8)*512u; last=(raw8(np+0x0A)&0x80)!=0; } } else last=1;
        nvl_s("  ROM image "); nvl_d((u64)n); nvl_s(" at "); nvl_x(b); nvl_s(" type "); nvl_x(ty); nvl_s(" size "); nvl_x(sz); nvl_nl();
        if(n==0)g_img0=sz; if(ty==0xE0&&!g_imgd)g_imgd=b;
        if(last||!sz)break; b+=sz; }
    for(int i=0;i<65536&&i<(int)len;i++)g_nv_vbios[i]=g_rom[i]; g_nv_vbios_len=len<65536?(int)len:65536;
    nvl_kv("ROM bytes read",len); nvl_kv("image 0 size",g_img0); nvl_kv("extended image (type 0xE0) at",g_imgd);
    return 1; }

int nvb_find_fwsec(nvfwsec* fw){
    u32 bit=0; for(u32 i=0;i+6<g_img0&&i+6<g_rom_len;i++) if(g_rom[i]==0xFF&&g_rom[i+1]==0xB8&&g_rom[i+2]=='B'&&g_rom[i+3]=='I'&&g_rom[i+4]=='T'){ bit=i; break; }
    if(!bit){ nvl_s("  no BIT table in the VBIOS"); nvl_nl(); return 0; }
    u32 esz=raw8(bit+9), ecnt=raw8(bit+10); u32 pe=0; u16 plen=0, poff=0;
    for(u32 e=0;e<ecnt;e++){ u32 o=bit+12+e*esz; if(raw8(o)=='p'){ pe=o; plen=raw16(o+2); poff=raw16(o+4); nvl_s("  BIT 'p' version "); nvl_d(raw8(o+1)); nvl_s(" length "); nvl_d(plen); nvl_s(" offset "); nvl_x(poff); nvl_nl(); break; } }
    if(!pe||raw8(pe+1)!=2||plen<4){ nvl_s("  BIT 'p' (falcon data) entry missing or not version 2"); nvl_nl(); return 0; }
    u32 tbl=rb32(poff); u8 tv=rb8(tbl), th=rb8(tbl+1), tl=rb8(tbl+2), tc=rb8(tbl+3);
    nvl_s("  falcon ucode table at "); nvl_x(tbl); nvl_s(" ver "); nvl_d(tv); nvl_s(" hdr "); nvl_d(th); nvl_s(" len "); nvl_d(tl); nvl_s(" count "); nvl_d(tc); nvl_nl();
    u32 desc=0; for(u32 i=0;i<tc;i++){ u32 o=tbl+th+i*tl; u8 app=rb8(o); u32 dp=rb32(o+2); nvl_s("    app "); nvl_x(app); nvl_s(" target "); nvl_x(rb8(o+1)); nvl_s(" desc "); nvl_x(dp); nvl_nl(); if(app==0x85&&!desc)desc=dp; }
    if(!desc){ nvl_s("  FWSEC_PROD (app 0x85) not found"); nvl_nl(); return 0; }
    u32 d=rom_fix(desc); u32 hdr=raw32(d); u32 dsz=(hdr>>16)&0xFFFFu, dver=(hdr>>8)&0xFFu;
    nvl_kv("FWSEC descriptor header",hdr);
    if(!(hdr&1u)||dver!=2){ nvl_s("  descriptor is not version 2 (Turing)"); nvl_nl(); return 0; }
    fw->if_off=raw32(d+0x10); fw->imem_base=raw32(d+0x14); u32 imem_load=raw32(d+0x18); fw->imem_sec_base=raw32(d+0x20); fw->imem_sec_size=raw32(d+0x24);
    fw->dmem_off=raw32(d+0x28); fw->dmem_base=raw32(d+0x2C); fw->dmem_size=raw32(d+0x30);
    fw->imem_size=imem_load; fw->total=imem_load+fw->dmem_size;
    nvl_kv("IMEM load size",imem_load); nvl_kv("IMEM secure base",fw->imem_sec_base); nvl_kv("IMEM secure size",fw->imem_sec_size); nvl_kv("DMEM offset",fw->dmem_off); nvl_kv("DMEM size",fw->dmem_size); nvl_kv("interface offset",fw->if_off);
    if(fw->total>0x80000u||fw->dmem_off+fw->dmem_size>fw->total+0x1000u){ nvl_s("  descriptor sizes look wrong"); nvl_nl(); return 0; }
    u32 src=d+dsz; if(src+fw->total>g_rom_len){ nvl_s("  ucode runs past the end of the ROM"); nvl_nl(); return 0; }
    u8* raw=(u8*)kmalloc(fw->total+0x2000u); if(!raw)return 0; u8* buf=(u8*)(((u64)raw+0xFFFULL)&~0xFFFULL);
    for(u32 i=0;i<fw->total;i++)buf[i]=g_rom[src+i]; for(u32 i=fw->total;i<fw->total+0x100u;i++)buf[i]=0;
    fw->buf=buf; return 1; }

static int fwsec_patch(nvfwsec* fw,u32 cmd){
    u8* dm=fw->buf+fw->dmem_off; u8* h=dm+fw->if_off;
    if(h[0]!=1){ nvl_kv("application interface version",h[0]); return 0; }
    u32 hl=h[1], el=h[2], cnt=h[3]; u32 mapper=0xFFFFFFFFu;
    for(u32 i=0;i<cnt;i++){ u8* e=h+hl+i*el; u32 id=*(u32*)e; if(id==4){ mapper=*(u32*)(e+4); break; } }
    if(mapper==0xFFFFFFFFu||mapper+0x40>fw->dmem_size){ nvl_s("  DMEMMAPPER entry not found"); nvl_nl(); return 0; }
    u8* m=dm+mapper; u32 cin=*(u32*)(m+0x08);
    nvl_kv("DMEMMAPPER signature",*(u32*)m); nvl_kv("DMEMMAPPER cmd_in offset",cin); nvl_kv("DMEMMAPPER init_cmd before",*(u32*)(m+0x2C));
    *(u32*)(m+0x2C)=cmd;
    if(cin+44>fw->dmem_size){ nvl_s("  FRTS command buffer outside DMEM"); nvl_nl(); return 0; }
    u8* c=dm+cin; for(int i=0;i<44;i++)c[i]=0;
    *(u32*)(c+0x00)=1; *(u32*)(c+0x04)=24; *(u32*)(c+0x14)=2;
    if(cmd==0x15){ *(u32*)(c+0x18)=1; *(u32*)(c+0x1C)=20; *(u32*)(c+0x20)=(u32)(g_nvl.frts_addr>>12); *(u32*)(c+0x24)=(u32)(g_nvl.frts_size>>12); *(u32*)(c+0x28)=2; }
    return 1; }

int nv_run_fwsec(u8* bl,u32 bl_len,u32 cmd){
    nvfwsec fw; if(!nvb_find_fwsec(&fw))return nv_fail(4,"FWSEC not found in the VBIOS");
    if(!fwsec_patch(&fw,cmd))return nv_fail(4,"FWSEC could not be patched");
    if(bl_len<0x400||*(u32*)bl!=0x10DEu)return nv_fail(4,"acr/bl.bin is damaged");
    u32 bdo=*(u32*)(bl+0x10), bho=*(u32*)(bl+0x0C); u32 start_tag=*(u32*)(bl+bho+0), code_off=*(u32*)(bl+bho+8), code_size=*(u32*)(bl+bho+12);
    u32 F=NVF_GSP;
    if(cmd==0x15&&(nvr32(0x1FA828)>>4)){ nvl_kv("WPR2 already set (hi)",nvr32(0x1FA828)); return nv_fail(4,"WPR2 is already set up from an earlier boot - power the PC off fully and try again"); }
    if(!nvf_reset(F)){ nvf_dump(F,"GSP falcon"); return nv_fail(4,"GSP falcon memory scrubbing did not finish"); }
    u32 limit=(nvr32(F+0x108)&0x1FFu)<<8; nvl_kv("GSP falcon IMEM size",limit);
    nvm32(F+0x624,0x80u,0x80u); nvw32(F+0x10C,0);
    nvf_imem(F,limit-code_size,bl+bdo+code_off,code_size,start_tag,0);
    nvm32(F+0x610,7u,5u);
    u8 d[84]; for(int i=0;i<84;i++)d[i]=0; u64 pa=(u64)fw.buf;
    *(u32*)(d+0x20)=4; *(u64*)(d+0x24)=pa; *(u32*)(d+0x2C)=fw.imem_base; *(u32*)(d+0x30)=fw.imem_size-fw.imem_sec_size; *(u32*)(d+0x34)=fw.imem_sec_base; *(u32*)(d+0x38)=fw.imem_sec_size;
    *(u64*)(d+0x40)=pa+fw.dmem_off; *(u32*)(d+0x48)=fw.dmem_size;
    nvf_dmem(F,0,d,84);
    for(u64 a=pa&~63ULL;a<pa+fw.total;a+=64)cpu_clflush(a); cpu_mb();
    u32 m0=0,m1=0; int ok=nvf_boot(F,start_tag<<8,0,0,&m0,&m1,2000);
    u32 err=nvr32(0x001438)>>16, lo=nvr32(0x1FA824), hi=nvr32(0x1FA828);
    nvl_kv("FWSEC mbox0",m0); nvl_kv("FRTS error (0x1438>>16)",err); nvl_kv("WPR2 lo",lo); nvl_kv("WPR2 hi",hi);
    if(!ok){ nvf_dump(F,"GSP falcon"); return nv_fail(4,"FWSEC did not halt within 2 s"); }
    if(cmd!=0x15){ u32 sb=nvr32(0x001454)&0xFFFFu; nvl_kv("FWSEC-SB error (0x1454)",sb); if(m0||sb){ nvf_dump(F,"GSP falcon"); return nv_fail(4,"FWSEC-SB reported an error"); } return 1; }
    if(m0||err){ nvf_dump(F,"GSP falcon"); return nv_fail(4,"FWSEC reported an error"); }
    if(!(hi>>4)||(lo>>4)!=(u32)(g_nvl.frts_addr>>12))return nv_fail(4,"WPR2 was not set where the FRTS region should be");
    return 1; }

int nv_booter(const u8* f,u32 len,u64 wpr_meta_pa,int unload){
    if(len<0x100||*(u32*)f!=0x10DEu)return nv_fail(5,"booter file damaged");
    u32 ho=*(u32*)(f+0x0C), dofs=*(u32*)(f+0x10), dsz=*(u32*)(f+0x14);
    u32 sig_off=*(u32*)(f+ho+0x00), sig_size=*(u32*)(f+ho+0x04), loc=*(u32*)(f+*(u32*)(f+ho+0x08)), sig=*(u32*)(f+*(u32*)(f+ho+0x0C)), cnt=*(u32*)(f+*(u32*)(f+ho+0x18)), lho=*(u32*)(f+ho+0x1C);
    const u8* lh=f+lho; u32 oco=*(u32*)(lh+0), ocs=*(u32*)(lh+4), odo=*(u32*)(lh+8), ods=*(u32*)(lh+12), napp=*(u32*)(lh+16), a0o=*(u32*)(lh+20), a0s=*(u32*)(lh+24);
    if(!cnt||!napp||dofs+dsz>len||loc+sig_size/cnt>dsz)return nv_fail(5,"booter headers out of range");
    u8* img=(u8*)kmalloc(dsz+16); if(!img)return nv_fail(5,"no memory for the booter image");
    for(u32 i=0;i<dsz;i++)img[i]=f[dofs+i];
    u32 ss=sig_size/cnt; u32 idx=(nvr32(NVF_SEC2+0x408)&0x00100000u)?1u:0u; if(idx>=cnt)idx=0;
    for(u32 i=0;i<ss;i++)img[loc+i]=f[sig_off+sig+idx*ss+i];
    u32 S=NVF_SEC2; u32 hw=nvr32(S+0x108); u32 il=(hw&0x1FFu)<<8, dl=(hw&0x3FE00u)>>1;
    nvl_kv(unload?"booter_unload signature index":"booter_load signature index",idx); nvl_kv("SEC2 IMEM bytes",il); nvl_kv("SEC2 DMEM bytes",dl);
    if(a0o+a0s>il||ods>dl){ kfree(img); return nv_fail(5,"SEC2 is too small for this booter"); }
    if(!nvf_reset(S)){ nvf_dump(S,"SEC2"); kfree(img); return nv_fail(5,"SEC2 memory scrubbing did not finish"); }
    nvm32(S+0x624,0x80u,0x80u); nvw32(S+0x10C,0);
    nvf_imem(S,oco,img+0,ocs,oco>>8,0);
    nvf_imem(S,a0o,img+ocs,a0s,a0o>>8,1);
    nvf_dmem(S,0,img+odo,ods);
    kfree(img);
    u32 m0=0,m1=0; int ok=unload?nvf_boot(S,oco,0xFFu,0xFFu,&m0,&m1,2000):nvf_boot(S,oco,(u32)wpr_meta_pa,(u32)(wpr_meta_pa>>32),&m0,&m1,2000);
    nvl_kv("SEC2 mbox0 after booter",m0); nvl_kv("SEC2 mbox1 after booter",m1);
    if(!ok){ nvf_dump(S,"SEC2"); return nv_fail(5,"booter did not halt within 2 s"); }
    if(m0){ nvf_dump(S,"SEC2"); return nv_fail(5,"booter returned an error code in mailbox 0"); }
    return 1; }
