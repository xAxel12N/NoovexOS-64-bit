#include "include/nvx/base.h"
#include "include/nvx/core/kaslr.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"

int g_kaslr_on=1;
u64 g_kaslr_text_slide, g_kaslr_heap, g_kaslr_kstk, g_kaslr_pf_seed;
static int g_kaslr_kstk_set;
static u64 g_kaslr_last_brk, g_kaslr_last_mmap, g_kaslr_last_pie, g_kaslr_last_stk, g_kaslr_last_gap, g_kaslr_last_interp, g_kaslr_last_stkva;
u64 kaslr_rand(u64 n){ if(!g_kaslr_on||n<2)return 0; u64 r=0; csprng_bytes((u8*)&r,8); return r%n; }
u64 kaslr_kstk_off(void){ if(!g_kaslr_kstk_set){ g_kaslr_kstk=kaslr_rand(64)<<4; g_kaslr_kstk_set=1; } return g_kaslr_kstk; }
u64 kaslr_user_brk(void){ return g_kaslr_last_brk=kaslr_rand(1024)<<12; }
u64 kaslr_user_mmap(void){ return g_kaslr_last_mmap=kaslr_rand(1ULL<<KASLR_MMNEXT_BITS)<<12; }
u64 kaslr_gap_hint(void){ return g_kaslr_last_gap=kaslr_rand(1ULL<<KASLR_MMAP_BITS)<<12; }
u64 kaslr_pie_slide(u64 room,int shift,u64 maxn){ u64 n=room>>shift; if(n>maxn)n=maxn; return g_kaslr_last_pie=kaslr_rand(n+1)<<shift; }
u64 kaslr_interp_slide(u64 room,int shift){ u64 n=room>>shift; if(n>(1ULL<<28))n=1ULL<<28; return g_kaslr_last_interp=kaslr_rand(n)<<shift; }
u64 kaslr_stack_room(u64 room){ u64 n=(room>KASLR_STK_MAX?KASLR_STK_MAX:room)>>4; return g_kaslr_last_stk=kaslr_rand(n)<<4; }
u64 kaslr_user_stack(u64 fixed,u64 below){ if(!g_kaslr_on)return g_kaslr_last_stkva=fixed; return g_kaslr_last_stkva=KASLR_STK_LO+((below+0xFFFULL)&~0xFFFULL)+(kaslr_rand(1ULL<<KASLR_STK_BITS)<<12); }
u64 kaslr_stack_off(void){ return kaslr_stack_room(4096); }
int kaslr_align_shift(u64 a){ int s=12; while(s<21&&(1ULL<<s)<a)s++; return s; }
int kaslr_hide_addr(void){ return g_uid!=0; }
static int ka_hx(char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
static int ka_al(char c){ return (c>='a'&&c<='f')||(c>='A'&&c<='F'); }
int kaslr_mask(char* s,int n){
    if(!s||!kaslr_hide_addr())return 0;
    int hits=0;
    for(int i=0;i<n&&s[i];){
        if(i&&(ka_hx(s[i-1])||s[i-1]=='x'||s[i-1]=='X')){ i++; continue; }
        int px=s[i]=='0'&&i+1<n&&(s[i+1]=='x'||s[i+1]=='X'), a=i+(px?2:0), j=a, al=0;
        while(j<n&&ka_hx(s[j])){ if(ka_al(s[j]))al=1; j++; }
        if((px&&j-a>=5)||(!px&&j-a>=8&&al)){ for(int k=a;k<j;k++)s[k]='*'; hits++; }
        i=j>i?j:i+1; }
    return hits; }
static char* ka_s(char* o,char* e,const char* s){ while(*s&&o<e)*o++=*s++; return o; }
static char* ka_x(char* o,char* e,u64 v){ char d[17]; int n=0; do{ d[n++]="0123456789abcdef"[v&15]; v>>=4; }while(v&&n<16); o=ka_s(o,e,"0x"); while(n&&o<e)*o++=d[--n]; return o; }
int kaslr_report(char* o,int cap){
    char* p=o; char* e=o+cap-1;
    p=ka_s(p,e,"kaslr: "); p=ka_s(p,e,g_kaslr_on?"on\n":"off\n"); p=ka_s(p,e,"  boot cmdline   not passed to the kernel, so there is no nokaslr option\n");
    p=ka_s(p,e,"  kernel image   fixed at 0x100000 (not relocatable), slide "); p=ka_x(p,e,g_kaslr_text_slide); p=ka_s(p,e,"\n");
    p=ka_s(p,e,"  kernel heap    slide "); p=ka_x(p,e,g_kaslr_heap); p=ka_s(p,e,"\n");
    p=ka_s(p,e,"  kernel stacks  offset "); p=ka_x(p,e,g_kaslr_kstk); p=ka_s(p,e,"\n");
    p=ka_s(p,e,"  frame alloc    "); p=ka_s(p,e,g_kaslr_pf_seed?"randomized order\n":"linear order\n");
    p=ka_s(p,e,"  user bits      mmap 26, ld.so 25 (4K align), pie 12 small / 24 big, brk 10, stack VA 22 + 12 in-page\n"); p=ka_s(p,e,"  last exec      pie "); p=ka_x(p,e,g_kaslr_last_pie); p=ka_s(p,e,"  brk "); p=ka_x(p,e,g_kaslr_last_brk);
    p=ka_s(p,e,"  mmap "); p=ka_x(p,e,g_kaslr_last_mmap); p=ka_s(p,e,"  stack "); p=ka_x(p,e,g_kaslr_last_stkva); p=ka_s(p,e,"-"); p=ka_x(p,e,g_kaslr_last_stk); p=ka_s(p,e,"  ld.so "); p=ka_x(p,e,g_kaslr_last_interp); p=ka_s(p,e,"  gap "); p=ka_x(p,e,g_kaslr_last_gap); p=ka_s(p,e,"\n");
    p=ka_s(p,e,"  last mmap gap  "); p=ka_x(p,e,g_kaslr_last_gap); p=ka_s(p,e,"\n");
    *p=0; return (int)(p-o); }
