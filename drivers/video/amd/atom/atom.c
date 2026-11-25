#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "drivers/video/amd/atom/atom.h"

amd_atom_t g_amd_atom;

static const char* const atom_dt_names[]={"utilitypipeline","multimedia_info","smc_dpm_info","sw3","firmwareinfo","sw5","lcd_info","sw7","smu_info","sw9","sw10",
    "vram_usagebyfirmware","gpio_pin_lut","sw13","gfx_info","powerplayinfo","sw16","sw17","sw18","sw19","sw20","sw21","displayobjectinfo","indirectioaccess",
    "umc_info","sw25","sw26","dce_info","vram_info","sw29","integratedsysteminfo","asic_profiling_info","voltageobject_info","sw33","sw34"};
#define ATOM_NDT ((int)(sizeof(atom_dt_names)/sizeof(atom_dt_names[0])))

static const u8* atom_img; static u32 atom_len;

static u16 atom_r16(const u8* b,u32 len,u32 o){ return o+2<=len?(u16)(b[o]|(b[o+1]<<8)):0; }
static u32 atom_r32(const u8* b,u32 len,u32 o){ return o+4<=len?(u32)b[o]|((u32)b[o+1]<<8)|((u32)b[o+2]<<16)|((u32)b[o+3]<<24):0; }
static u8 atom_r8(const u8* b,u32 len,u32 o){ return o<len?b[o]:0; }

int amd_atom_check(const u8* b,u32 len){
    if(!b||len<0x4A||b[0]!=0x55||b[1]!=0xAA)return 0;
    u32 h=atom_r16(b,len,0x48); if(!h||h+8>len)return 0;
    return (b[h+4]=='A'&&b[h+5]=='T'&&b[h+6]=='O'&&b[h+7]=='M')||(b[h+4]=='M'&&b[h+5]=='O'&&b[h+6]=='T'&&b[h+7]=='A');
}

u32 amd_vfct_find(const u8* t,u32 tlen,u8 bus,u8 slot,u8 fn,u16 ven,u16 did,u32* olen){
    if(!t||tlen<0x4C||t[0]!='V'||t[1]!='F'||t[2]!='C'||t[3]!='T')return 0;
    u32 l=atom_r32(t,tlen,4); if(l<0x4C)return 0; if(l<tlen)tlen=l;
    u32 off=atom_r32(t,tlen,0x34);
    for(int g=0;g<64&&off<tlen;g++){
        u32 h=off; if(h+0x1C>tlen)return 0;
        u32 il=atom_r32(t,tlen,h+0x18); off=h+0x1C;
        if(il>tlen-off)return 0;
        off+=il;
        if(il&&atom_r32(t,tlen,h)==bus&&atom_r32(t,tlen,h+4)==slot&&atom_r32(t,tlen,h+8)==fn&&atom_r16(t,tlen,h+12)==ven&&atom_r16(t,tlen,h+14)==did){ if(olen)*olen=il; return h+0x1C; }
    }
    return 0;
}

static void atom_str(const u8* b,u32 len,u32 o,char* d,int max){
    int n=0; if(o<len&&b[o]==0)o++;
    while(n<max-1&&o+n<len&&b[o+n]>=' '&&b[o+n]<='z'){ d[n]=(char)b[o+n]; n++; }
    while(n>0&&d[n-1]==' ')n--;
    d[n]=0;
}

static u32 atom_find(const u8* b,u32 len,const char* s,u32 from,u32 to){
    int sl=0; while(s[sl])sl++;
    if(to>len)to=len;
    for(u32 i=from;i+(u32)sl<=to;i++){ int k=0; while(k<sl&&b[i+k]==(u8)s[k])k++; if(k==sl)return i; }
    return 0;
}

static u16 atom_hdr(const u8* b,u32 len,u16 off,u8* fr,u8* cr){
    if(!off||(u32)off+4>len){ *fr=*cr=0; return 0; }
    u16 sz=atom_r16(b,len,off); *fr=b[off+2]; *cr=b[off+3];
    if(sz<4||(u32)off+sz>len){ *fr=*cr=0; return 0; }
    return sz;
}

