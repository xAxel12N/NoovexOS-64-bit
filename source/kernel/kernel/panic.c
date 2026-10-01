/* =========================================================================
   Red Screen of Death
   ========================================================================= */
static const char *EXC[32]={
 "Divide Error","Debug","NMI","Breakpoint","Overflow","BOUND Range","Invalid Opcode",
 "Device N/A","Double Fault","Coproc Overrun","Invalid TSS","Segment Not Present",
 "Stack Fault","General Protection","Page Fault","Reserved","x87 FP Error",
 "Alignment Check","Machine Check","SIMD FP","Virtualization","Control Protection",
 "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved","Hypervisor",
 "VMM Comm","Security","Reserved"};
void panic(regs_t*r,const char*msg){
    static volatile int in_panic=0;
    __asm__ volatile("cli");
    if(in_panic){ for(;;) __asm__ volatile("hlt"); }   /* a fault while panicking -> just halt, don't triple-fault */
    in_panic=1;
    if(!FBB||!W||!H){ for(;;) __asm__ volatile("hlt"); }
    for(u32 y=0;y<H;y++){ u32 c=mix(rgb(178,30,40),rgb(120,16,26),(int)((y*256)/H));
        for(u32 x=0;x<W;x++) SCENE[(u64)y*SW+x]=c; }
    int cx=64,y=58; dstr(cx,y,":(",rgb(255,255,255),6); y+=90;
    dstr(cx,y,"NoovexOS ran into a problem and needs to stop.",rgb(255,255,255),2); y+=52;
    fill_round(cx,y,W-cx-64,4,2,15,rgb(255,180,185)); y+=20;
    char L[160],*p;
    if(r && r->vector<32){
        p=L;p=scopy(p,"KERNEL PANIC  -  ");p=scopy(p,EXC[r->vector]);p=scopy(p,"  (vector ");p=udec(p,(u32)r->vector);*p++=')';*p=0;
        dstr(cx,y,L,rgb(255,235,235),2); y+=40;
        p=L;p=scopy(p,"error=");p=uhex(p,r->errcode,4);p=scopy(p,"   RIP=");p=uhex(p,r->rip,16);*p=0; dstr(cx,y,L,rgb(255,220,220),1); y+=20;
        p=L;p=scopy(p,"RSP=");p=uhex(p,r->rsp,16);p=scopy(p,"   RFLAGS=");p=uhex(p,r->rflags,8);*p=0; dstr(cx,y,L,rgb(255,220,220),1); y+=26;
        u64 a1[8]={r->rax,r->rbx,r->rcx,r->rdx,r->rsi,r->rdi,r->rbp,r->rsp};
        u64 a2[8]={r->r8,r->r9,r->r10,r->r11,r->r12,r->r13,r->r14,r->r15};
        const char*n1[8]={"RAX","RBX","RCX","RDX","RSI","RDI","RBP","RSP"};
        const char*n2[8]={"R8 ","R9 ","R10","R11","R12","R13","R14","R15"};
        for(int i=0;i<8;i++){ p=L;p=scopy(p,n1[i]);*p++='=';p=uhex(p,a1[i],16);p=scopy(p,"   ");p=scopy(p,n2[i]);*p++='=';p=uhex(p,a2[i],16);*p=0;
            dstr(cx,y,L,rgb(240,210,210),1); y+=18; }
    } else { dstr(cx,y,"KERNEL PANIC",rgb(255,235,235),2); y+=40; dstr(cx,y,msg?msg:"Unspecified fatal error.",rgb(255,225,225),1); y+=24; }
    y+=18; dstr(cx,y,"The system has been halted.  Please power off and restart.",rgb(255,255,255),1);
    dstr(cx,(int)H-30,"NoovexOS 64  -  x86-64",rgb(255,180,185),1);
    blit_all(); for(;;) __asm__ volatile("hlt");
}

