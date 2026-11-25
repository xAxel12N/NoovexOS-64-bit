typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
extern const unsigned char s3_wake_start[], s3_wake_end[];
int s3_save(void);
static inline void outb(u16 p,u8 v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(u16 p,u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl(u16 p,u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline u16 inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static void puts_(const char* s){ while(*s)outb(0x3F8,(u8)*s++); }
static void hex(u64 v){ char b[19]; b[0]='0'; b[1]='x'; for(int i=0;i<16;i++){ int d=(int)((v>>(60-4*i))&15); b[2+i]=(char)(d<10?'0'+d:'a'+d-10); } b[18]=0; puts_(b); }
static u32 r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int sig(const u8* p,const char* s){ return p[0]==s[0]&&p[1]==s[1]&&p[2]==s[2]&&p[3]==s[3]; }
static void quit(int c){ outl(0xF4,(u32)c); for(;;)__asm__ volatile("cli; hlt"); }
static int pkgint(const u8** pp,u32* o){ const u8* p=*pp; u8 op=*p++; if(op==0)*o=0; else if(op==1)*o=1; else if(op==0x0A)*o=*p++; else return -1; *pp=p; return 0; }
static volatile u64 g_magic=0x1122334455667788ULL;
void s3t_main(void){
    puts_("s3t: start\n");
    const u8* rsdp=0; for(u64 a=0xE0000;a<0x100000;a+=16){ const u8* p=(const u8*)a; if(sig(p,"RSD ")&&sig(p+4,"PTR ")){ rsdp=p; break; } }
    if(!rsdp){ puts_("s3t: FAIL no RSDP\n"); quit(3); }
    const u8* rs=(const u8*)(u64)r32(rsdp+16); u32 n=(r32(rs+4)-36)/4; const u8* fadt=0;
    for(u32 i=0;i<n;i++){ const u8* t=(const u8*)(u64)r32(rs+36+i*4); if(sig(t,"FACP"))fadt=t; }
    if(!fadt){ puts_("s3t: FAIL no FADT\n"); quit(3); }
    u8* facs=(u8*)(u64)r32(fadt+36); const u8* dsdt=(const u8*)(u64)r32(fadt+40);
    u16 pm1a=(u16)r32(fadt+64), evt=(u16)r32(fadt+56);
    u32 typ=0xFF; u32 dl=r32(dsdt+4);
    for(const u8* q=dsdt+36;q+8<dsdt+dl;q++) if(q[0]=='_'&&q[1]=='S'&&q[2]=='3'&&q[3]=='_'&&q[-1]==0x08&&q[4]==0x12){ const u8* p=q+5; p+=1+((*p>>6)&3); p++; if(!pkgint(&p,&typ))break; }
    if(!sig(facs,"FACS")||typ==0xFF){ puts_("s3t: SKIP no FACS or no _S3\n"); quit(5); }
    u32 tl=(u32)(s3_wake_end-s3_wake_start); u8* d=(u8*)0x9000; for(u32 i=0;i<tl;i++)d[i]=s3_wake_start[i];
    *(volatile u32*)(facs+12)=0x9000;
    u64 rbx_canary=0xCAFEBABEDEADBEEFULL; __asm__ volatile("":"+r"(rbx_canary));
    if(!s3_save()){
        puts_("s3t: entering S3 typ="); hex(typ); puts_("\n");
        outw(evt,0x8000); u16 c=(u16)((inw(pm1a)&0xC3FF)|((typ&7)<<10)); outw(pm1a,c); outw(pm1a,(u16)(c|(1<<13)));
        for(int i=0;i<100000000;i++)__asm__ volatile("pause");
        puts_("s3t: FAIL machine did not sleep\n"); quit(7);
    }
    u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    puts_("s3t: resumed cr3="); hex(cr3); puts_("\n");
    if(g_magic!=0x1122334455667788ULL||rbx_canary!=0xCAFEBABEDEADBEEFULL){ puts_("s3t: FAIL state lost\n"); quit(9); }
    puts_("S3 HOST TEST: RESUME PASS\n"); quit(0x10);
}
