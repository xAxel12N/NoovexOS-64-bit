#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "net/bluetooth/bt_sec.h"
#define KDIR "/etc/bluetooth"
#define KFILE KDIR "/keys"
static btkey_t bk[BTK_MAX];
static int bk_loaded;
static int bk_eq(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
static void bk_zero(void* p,int n){ volatile u8* z=(volatile u8*)p; for(int i=0;i<n;i++)z[i]=0; }
static int bk_nib(char c){ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static void bk_mkdir(const char* parent,const char* name,unsigned mode){ dt_init(); int d=dt_resolve_dir(parent); if(d<0)return; int c=dt_child(d,name); if(c<0)c=dt_mkdir_at(d,name); if(c>=0){ DT[c].uid=0; DT[c].gid=0; DT[c].mode=mode; } }
void bt_keys_load(void){
    static char b[BTK_MAX*2*sizeof(btkey_t)+BTK_MAX*8+64];
    bk_loaded=1; bk_zero(bk,(int)sizeof bk);
    int su=g_uid; g_uid=0; int n=ux_read_file(KFILE,b,(int)sizeof b-1); g_uid=su;
    if(n<8){ bk_zero(b,(int)sizeof b); return; }
    b[n]=0; int nk=0, ok=1; const char* hd="NVBTK1\n"; for(int i=0;i<7;i++) if(b[i]!=hd[i])ok=0;
    for(int i=7;ok&&i<n&&nk<BTK_MAX;){ int e=i; while(e<n&&b[e]!='\n')e++;
        if(e-i==4+2*(int)sizeof(btkey_t)&&b[i]=='k'&&b[i+1]=='e'&&b[i+2]=='y'&&b[i+3]==' '){ u8* d=(u8*)&bk[nk]; int good=1;
            for(int k=0;k<(int)sizeof(btkey_t);k++){ int h=bk_nib(b[i+4+2*k]), l=bk_nib(b[i+5+2*k]); if(h<0||l<0){ good=0; break; } d[k]=(u8)(h*16+l); }
            if(good&&bk[nk].used==1&&bk[nk].le<2&&bk[nk].keysz>=7&&bk[nk].keysz<=16)nk++; else bk_zero(&bk[nk],(int)sizeof(btkey_t)); }
        i=e+1; }
    bk_zero(b,(int)sizeof b);
    if(nk){ char m[64],*p=m; p=scopy(p,"bluetooth: "); p=udec(p,(u32)nk); p=scopy(p," bonded device key(s) loaded"); *p=0; klog(m); }
}
static void bk_need(void){ if(!bk_loaded)bt_keys_load(); }
void bt_keys_save(void){
    static char b[BTK_MAX*2*sizeof(btkey_t)+BTK_MAX*8+64]; static const char H[]="0123456789abcdef";
    bk_need(); int su=g_uid; g_uid=0; bk_mkdir("/etc","bluetooth",0040700u);
    char* p=b; p=scopy(p,"NVBTK1\n");
    for(int i=0;i<BTK_MAX;i++){ if(!bk[i].used)continue; const u8* s=(const u8*)&bk[i]; p=scopy(p,"key "); for(int k=0;k<(int)sizeof(btkey_t);k++){ *p++=H[s[k]>>4]; *p++=H[s[k]&15]; } *p++='\n'; }
    ux_write_text(KFILE,b,(int)(p-b),0100600u); int isd=0; int k=ux_lookup(KFILE,&isd,0); if(k>=0&&!isd){ FS[k].uid=0; FS[k].gid=0; FS[k].mode=0100600u; }
    bk_zero(b,(int)sizeof b); dt_save(); g_uid=su;
}
btkey_t* bt_keys_find(const u8* addr,int le){ bk_need(); for(int i=0;i<BTK_MAX;i++) if(bk[i].used&&bk[i].le==(u8)(le?1:0)&&bk_eq(bk[i].addr,addr))return &bk[i]; return 0; }
int bt_rpa_match(const u8 irk[16],const u8* rpa){ if((rpa[5]&0xC0)!=0x40)return 0; u32 h=smp_ah(irk,rpa+3); return (u8)h==rpa[0]&&(u8)(h>>8)==rpa[1]&&(u8)(h>>16)==rpa[2]; }
btkey_t* bt_keys_find_le(const u8* addr,int atype){
    bk_need();
    for(int i=0;i<BTK_MAX;i++) if(bk[i].used&&bk[i].le&&bk[i].atype==(u8)atype&&bk_eq(bk[i].addr,addr))return &bk[i];
    if(atype==1&&(addr[5]&0xC0)==0x40) for(int i=0;i<BTK_MAX;i++) if(bk[i].used&&bk[i].le&&(bk[i].flags&BTKF_IRK)&&bt_rpa_match(bk[i].irk,addr))return &bk[i];
    return 0;
}
btkey_t* bt_keys_add(const u8* addr,int le,int atype){
    bk_need(); btkey_t* k=le?bt_keys_find_le(addr,atype):bt_keys_find(addr,0);
    if(!k) for(int i=0;i<BTK_MAX&&!k;i++) if(!bk[i].used)k=&bk[i];
    if(!k)k=&bk[0];
    bk_zero(k,(int)sizeof *k); k->used=1; k->le=(u8)(le?1:0); k->atype=(u8)atype; k->keysz=16; for(int i=0;i<6;i++)k->addr[i]=addr[i];
    return k;
}
int bt_keys_forget(const u8* addr,int le){
    bk_need(); int n=0;
    for(int i=0;i<BTK_MAX;i++){ btkey_t* k=&bk[i]; if(!k->used||k->le!=(u8)(le?1:0))continue;
        if(bk_eq(k->addr,addr)||(le&&(k->flags&BTKF_IRK)&&bt_rpa_match(k->irk,addr))){ bk_zero(k,(int)sizeof *k); n++; } }
    if(n){ bt_keys_save(); } return n;
}
int bt_keys_count(void){ bk_need(); int n=0; for(int i=0;i<BTK_MAX;i++) if(bk[i].used)n++; return n; }
btkey_t* bt_keys_at(int i){ bk_need(); for(int j=0;j<BTK_MAX;j++) if(bk[j].used&&!i--)return &bk[j]; return 0; }
