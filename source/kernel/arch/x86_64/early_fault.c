#include "include/nvx/base.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/mm/pmm_vmm.h"

extern const u64 early_exc_stubs[32];
volatile int g_early_step=0;
static u64 g_eidt[64] __attribute__((section(".data"),aligned(16)))={1};
static int ef_y=0;

static void ef_px(u64 fb,u32 pitch,int x,int y,u32 c){ *(volatile u32*)(fb+(u64)y*pitch+(u64)x*4)=c; }
static void ef_text(const char* s,u32 col){
    u64 fb=*(volatile u64*)0x5000ULL; u32 pitch=*(volatile u32*)0x5008ULL; int w=*(volatile u16*)0x500CULL, h=*(volatile u16*)0x500EULL;
    if(!fb||!pitch||w<200||h<100)return; int sc=w>=1600?2:1; int y0=h/3+ef_y*(16*sc+4); if(y0+16*sc>=h-60)return;
    for(int yy=y0-2;yy<y0+16*sc+2;yy++) for(int xx=20;xx<w-20;xx++)ef_px(fb,pitch,xx,yy,0x00200000u);
    int x=30; for(int i=0;s[i]&&x+8*sc<w-20;i++){ int idx=(u8)s[i]-32; if(idx>=0&&idx<95){ const unsigned char* g=fontaa[idx];
            for(int r=0;r<14;r++) for(int c=0;c<8;c++) if(g[r*8+c]>110) for(int dy=0;dy<sc;dy++) for(int dx=0;dx<sc;dx++)ef_px(fb,pitch,x+c*sc+dx,y0+r*sc+dy,col); }
        x+=8*sc; }
    ef_y++; }