int amd_atom_parse(const u8* b,u32 len,amd_atom_t* a){
    u8* z=(u8*)a; for(u32 i=0;i<sizeof(*a);i++)z[i]=0;
    atom_img=0; atom_len=0;
    if(!amd_atom_check(b,len)){ scopy(a->why,"no ATOM signature"); return 0; }
    u32 isz=(u32)b[2]*512u; if(isz>=0x200&&isz<len)len=isz;
    u16 h=atom_r16(b,len,0x48); a->rom_hdr=h;
    u16 hs=atom_hdr(b,len,h,&a->rom_frev,&a->rom_crev);
    if(hs<34){ scopy(a->why,"short ROM header"); return 0; }
    a->ssvid=atom_r16(b,len,h+24u); a->ssdid=atom_r16(b,len,h+26u);
    a->cmd_tbl=atom_r16(b,len,h+30u); a->data_tbl=atom_r16(b,len,h+32u);
    u8 fr,cr; u16 ds=atom_hdr(b,len,a->data_tbl,&fr,&cr);
    if(ds<4+2*31){ scopy(a->why,"bad master data table"); return 0; }
    atom_img=b; atom_len=len;
    u32 dt=a->data_tbl+4u;
    a->fwi_off=atom_r16(b,len,dt+2*4); a->lcd_off=atom_r16(b,len,dt+2*6); a->smu_off=atom_r16(b,len,dt+2*8);
    a->obj_off=atom_r16(b,len,dt+2*22); a->isi_off=atom_r16(b,len,dt+2*30);
    u16 s=atom_hdr(b,len,a->fwi_off,&a->fwi_frev,&a->fwi_crev);
    if(a->fwi_frev==3&&s>=36){ u32 o=a->fwi_off;
        a->fw_rev=atom_r32(b,len,o+4); a->boot_sclk_10khz=atom_r32(b,len,o+8); a->boot_mclk_10khz=atom_r32(b,len,o+12); a->fw_cap=atom_r32(b,len,o+16);
        a->scratch_addr=atom_r32(b,len,o+24); a->vddc_mv=atom_r16(b,len,o+28); a->vddgfx_mv=atom_r16(b,len,o+34); }
    s=atom_hdr(b,len,a->isi_off,&a->isi_frev,&a->isi_crev);
    if(s>=54&&(a->isi_frev==1||a->isi_frev==2)){ u32 o=a->isi_off;
        a->isi_vbios_misc=atom_r32(b,len,o+4); a->isi_gpucap=atom_r32(b,len,o+8); a->isi_syscfg=atom_r32(b,len,o+12);
        if(a->isi_frev==1){ a->bl_pwm_hz=atom_r16(b,len,o+40); a->memtype=atom_r8(b,len,o+42); a->umach=atom_r8(b,len,o+43); a->min_bl=atom_r8(b,len,o+51); a->htc_hyst=atom_r8(b,len,o+52); a->htc_lim=atom_r8(b,len,o+53); }
        else { a->memtype=atom_r8(b,len,o+26); a->umach=atom_r8(b,len,o+27); a->htc_hyst=atom_r8(b,len,o+28); a->htc_lim=atom_r8(b,len,o+29);
            a->bl_pwm_hz=atom_r16(b,len,o+32); a->edp_bpc=atom_r8(b,len,o+47); a->edp_boot_bl=atom_r8(b,len,o+48); } }
    s=atom_hdr(b,len,a->obj_off,&a->obj_frev,&a->obj_crev);
    if(s>=8&&a->obj_frev==1&&(a->obj_crev==4||a->obj_crev==5)){ u32 o=a->obj_off; u32 np=atom_r8(b,len,o+6);
        for(u32 i=0;i<np&&a->ncon<AMD_ATOM_MAXCON;i++){ u32 p=o+8+i*16; if(p+16>(u32)o+s)break;
            amd_atom_con_t* c=&a->con[a->ncon]; c->objid=atom_r16(b,len,p); c->enc=atom_r16(b,len,p+4); c->extenc=a->obj_crev==4?atom_r16(b,len,p+6):0;
            c->devtag=atom_r16(b,len,p+12); c->prio=a->obj_crev==4?atom_r8(b,len,p+14):0;
            if(((c->objid>>12)&7)!=3)continue;
            c->type=(u8)(c->objid&0xFF); c->enumid=(u8)((c->objid>>8)&7); c->encid=(u8)(c->enc&0xFF); a->ncon++; } }
    if(atom_r8(b,len,0x2F))atom_str(b,len,atom_r16(b,len,0x6E),a->pn,sizeof(a->pn)); else atom_str(b,len,0x80,a->pn,sizeof(a->pn));
    if(!a->pn[0]){ u32 f=atom_find(b,len,"ATOMBIOS",3,1024); if(f)atom_str(b,len,f+8,a->pn,sizeof(a->pn)); }
    { u32 st=atom_r16(b,len,0x6E),en=h; if(en<=st){ st=3; en=1024; } u32 f=atom_find(b,len,"ATOMBIOSBK-AMD",st,en+64);
      if(f&&f+18<len)atom_str(b,len,f+18,a->ver,sizeof(a->ver)); }
    atom_str(b,len,0x50,a->date,sizeof(a->date));
    a->ok=1; return 1;
}

