#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint64_t u64;
__attribute__((aligned(4096))) u64 __bss_start[1];
__attribute__((aligned(4096))) u64 pml4[512], pdpt[512], pd0[512*4], pdfb[512*2];
__attribute__((aligned(4096))) u64 junk_tail[4096]; u64 __bss_end[1];
u64 t_fb; void run_entry(void);
int g_marks[8], g_nm; unsigned g_mcol[8]; int g_bss_zero_at_mark3;
void uefi_mark(int i,unsigned c){ if(g_nm<8){ g_marks[g_nm]=i; g_mcol[g_nm]=c; g_nm++; } if(i==3)g_bss_zero_at_mark3=(junk_tail[0]==0); }
static int ok=1; static void chk(const char* w,int c){ printf("  %-62s %s\n",w,c?"ok":"FAIL"); if(!c)ok=0; }
int main(void){
  for(u64 fbcase=0;fbcase<3;fbcase++){
    memset(pml4,0x5A,sizeof pml4); memset(pdpt,0x5A,sizeof pdpt); memset(pd0,0x5A,sizeof pd0); memset(pdfb,0x5A,sizeof pdfb); memset(junk_tail,0x5A,sizeof junk_tail);
    t_fb = fbcase==0?0x80000000ULL : fbcase==1?0x4000000000ULL : 0x7FC0000000ULL;
    g_nm=0; g_bss_zero_at_mark3=0; run_entry();
    printf("== framebuffer at 0x%llx ==\n",(unsigned long long)t_fb);
    chk("markers 3, 4, 5 drawn in order (entry, BSS cleared, tables built)", g_nm==3&&g_marks[0]==2&&g_marks[1]==3&&g_marks[2]==4);
    chk("marker 4 is drawn only after the BSS is really cleared", g_bss_zero_at_mark3);
    chk("BSS zeroed (stale firmware data gone)", junk_tail[0]==0&&junk_tail[4095]==0);
    chk("PML4[0] -> PDPT, present+writable; other PML4 entries empty", pml4[0]==((u64)pdpt|3)&&pml4[1]==0&&pml4[511]==0);
    int pdok=1; for(int i=0;i<4;i++) if(pdpt[i]!=(((u64)pd0+(u64)i*4096)|3))pdok=0; chk("PDPT[0..3] -> PD0..PD3", pdok);
    int idok=1; for(int i=0;i<2048;i++) if(pd0[i]!=(((u64)i<<21)|0x83))idok=0; chk("identity map 0-4 GB with 2 MB pages", idok);
    if(fbcase==0){ int empty=1; for(int i=4;i<512;i++) if(pdpt[i])empty=0; chk("framebuffer below 4 GB: no extra mapping", empty&&pdfb[0]==0); }
    else { u64 gi=t_fb>>30; int m=pdpt[gi]==(((u64)pdfb)|3)&&(gi+1>=512||pdpt[gi+1]==(((u64)pdfb+4096)|3));
      chk("framebuffer above 4 GB: its two 1 GB blocks mapped", m);
      int fbok=1; for(int i=0;i<512;i++) if(pdfb[i]!=(((gi<<30)+((u64)i<<21))|0x83))fbok=0; for(int i=0;i<512&&gi+1<512;i++) if(pdfb[512+i]!=((((gi+1)<<30)+((u64)i<<21))|0x83))fbok=0;
      chk("framebuffer pages are identity-mapped 2 MB pages", fbok);
      u64 va=t_fb+0x123456; u64 pde=(pdfb[(va>>21)&511]); chk("a framebuffer address translates to itself", (pde&~0x1FFFFFULL)+(va&0x1FFFFF)==va+0||((pde&~(u64)0x1FFFFF)|(va&0x1FFFFF))==va); }
  }
  printf("%s\n",ok?"ALL PASS":"FAILURES"); return !ok; }