static char* ef_hex(char* p,u64 v){ static const char H[]="0123456789ABCDEF"; *p++='0'; *p++='x'; int st=0; for(int i=60;i>=0;i-=4){ int d=(int)((v>>i)&15); if(d||st||i==0){ *p++=H[d]; st=1; } } return p; }
static char* ef_str(char* p,const char* s){ while(*s)*p++=*s++; return p; }
static char* ef_dec(char* p,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
static const char* ef_name(u64 v){ switch(v){ case 0: return "divide error"; case 1: return "debug"; case 2: return "NMI"; case 3: return "breakpoint"; case 4: return "overflow"; case 5: return "bound range"; case 6: return "invalid instruction"; case 7: return "no FPU"; case 8: return "double fault"; case 10: return "invalid TSS"; case 11: return "segment not present"; case 12: return "stack fault"; case 13: return "general protection fault"; case 14: return "page fault"; case 16: return "x87 FPU error"; case 17: return "alignment check"; case 18: return "machine check"; case 19: return "SIMD error"; case 21: return "control protection"; } return "CPU exception"; }
static const char* ef_stepname(int s){ switch(s){ case 1: return "clearing kernel memory"; case 2: return "per-CPU setup"; case 3: return "kernel snapshot"; case 4: return "reading the memory map"; case 5: return "FPU setup"; case 6: return "framebuffer setup"; case 7: return "early hardware setup"; } return "start-up"; }
void early_fault(u64* f){
    u64 vec=f[0], err=f[1], rip=f[2], cr2=0, cr3=0; __asm__ volatile("mov %%cr2,%0":"=r"(cr2)); __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    char b[160],*p; ef_y=0;
    ef_text("NoovexOS stopped during start-up. Please take a photo of this screen.",0x00FFFFFFu);
    p=b; p=ef_str(p,"Exception "); p=ef_dec(p,vec); p=ef_str(p," ("); p=ef_str(p,ef_name(vec)); p=ef_str(p,")  error code "); p=ef_hex(p,err); *p=0; ef_text(b,0x00FF8080u);
    p=b; p=ef_str(p,"At RIP "); p=ef_hex(p,rip); p=ef_str(p,"   address (CR2) "); p=ef_hex(p,cr2); p=ef_str(p,"   CR3 "); p=ef_hex(p,cr3); *p=0; ef_text(b,0x00FFD080u);
    p=b; p=ef_str(p,"Step "); p=ef_dec(p,(u64)g_early_step); p=ef_str(p,": "); p=ef_str(p,ef_stepname(g_early_step)); p=ef_str(p,*(volatile u32*)0x502CULL==0x49464555u?"   (UEFI boot)":"   (legacy BIOS boot)"); *p=0; ef_text(b,0x00A0E0FFu);
    p=b; p=ef_str(p,"Stack "); p=ef_hex(p,f[5]); p=ef_str(p,"   RFLAGS "); p=ef_hex(p,f[4]); *p=0; ef_text(b,0x00C0C0C0u);
    if(vec==14&&cr2<0x8000000000ULL){ u64* pml4=(u64*)(cr3&0x000FFFFFFFFFF000ULL); u64 e4=pml4[(cr2>>39)&511]; p=b; p=ef_str(p,"Page table: PML4E "); p=ef_hex(p,e4);
        if(e4&1){ u64* pdpt=(u64*)(e4&0x000FFFFFFFFFF000ULL); u64 e3=pdpt[(cr2>>30)&511]; p=ef_str(p,"  PDPTE "); p=ef_hex(p,e3);
            if((e3&1)&&!(e3&0x80)){ u64* pd=(u64*)(e3&0x000FFFFFFFFFF000ULL); u64 e2=pd[(cr2>>21)&511]; p=ef_str(p,"  PDE "); p=ef_hex(p,e2); p=ef_str(p," at "); p=ef_hex(p,(u64)&pd[(cr2>>21)&511]); } }
        *p=0; ef_text(b,0x00FFB0FFu); }
    { const volatile u8* q=(const volatile u8*)0x7000ULL; if(*(const volatile u32*)q==0x4D58564Eu){ u32 n=*(const volatile u32*)(q+4)&0x7FFFFFFFu; if(n>300)n=300;
        u64 chk[3]={cr2,0x10000000ULL,cr3}; const char* nm[3]={"Fault address","Kernel memory 0x10000000","Page tables"};
        for(int c=0;c<3;c++){ u32 t=0xFFFFFFFFu; u64 rs=0,re=0; for(u32 i=0;i<n;i++){ u64 s=*(const volatile u64*)(q+8+i*24), e=*(const volatile u64*)(q+16+i*24); if(chk[c]>=s&&chk[c]<e){ t=*(const volatile u32*)(q+24+i*24); rs=s; re=e; break; } }
            p=b; p=ef_str(p,nm[c]); p=ef_str(p," is in firmware memory type "); if(t==0xFFFFFFFFu)p=ef_str(p,"(not listed)"); else { p=ef_dec(p,t); p=ef_str(p," ("); p=ef_hex(p,rs); p=ef_str(p," - "); p=ef_hex(p,re); p=ef_str(p,")"); } *p=0; ef_text(b,0x00B0FFB0u); } } }
    for(;;)__asm__ volatile("cli; hlt"); }
void early_idt_install(void){
    for(int i=0;i<32;i++){ u64 a=early_exc_stubs[i]; u64 lo=(a&0xFFFFULL)|((u64)0x08<<16)|((u64)0x8E00<<32)|(((a>>16)&0xFFFFULL)<<48); g_eidt[i*2]=lo; g_eidt[i*2+1]=a>>32; }
    struct __attribute__((packed)) { u16 l; u64 b; } ip={ (u16)(32*16-1), (u64)g_eidt };
    __asm__ volatile("lidt %0"::"m"(ip)); }
void early_mark(int idx,u32 col){
    if(*(volatile u32*)0x502CULL!=0x49464555u)return;
    u64 fb=*(volatile u64*)0x5000ULL; u32 mp=*(volatile u32*)0x5008ULL; u32 mh=*(volatile u16*)0x500EULL; if(!fb||mh<=40)return;
    u8* r=(u8*)(fb+(u64)(mh-36)*mp+(u64)(16+idx*28)*4); for(int y=0;y<24;y++){ u32* q=(u32*)(r+(u64)y*mp); for(int x=0;x<24;x++)q[x]=col; } }
void early_note(const char* title,const char* what,u64 rip,int stage){
    extern volatile u32 g_stray_n; extern volatile int g_stray_vec; extern int g_ioapic_n, g_ioapic_masked; extern volatile u64 g_ticks;
    char b[160],*p; ef_y=0; ef_text(title,0x00FFFFFFu);
    p=b; p=ef_str(p,"Main loop "); p=ef_str(p,what); p=ef_str(p," "); p=ef_dec(p,(u64)stage); p=ef_str(p,"   interrupted at RIP "); p=ef_hex(p,rip); *p=0; ef_text(b,0x00FFD080u);
    p=b; p=ef_str(p,"Stray interrupts "); p=ef_dec(p,g_stray_n); p=ef_str(p,"  last vector "); if(g_stray_vec>=0)p=ef_dec(p,(u64)g_stray_vec); else p=ef_str(p,"none"); p=ef_str(p,"   I/O APICs "); p=ef_dec(p,(u64)g_ioapic_n); p=ef_str(p,"  lines masked "); p=ef_dec(p,(u64)g_ioapic_masked); p=ef_str(p,"   ticks "); p=ef_dec(p,g_ticks); *p=0; ef_text(b,0x00A0E0FFu); }
static u64 g_pdtmp[512] __attribute__((section(".data"),aligned(4096)))={0};
u32 g_remap_n __attribute__((section(".data")))=0; u32 g_remap_bad_first __attribute__((section(".data")))=0; u32 g_remap_fail __attribute__((section(".data")))=0;
u64 g_remap_lo __attribute__((section(".data")))=0, g_remap_hi __attribute__((section(".data")))=0, g_remap_badlo __attribute__((section(".data")))=0, g_remap_badhi __attribute__((section(".data")))=0;
static int er_map(u64* s,u64* e,u32* t,int max){
    const volatile u8* q=(const volatile u8*)0x7000ULL; u32 mg=*(const volatile u32*)q; int n=0;
    if(mg==0x4D58564Eu){ u32 c=*(const volatile u32*)(q+4)&0x7FFFFFFFu; for(u32 i=0;i<c&&n<max;i++){ s[n]=*(const volatile u64*)(q+8+i*24); e[n]=*(const volatile u64*)(q+16+i*24); u32 ty=*(const volatile u32*)(q+24+i*24); t[n]=(ty==7||ty==4)?1u:0u; if(e[n]>s[n])n++; } }
    else if(mg==0x4558564Eu){ u32 c=*(const volatile u32*)(q+4); if(c>40)c=40; for(u32 i=0;i<c&&n<max;i++){ u64 b=*(const volatile u64*)(q+8+i*24), l=*(const volatile u64*)(q+16+i*24); u32 ty=*(const volatile u32*)(q+24+i*24); if(!l||b+l<b)continue; s[n]=b; e[n]=b+l; t[n]=ty==1?1u:0u; n++; } }
    return n; }
static int er_ok(u64 p,u64* s,u64* e,u32* t,int n){ u64 c=p, end=p+0x200000ULL; for(int k=0;k<64&&c<end;k++){ int mv=0; for(int i=0;i<n;i++) if(t[i]&&s[i]<=c&&e[i]>c){ c=e[i]; mv=1; } if(!mv)return 0; } return c>=end; }
static int er_bad_anywhere(u64 p,u64* s,u64* e,u32* t,int n){ for(int i=0;i<n;i++) if(!t[i]&&s[i]<p+0x200000ULL&&e[i]>p)return 1; return 0; }
void early_remap(void){
    extern char __bss_end[]; u64 s[64],e[64]; u32 t[64]; int n=er_map(s,e,t,64); if(!n)return;
    u64 pages[80]; int np=0; u64 R[2][2]={{0x09800000ULL,0x09E00000ULL},{0x0E000000ULL,((u64)__bss_end+0x1FFFFFULL)&~0x1FFFFFULL}};
    for(int r=0;r<2;r++) for(u64 p=R[r][0];p<R[r][1]&&np<80;p+=0x200000ULL) if(!er_ok(p,s,e,t,n)||er_bad_anywhere(p,s,e,t,n)){ if(!np){ g_remap_badlo=p; } g_remap_badhi=p+0x200000ULL; pages[np++]=p; }
    if(!np)return;
    int bi=-1; u64 best=0; for(int i=0;i<n;i++){ if(!t[i]||e[i]<=0x100000000ULL)continue; u64 lo=s[i]<0x100000000ULL?0x100000000ULL:s[i]; lo=(lo+0x1FFFFFULL)&~0x1FFFFFULL; u64 hi=e[i]&~0x1FFFFFULL; if(hi>lo&&hi-lo>best){ best=hi-lo; bi=i; } }
    if(bi<0||best<(u64)np*0x200000ULL){ g_remap_fail=1; return; }
    u64 hi=e[bi]&~0x1FFFFFULL, lo=hi-(u64)np*0x200000ULL; if(hi-lo>0x40000000ULL){ g_remap_fail=2; return; }
    u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3)); u64* pml4=(u64*)(cr3&0x000FFFFFFFFFF000ULL); u64* pdpt=(u64*)(pml4[0]&0x000FFFFFFFFFF000ULL);
    u64 save4=pdpt[4]; for(int k=0;k<512;k++)g_pdtmp[k]=0;
    for(int k=0;k<np;k++)g_pdtmp[k]=(lo+(u64)k*0x200000ULL)|0x83ULL;
    pdpt[4]=((u64)g_pdtmp)|3ULL; __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
    u64 rsp; __asm__ volatile("mov %%rsp,%0":"=r"(rsp)); int sk=-1;
    for(int k=0;k<np;k++){ if(rsp>=pages[k]&&rsp<pages[k]+0x200000ULL){ sk=k; continue; } volatile u64* src=(volatile u64*)pages[k]; volatile u64* dst=(volatile u64*)(0x100000000ULL+(u64)k*0x200000ULL); for(int w=0;w<0x200000/8;w++)dst[w]=src[w];
        u64 v=pages[k]; u64* pd=(u64*)(pdpt[(v>>30)&511]&0x000FFFFFFFFFF000ULL); pd[(v>>21)&511]=(lo+(u64)k*0x200000ULL)|0x83ULL; }
    if(sk>=0){ u64 v=pages[sk]; u64* pd=(u64*)(pdpt[(v>>30)&511]&0x000FFFFFFFFFF000ULL); u64* pde=&pd[(v>>21)&511]; u64 nv=(lo+(u64)sk*0x200000ULL)|0x83ULL;
        u64 src=v, dst=0x100000000ULL+(u64)sk*0x200000ULL, cnt=0x200000/8;
        __asm__ volatile("cld\n\trep movsq\n\tmovq %3,(%4)\n\tmovq %5,%%rax\n\tmovq %%rax,%%cr3" : "+S"(src),"+D"(dst),"+c"(cnt) : "r"(nv),"r"(pde),"r"(cr3) : "rax","memory"); }
    __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
    pdpt[4]=save4; __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
    g_remap_n=(u32)np; g_remap_lo=lo; g_remap_hi=hi; }
void early_remap_fix_mm(void){
    if(!g_remap_n)return; extern mmreg g_mmr[]; extern int g_mmr_n;
    for(int i=0;i<g_mmr_n;i++) if(g_mmr[i].s<=g_remap_lo&&g_mmr[i].e>=g_remap_hi){ g_mmr[i].e=g_remap_lo; break; } }