int amd_atom_table(const char* name,u32* off,u8* frev,u8* crev,u16* size){
    if(!atom_img||!g_amd_atom.ok)return 0;
    for(int i=0;i<ATOM_NDT;i++) if(seq(name,atom_dt_names[i])){
        u16 o=atom_r16(atom_img,atom_len,g_amd_atom.data_tbl+4u+2u*(u32)i); u8 f,c; u16 s=atom_hdr(atom_img,atom_len,o,&f,&c); if(!s)return 0;
        if(off)*off=o;
        if(frev)*frev=f;
        if(crev)*crev=c;
        if(size)*size=s;
        return 1; }
    return 0;
}

const char* amd_atom_con_name(u8 t){
    switch(t){ case 0x01: case 0x02: return "DVI-I"; case 0x03: case 0x04: return "DVI-D"; case 0x05: return "VGA"; case 0x0C: return "HDMI-A"; case 0x0D: return "HDMI-B";
    case 0x0E: return "LVDS"; case 0x13: return "DP"; case 0x14: return "eDP"; case 0x17: return "USB-C"; default: return "other"; }
}

const char* amd_atom_mem_name(u8 t){
    static const char* const n[]={"DDR3","FBD2","DDR4","LPDDR","LPDDR2","LPDDR3","LPDDR4","GDDR6","HBM","HBM2","DDR5","LPDDR5","LPDDR5X"};
    return t>=0x18&&t<=0x24?n[t-0x18]:"?";
}

static char* atom_rev(char* p,u8 f,u8 c){ p=udec(p,f); *p++='.'; return udec(p,c); }

int amd_atom_describe(const amd_atom_t* a,char* buf,int n){
    char t[1400]; char* p=t;
    if(!a->ok){ p=scopy(p,"atom: "); p=scopy(p,a->why[0]?a->why:"not parsed"); *p++='\n'; }
    else {
        p=scopy(p,"vbios: "); p=scopy(p,a->pn[0]?a->pn:"?"); p=scopy(p," ver "); p=scopy(p,a->ver[0]?a->ver:"?"); p=scopy(p," date "); p=scopy(p,a->date); *p++='\n';
        p=scopy(p,"  rom hdr v"); p=atom_rev(p,a->rom_frev,a->rom_crev); p=scopy(p," @0x"); p=hx(p,a->rom_hdr,4); p=scopy(p," cmd @0x"); p=hx(p,a->cmd_tbl,4);
        p=scopy(p," data @0x"); p=hx(p,a->data_tbl,4); p=scopy(p," subsys "); p=hx(p,a->ssvid,4); *p++=':'; p=hx(p,a->ssdid,4); *p++='\n';
        if(a->fwi_frev){ p=scopy(p,"  firmware_info v"); p=atom_rev(p,a->fwi_frev,a->fwi_crev); p=scopy(p," rev 0x"); p=hx(p,a->fw_rev,8);
            p=scopy(p," sclk "); p=udec(p,a->boot_sclk_10khz/100); p=scopy(p," MHz mclk "); p=udec(p,a->boot_mclk_10khz/100);
            p=scopy(p," MHz cap 0x"); p=hx(p,a->fw_cap,8); p=scopy(p,a->fw_cap&1?" posted":" not-posted"); *p++='\n'; }
        if(a->isi_frev){ p=scopy(p,"  integrated_info v"); p=atom_rev(p,a->isi_frev,a->isi_crev); p=scopy(p," mem "); p=scopy(p,amd_atom_mem_name(a->memtype));
            p=scopy(p," x"); p=udec(p,a->umach); p=scopy(p," bl_pwm "); p=udec(p,a->bl_pwm_hz); p=scopy(p," Hz htc "); p=udec(p,a->htc_lim); p=scopy(p," C");
            if(a->isi_frev==2){ p=scopy(p," edp_bpc "); p=udec(p,a->edp_bpc); p=scopy(p," boot_bl "); p=udec(p,a->edp_boot_bl); }
            else { p=scopy(p," min_bl "); p=udec(p,a->min_bl); }
            *p++='\n'; }
        if(a->obj_frev){ p=scopy(p,"  display objects v"); p=atom_rev(p,a->obj_frev,a->obj_crev); p=scopy(p,", "); p=udec(p,(u32)a->ncon); p=scopy(p," connectors\n");
            for(int i=0;i<a->ncon;i++){ const amd_atom_con_t* c=&a->con[i]; p=scopy(p,"   "); p=scopy(p,amd_atom_con_name(c->type)); *p++='-'; p=udec(p,c->enumid);
                p=scopy(p," obj 0x"); p=hx(p,c->objid,4); p=scopy(p," enc 0x"); p=hx(p,c->enc,4); p=scopy(p," tag 0x"); p=hx(p,c->devtag,4); *p++='\n'; } }
    }
    int l=(int)(p-t); if(l>n-1)l=n-1; if(l<0)return 0;
    for(int i=0;i<l;i++)buf[i]=t[i];
    buf[l]=0; return l;
}
