#include "include/nvx/base.h"
#include "include/nvx/core/secboot.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "lib/crypto/nvxsigv.c"

int g_lockdown;
u32 g_sb_flags, g_sb_minsvn, g_sb_loader;
static int g_sb_uefi, g_sb_info;
static u8 g_sb_keyid[8];

void secboot_init(void){
    g_sb_uefi=*(volatile u32*)0x502CULL==0x49464555u;
    const volatile u8* p=(const volatile u8*)0x5100ULL;
    if(g_sb_uefi&&*(const volatile u32*)p==0x4253564Eu){
        g_sb_info=1; g_sb_flags=*(const volatile u32*)(p+4); g_sb_minsvn=*(const volatile u32*)(p+8); g_sb_loader=*(const volatile u32*)(p+20);
        for(int i=0;i<8;i++)g_sb_keyid[i]=p[24+i]; }
    g_lockdown=g_sb_info&&(g_sb_flags&SB_KERNEL_OK)&&!(g_sb_flags&SB_OVERRIDE);
    klog(g_lockdown?"secboot: signed boot verified by the loader - lockdown on":g_sb_info?"secboot: UNTRUSTED kernel started by the user - lockdown off":g_sb_uefi?"secboot: old UEFI loader without signature checks - lockdown off":"secboot: legacy BIOS start, no signature check before the kernel - lockdown off");
}
int secboot_signed(void){ return g_buildsig.sb_version==1; }
static void sb_pub(u8* pub){ for(int i=0;i<65;i++)pub[i]=g_buildsig.pub[i]; }
int secboot_check_kernel(const u8* k,u32 n,u32* svn){
    if(svn)*svn=0;
    if(!secboot_signed())return 0;
    u8 pub[65]; sb_pub(pub); u64 off=0;
    int r=sv_kernel_verify(k,n,pub,&off);
    if(r)return r;
    u32 v=sv_rd32(k+off+SV_O_SVN); if(svn)*svn=v;
    if(v<g_buildsig.svn||v<g_sb_minsvn)return -9;
    return 0;
}
int secboot_check_sysroot(const u8* b,u32 n){
    if(!secboot_signed())return 0;
    if(g_buildsig.sr_len!=n)return -8;
    sv_sha c; sv_sha_init(&c);
    for(u32 a=0;a<n;){ u32 q=n-a; if(q>4194304u)q=4194304u; sv_sha_update(&c,b+a,q); a+=q; }
    u8 d[32]; sv_sha_final(&c,d); u8 bad=0; for(int i=0;i<32;i++)bad|=d[i]^g_buildsig.sr_sha256[i];
    return bad?-8:0;
}
int secboot_check_update(const u8* k,u32 n,const u8* sr,u32 srn,u32* svn){
    int r=secboot_check_kernel(k,n,svn); if(r||!secboot_signed())return r;
    u64 off=sv_find_block(k,n); u32 want=sv_rd32(k+off+SV_O_SRLEN);
    if(!sr||!srn)return 0;
    if(want!=srn)return -8;
    sv_sha c; sv_sha_init(&c); for(u32 a=0;a<srn;){ u32 q=srn-a; if(q>4194304u)q=4194304u; sv_sha_update(&c,sr+a,q); a+=q; }
    u8 d[32]; sv_sha_final(&c,d); u8 bad=0; for(int i=0;i<32;i++)bad|=d[i]^k[off+SV_O_SRHASH+i];
    return bad?-8:0;
}
const char* secboot_why(int r){ return sv_why(r); }
int secboot_locked(const char* what){
    if(!g_lockdown)return 0;
    char b[120],*p=b; p=scopy(p,"secboot: lockdown refused: "); p=scopy(p,what); *p=0; klog(b);
    return 1;
}
static char* sb_hex(char* p,const volatile u8* b,int n){ for(int i=0;i<n;i++){ *p++="0123456789abcdef"[b[i]>>4]; *p++="0123456789abcdef"[b[i]&15]; } *p=0; return p; }
void secboot_report(void (*out)(const char*)){
    char b[160],*p;
    p=b; p=scopy(p,"  Boot path          "); p=scopy(p,g_sb_uefi?"UEFI":"legacy BIOS"); p=scopy(p,g_sb_info?" (NoovexOS loader ":""); if(g_sb_info){ p=udec(p,g_sb_loader); p=scopy(p,")"); } p=scopy(p,"\n"); *p=0; out(b);
    p=b; p=scopy(p,"  UEFI Secure Boot   "); p=scopy(p,!g_sb_info?"unknown":(g_sb_flags&SB_FW_SECURE)?"on":"off"); p=scopy(p,"\n"); *p=0; out(b);
    p=b; p=scopy(p,"  Kernel signature   "); p=scopy(p,!secboot_signed()?"this build is not signed":!g_sb_info?"not checked before start":(g_sb_flags&SB_KERNEL_OK)?"checked by the loader: OK":(g_sb_flags&SB_OVERRIDE)?"FAILED - started by the user as untrusted":"not checked"); p=scopy(p,"\n"); *p=0; out(b);
    p=b; p=scopy(p,"  Signing key        "); p=sb_hex(p,g_buildsig.keyid,8); if(g_sb_info){ int same=1; for(int i=0;i<8;i++) if(g_sb_keyid[i]!=g_buildsig.keyid[i])same=0; p=scopy(p,same?" (loader key: same)":" (loader key: "); if(!same){ p=sb_hex(p,g_sb_keyid,8); p=scopy(p,")"); } } p=scopy(p,"\n"); *p=0; out(b);
    p=b; p=scopy(p,"  Linux files        "); p=scopy(p,!g_buildsig.sr_len?"none in this build":g_lockdown&&(g_sb_flags&SB_SYSROOT_OK)?"checked by the loader: OK":g_lockdown?"checked when read from the boot partition":"not checked"); p=scopy(p,"\n"); *p=0; out(b);
    p=b; p=scopy(p,"  Security version   "); p=udec(p,g_buildsig.svn); p=scopy(p,", lowest allowed "); if(g_sb_info&&(g_sb_flags&SB_ROLLBACK)){ p=udec(p,g_sb_minsvn); p=scopy(p," (stored in firmware)"); } else p=scopy(p,"not stored"); p=scopy(p,"\n"); *p=0; out(b);
    p=b; p=scopy(p,"  Lockdown           "); p=scopy(p,g_lockdown?"on (no kernel modules, no unsigned updates, no continuing a modified kernel)":"off"); p=scopy(p,"\n"); *p=0; out(b);
}
int secboot_check_file(const u8* d,u32 n,u32* body){
    if(body)*body=0;
    if(!d||n<152u)return -1;
    const u8* t=d+n-88u;
    if(sv_rd32(t+80)!=0x4658564Eu||sv_rd32(t+84)!=0x31454C49u)return -1;
    u32 len=sv_rd32(t+72); if(len!=n-88u||sv_rd32(t+76)!=1u)return -2;
    if(!secboot_signed())return -5;
    u8 pub[65]; sb_pub(pub);
    u8 kd[32]; sv_sha c; sv_sha_init(&c); sv_sha_update(&c,pub,65); sv_sha_final(&c,kd);
    for(int i=0;i<8;i++) if(t[64+i]!=kd[i])return -3;
    u8 h[32]; sv_sha_init(&c);
    for(u32 a=0;a<len;){ u32 q=len-a; if(q>4194304u)q=4194304u; sv_sha_update(&c,d+a,q); a+=q; }
    sv_sha_final(&c,h);
    if(sv_p256_verify(pub,h,t))return -4;
    if(body)*body=len;
    return 0; }
const char* secboot_file_why(int r){
    switch(r){ case 0: return "signature OK"; case -1: return "not signed"; case -2: return "signature block damaged"; case -3: return "signed with another key"; case -4: return "file changed after signing"; case -5: return "this kernel has no signing key"; default: return "unknown error"; } }
