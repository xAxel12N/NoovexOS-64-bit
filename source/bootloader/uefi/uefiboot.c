typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef unsigned long long UINTN; typedef u64 EFI_STATUS; typedef void* EFI_HANDLE;
#define EFIAPI __attribute__((ms_abi))
#define EFI_ERR(n) (0x8000000000000000ULL|(n))
#define EFI_LOAD_ERROR EFI_ERR(1)
#define EFI_INVALID_PARAMETER EFI_ERR(2)
#define EFI_UNSUPPORTED EFI_ERR(3)
#define EFI_BUFFER_TOO_SMALL EFI_ERR(5)
#define EFI_OUT_OF_RESOURCES EFI_ERR(9)
#define EFI_NOT_FOUND EFI_ERR(14)
typedef struct { u32 Data1; u16 Data2; u16 Data3; u8 Data4[8]; } EFI_GUID;
typedef struct { u32 Type; u32 Pad; u64 PhysicalStart; u64 VirtualStart; u64 NumberOfPages; u64 Attribute; } EFI_MEMORY_DESCRIPTOR;
typedef struct { u16 ScanCode; u16 UnicodeChar; } EFI_INPUT_KEY;
typedef struct EFI_TEXT_IN { EFI_STATUS (EFIAPI *Reset)(struct EFI_TEXT_IN*,u8); EFI_STATUS (EFIAPI *ReadKeyStroke)(struct EFI_TEXT_IN*,EFI_INPUT_KEY*); void* WaitForKey; } EFI_TEXT_IN;
typedef struct EFI_TEXT_OUT { void* Reset; EFI_STATUS (EFIAPI *OutputString)(struct EFI_TEXT_OUT*,u16*); void* TestString; void* QueryMode; void* SetMode; EFI_STATUS (EFIAPI *SetAttribute)(struct EFI_TEXT_OUT*,UINTN); EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_TEXT_OUT*); } EFI_TEXT_OUT;
typedef struct { u32 Version, HorizontalResolution, VerticalResolution, PixelFormat; u32 RedMask, GreenMask, BlueMask, ReservedMask; u32 PixelsPerScanLine; } EFI_GOP_INFO;
typedef struct { u32 MaxMode, Mode; EFI_GOP_INFO* Info; UINTN SizeOfInfo; u64 FrameBufferBase; UINTN FrameBufferSize; } EFI_GOP_MODE;
typedef struct EFI_GOP { EFI_STATUS (EFIAPI *QueryMode)(struct EFI_GOP*,u32,UINTN*,EFI_GOP_INFO**); EFI_STATUS (EFIAPI *SetMode)(struct EFI_GOP*,u32); void* Blt; EFI_GOP_MODE* Mode; } EFI_GOP;
typedef struct EFI_FILE { u64 Revision;
    EFI_STATUS (EFIAPI *Open)(struct EFI_FILE*,struct EFI_FILE**,u16*,u64,u64);
    EFI_STATUS (EFIAPI *Close)(struct EFI_FILE*); void* Delete;
    EFI_STATUS (EFIAPI *Read)(struct EFI_FILE*,UINTN*,void*); void* Write; void* GetPosition; void* SetPosition;
    EFI_STATUS (EFIAPI *GetInfo)(struct EFI_FILE*,EFI_GUID*,UINTN*,void*); } EFI_FILE;
typedef struct EFI_SFS { u64 Revision; EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SFS*,EFI_FILE**); } EFI_SFS;
typedef struct { u32 Revision; EFI_HANDLE ParentHandle; void* SystemTable; EFI_HANDLE DeviceHandle; void* FilePath; void* Reserved; u32 LoadOptionsSize; void* LoadOptions; void* ImageBase; u64 ImageSize; } EFI_LOADED_IMAGE;
typedef struct {
    u8 Hdr[24];
    void* RaiseTPL; void* RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(u32,u32,UINTN,u64*);
    EFI_STATUS (EFIAPI *FreePages)(u64,UINTN);
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN*,EFI_MEMORY_DESCRIPTOR*,UINTN*,UINTN*,u32*);
    EFI_STATUS (EFIAPI *AllocatePool)(u32,UINTN,void**);
    EFI_STATUS (EFIAPI *FreePool)(void*);
    void* CreateEvent; void* SetTimer; void* WaitForEvent; void* SignalEvent; void* CloseEvent; void* CheckEvent;
    void* InstallProtocolInterface; void* ReinstallProtocolInterface; void* UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE,EFI_GUID*,void**);
    void* Reserved; void* RegisterProtocolNotify; void* LocateHandle; void* LocateDevicePath; void* InstallConfigurationTable;
    void* LoadImage; void* StartImage; void* Exit; void* UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE,UINTN);
    void* GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN,u64,UINTN,u16*);
    void* ConnectController; void* DisconnectController; void* OpenProtocol; void* CloseProtocol; void* OpenProtocolInformation;
    void* ProtocolsPerHandle; void* LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID*,void*,void**);
} EFI_BOOT_SERVICES;
typedef struct { EFI_GUID VendorGuid; void* VendorTable; } EFI_CONFIG_TABLE;
typedef struct {
    u8 Hdr[24]; u16* FirmwareVendor; u32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle; EFI_TEXT_IN* ConIn; EFI_HANDLE ConsoleOutHandle; EFI_TEXT_OUT* ConOut;
    EFI_HANDLE StandardErrorHandle; EFI_TEXT_OUT* StdErr; void* RuntimeServices; EFI_BOOT_SERVICES* BootServices;
    UINTN NumberOfTableEntries; EFI_CONFIG_TABLE* ConfigurationTable;
} EFI_SYSTEM_TABLE;
static EFI_GUID GOP_GUID  ={0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static EFI_GUID SFS_GUID  ={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID LIP_GUID  ={0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID FINFO_GUID={0x09576e92,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID ACPI2_GUID={0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
static EFI_GUID ACPI1_GUID={0xeb9d2d30,0x2d88,0x11d3,{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
#ifdef UEFI_TEST
extern u8* g_test_phys; extern void test_jump(u64 entry);
#define PHYS(a) ((u8*)(g_test_phys+(u64)(a)))
#else
#define PHYS(a) ((u8*)(u64)(a))
#endif
#define KERNEL_PHYS 0x100000ULL
#define KERNEL_MAX  (0x6500000ULL-0x100000ULL)
#define MAX_W 1920
#define MAX_H 1080
static EFI_SYSTEM_TABLE* ST; static EFI_BOOT_SERVICES* BS;
#ifdef UEFI_TEST
u64 g_pt;
#else
static u64 g_pt;
#endif
static int in_kernel_area(u64 a,u64 n){ static const u64 R[5][2]={{0x5000,0x8000},{0x100000,0x6500000},{0x9800000,0x9C80000},{0x10000000,0x18000000},{0x18000000,0x19000000}}; for(int i=0;i<5;i++) if(a<R[i][1]&&a+n>R[i][0])return 1; return 0; }
static u64 alloc_safe(UINTN pages){ for(int t=0;t<24;t++){ u64 a=0; if(BS->AllocatePages(0,2,pages,&a)||!a)return 0; if(!in_kernel_area(a,(u64)pages*4096))return a; } return 0; }
static int build_tables(u64 fb){
    u64 t=alloc_safe(36); if(!t)return 0; u64* m=(u64*)t; for(int i=0;i<36*512;i++)m[i]=0;
    u64* pml4=m; u64* pdpt=m+512; u64* pd=m+1024; u64* pdfb=m+1024+32*512;
    pml4[0]=(u64)pdpt|3;
    for(int i=0;i<32;i++){ pdpt[i]=((u64)(pd+i*512))|3; for(int j=0;j<512;j++)pd[i*512+j]=(((u64)i<<30)+((u64)j<<21))|0x83; }
    if(fb>=(64ULL<<30)&&fb<(512ULL<<30)){ u64 gi=fb>>30; for(u64 k=0;k<2&&gi+k<512;k++){ pdpt[gi+k]=((u64)(pdfb+k*512))|3; for(int j=0;j<512;j++)pdfb[k*512+j]=(((gi+k)<<30)+((u64)j<<21))|0x83; } }
    g_pt=t; return 1;
}
static void mark(u64 fb,u32 pitch,u32 h,int idx,u32 col){ if(!fb||h<40)return; u8* row=(u8*)fb+(u64)(h-36)*pitch+(u64)(16+idx*28)*4; for(int y=0;y<24;y++){ u32* p=(u32*)(row+(u64)y*pitch); for(int x=0;x<24;x++)p[x]=col; } }
#ifndef UEFI_TEST
static inline void outb(u16 port,u8 v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
static inline void outl(u16 port,u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port)); }
static inline u32 inl(u16 port){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v; }
static u32 pcr(int b,int d,int f,int o){ outl(0xCF8,0x80000000u|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(u32)(o&0xFC)); return inl(0xCFC); }
static void pcw(int b,int d,int f,int o,u32 v){ outl(0xCF8,0x80000000u|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(u32)(o&0xFC)); outl(0xCFC,v); }
static void quiet_dma(void){
    for(int b=0;b<256;b++) for(int d=0;d<32;d++){ u32 id0=pcr(b,d,0,0); if((id0&0xFFFF)==0xFFFF)continue;
        int nf=(pcr(b,d,0,0x0C)&0x800000)?8:1;
        for(int f=0;f<nf;f++){ u32 id=pcr(b,d,f,0); if((id&0xFFFF)==0xFFFF)continue;
            if(((pcr(b,d,f,0x0C)>>16)&0x7F)!=0)continue;
            u32 cl=pcr(b,d,f,0x08); int c=(int)(cl>>24), sc=(int)((cl>>16)&0xFF), pi=(int)((cl>>8)&0xFF);
            if(!(c==0x01||c==0x02||c==0x04||c==0x0D||(c==0x0C&&sc!=0x05)))continue;
            u32 cmd=pcr(b,d,f,0x04)&0xFFFFu; if(!(cmd&0x04))continue;
            if(c==0x0C&&sc==0x03&&pi==0x30&&(cmd&0x02)){ u32 b0=pcr(b,d,f,0x10); u64 base=b0&0xFFFFFFF0u; if((b0&6)==4)base|=(u64)pcr(b,d,f,0x14)<<32;
                if(base&&(!g_pt||base<(32ULL<<30))){ volatile u32* cap=(volatile u32*)base; u64 op=base+(cap[0]&0xFF); volatile u32* uc=(volatile u32*)op;
                    uc[0]&=~1u; for(int t=0;t<2000000&&!(uc[1]&1u);t++)__asm__ volatile("pause"); } }
            pcw(b,d,f,0x04,cmd&~0x04u); } } }
static void quiet_interrupts(void){
    outb(0x21,0xFF); outb(0xA1,0xFF);
    u32 lo,hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x1B));
    u64 apic=((u64)hi<<32|lo)&0xFFFFFF000ULL;
    if((lo&(1u<<11))&&apic&&apic<0x100000000ULL){ volatile u32* lvt=(volatile u32*)(apic+0x320); *lvt|=0x10000u; *(volatile u32*)(apic+0x380)=0; }
}
#endif
static int guid_eq(const EFI_GUID* a,const EFI_GUID* b){ const u8* x=(const u8*)a; const u8* y=(const u8*)b; for(int i=0;i<16;i++) if(x[i]!=y[i])return 0; return 1; }
static void print(const char* s){ u16 b[160]; int n=0; while(*s){ if(*s=='\n'){ b[n++]='\r'; } b[n++]=(u16)(u8)*s++; if(n>=150){ b[n]=0; ST->ConOut->OutputString(ST->ConOut,b); n=0; } } b[n]=0; if(n)ST->ConOut->OutputString(ST->ConOut,b); }
static void printnum(u64 v,int base){ char t[24]; int n=0; if(!v)t[n++]='0'; while(v){ t[n++]="0123456789ABCDEF"[v%base]; v/=base; } char o[26]; int k=0; if(base==16){ o[k++]='0'; o[k++]='x'; } while(n)o[k++]=t[--n]; o[k]=0; print(o); }
static void kcopy(u8* d,const u8* s,u64 n){ if(d<s){ for(u64 i=0;i<n;i++)d[i]=s[i]; } else if(d>s){ for(u64 i=n;i>0;i--)d[i-1]=s[i-1]; } }
static EFI_STATUS fail(const char* what,EFI_STATUS st){
    ST->ConOut->SetAttribute(ST->ConOut,0x0C);
    print("\nNoovexOS cannot start: "); print(what); if(st){ print(" (status "); printnum(st&0x7FFFFFFFFFFFFFFFULL,10); print(")"); }
    ST->ConOut->SetAttribute(ST->ConOut,0x07);
    print("\nPress any key to return to the firmware boot menu.\n");
    if(ST->ConIn){ EFI_INPUT_KEY k; ST->ConIn->Reset(ST->ConIn,0); for(int i=0;i<6000;i++){ if(ST->ConIn->ReadKeyStroke(ST->ConIn,&k)==0)break; BS->Stall(10000); } }
    return st?st:EFI_LOAD_ERROR;
}
static int usable_after_exit(u32 t){ return t==1||t==2||t==3||t==4||t==7; }
#define MM_MAX 170
static u8 g_mmsum[8+MM_MAX*24];
static void mm_summarize(EFI_MEMORY_DESCRIPTOR* mmap,UINTN msz,UINTN dsz){
    u32 n=0, over=0; u8* e=g_mmsum+8;
    for(UINTN o=0;o<msz;o+=dsz){ EFI_MEMORY_DESCRIPTOR* d=(EFI_MEMORY_DESCRIPTOR*)((u8*)mmap+o); u64 s=d->PhysicalStart, en=s+d->NumberOfPages*4096; u32 t=d->Type==7?7u:usable_after_exit(d->Type)?4u:d->Type;
        int merged=0; for(u32 i=0;i<n;i++){ u64* ps=(u64*)(e+i*24); u64* pe=(u64*)(e+i*24+8); u32* pt=(u32*)(e+i*24+16); if(*pt!=t)continue; if(*pe==s){ *pe=en; merged=1; break; } if(*ps==en){ *ps=s; merged=1; break; } }
        if(merged)continue; if(n>=MM_MAX){ over=1; continue; }
        *(u64*)(e+n*24)=s; *(u64*)(e+n*24+8)=en; *(u32*)(e+n*24+16)=t; *(u32*)(e+n*24+20)=0; n++; }
    *(u32*)g_mmsum=0x4D58564Eu; *(u32*)(g_mmsum+4)=n|(over<<31); }
static const char* memtype(u32 t){ switch(t){ case 0: return "reserved"; case 5: return "runtime code"; case 6: return "runtime data"; case 8: return "unusable"; case 9: return "ACPI tables"; case 10: return "ACPI NVS"; case 11: return "MMIO"; case 12: return "MMIO port"; case 13: return "PAL code"; case 14: return "persistent"; default: return "other"; } }
static EFI_FILE* open_kernel(EFI_FILE* root,const char** which){
    static u16 p1[]={'\\','K','E','R','N','E','L','6','4','.','B','I','N',0};
    static u16 p2[]={'\\','b','o','o','t','\\','k','e','r','n','e','l','6','4','.','b','i','n',0};
    EFI_FILE* f=0;
    if(root->Open(root,&f,p1,1,0)==0&&f){ *which="\\KERNEL64.BIN"; return f; }
    f=0; if(root->Open(root,&f,p2,1,0)==0&&f){ *which="\\boot\\kernel64.bin"; return f; }
    return 0;
}
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE* systab){
    ST=systab; BS=ST->BootServices; EFI_STATUS st;
    BS->SetWatchdogTimer(0,0,0,0);
    ST->ConOut->ClearScreen(ST->ConOut);
    print("NoovexOS UEFI loader 2.0\n");
    EFI_LOADED_IMAGE* li=0; EFI_SFS* fs=0;
    if(BS->HandleProtocol(image,&LIP_GUID,(void**)&li)==0&&li&&li->DeviceHandle) BS->HandleProtocol(li->DeviceHandle,&SFS_GUID,(void**)&fs);
    if(!fs){ print("  boot device has no file system, searching other disks\n"); st=BS->LocateProtocol(&SFS_GUID,0,(void**)&fs); if(st||!fs)return fail("no readable FAT volume found",st); }
    EFI_FILE* root=0; st=fs->OpenVolume(fs,&root); if(st||!root)return fail("cannot open the boot volume",st);
    const char* which=0; EFI_FILE* kf=open_kernel(root,&which);
    if(!kf)return fail("kernel not found (looked for \\KERNEL64.BIN and \\boot\\kernel64.bin)",EFI_NOT_FOUND);
    u64 fsize=0; { u8 ib[512]; UINTN isz=sizeof ib; if(kf->GetInfo(kf,&FINFO_GUID,&isz,ib)==0)fsize=*(u64*)(ib+8); }
    if(fsize==0)fsize=KERNEL_MAX;
    if(fsize>KERNEL_MAX){ kf->Close(kf); return fail("kernel file is too large",EFI_LOAD_ERROR); }
    u64 kbuf=0; UINTN kpages=(UINTN)((fsize+4095)/4096);
    kbuf=alloc_safe(kpages); st=kbuf?0:EFI_OUT_OF_RESOURCES; if(!kbuf){ kf->Close(kf); return fail("out of memory for the kernel",st); }
    u64 total=0;
    for(;;){ UINTN chunk=1024*1024; if(total+chunk>fsize)chunk=(UINTN)(fsize-total); if(!chunk)break;
        st=kf->Read(kf,&chunk,(u8*)kbuf+total); if(st){ kf->Close(kf); return fail("error reading the kernel",st); } if(!chunk)break; total+=chunk; }
    kf->Close(kf);
    print("  kernel: "); print(which); print(", "); printnum(total/1024,10); print(" KB\n");
    u64 uentry=0, kbss=0; { u8* k=(u8*)kbuf; for(u64 i=0;i+128<=total;i++) if(k[i]=='N'&&k[i+1]=='V'&&k[i+2]=='X'&&k[i+3]=='6'&&k[i+4]=='4'&&k[i+5]=='S'&&k[i+6]=='I'&&k[i+7]=='G'){ uentry=*(u64*)(k+i+8+16+4+4+32+16); kbss=*(u64*)(k+i+120); break; } }
    if(kbss<0x10000000||kbss>0x18000000)kbss=0x18000000;
    if(!uentry||uentry<KERNEL_PHYS||uentry>=KERNEL_PHYS+total)return fail("this file is not a NoovexOS 64 kernel (no UEFI entry point)",EFI_LOAD_ERROR);
    EFI_GOP* gop=0;
    if(BS->HandleProtocol(ST->ConsoleOutHandle,&GOP_GUID,(void**)&gop)||!gop){ gop=0; st=BS->LocateProtocol(&GOP_GUID,0,(void**)&gop); if(st||!gop)return fail("no graphics output (GOP) available",st); }
    { int best=-1; u64 bscore=0;
      for(u32 m=0;m<gop->Mode->MaxMode;m++){ EFI_GOP_INFO* inf=0; UINTN isz=0; if(gop->QueryMode(gop,m,&isz,&inf)||!inf)continue;
          int fmt=inf->PixelFormat; int bgr=(fmt==1)||(fmt==2&&inf->RedMask==0xFF0000&&inf->GreenMask==0xFF00&&inf->BlueMask==0xFF); int rgb=(fmt==0);
          if(!bgr&&!rgb)continue; if(inf->HorizontalResolution>MAX_W||inf->VerticalResolution>MAX_H)continue;
          u64 sc=(u64)inf->HorizontalResolution*inf->VerticalResolution*4+(bgr?2:0)+(m==gop->Mode->Mode?1:0);
          if(sc>bscore){ bscore=sc; best=(int)m; } }
      if(best>=0&&(u32)best!=gop->Mode->Mode){ if(gop->SetMode(gop,(u32)best)){ print("  could not switch screen mode, keeping the current one\n"); } } }
    EFI_GOP_INFO* mi=gop->Mode->Info; int pf=mi->PixelFormat;
    if(pf==3)return fail("the screen has no linear framebuffer (GOP is Blt-only)",EFI_UNSUPPORTED);
    int rgbfb=(pf==0);
    print("  screen: "); printnum(mi->HorizontalResolution,10); print("x"); printnum(mi->VerticalResolution,10); print(rgbfb?" (RGB)\n":" (BGR)\n");
    u64 rsdp=0; for(UINTN i=0;i<ST->NumberOfTableEntries;i++){ if(guid_eq(&ST->ConfigurationTable[i].VendorGuid,&ACPI2_GUID)){ rsdp=(u64)ST->ConfigurationTable[i].VendorTable; break; } if(!rsdp&&guid_eq(&ST->ConfigurationTable[i].VendorGuid,&ACPI1_GUID))rsdp=(u64)ST->ConfigurationTable[i].VendorTable; }
    UINTN msz=0,mkey=0,dsz=0; u32 dver=0; EFI_MEMORY_DESCRIPTOR* mmap=0;
    st=BS->GetMemoryMap(&msz,0,&mkey,&dsz,&dver); if(st!=EFI_BUFFER_TOO_SMALL&&st)return fail("cannot read the memory map",st);
    if(dsz<sizeof(EFI_MEMORY_DESCRIPTOR))dsz=sizeof(EFI_MEMORY_DESCRIPTOR);
    UINTN mcap=msz+dsz*32; st=BS->AllocatePool(2,mcap,(void**)&mmap); if(st||!mmap)return fail("out of memory for the memory map",st);
    msz=mcap; st=BS->GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver); if(st)return fail("cannot read the memory map",st);
    u64 cur=0;
    for(int start=0;start<2&&!cur;start++){ u64 c=start==0?0x18000000ULL:0x100000ULL, c0=c;
        for(int pass=0;pass<512;pass++){ int moved=0; for(UINTN o=0;o<msz;o+=dsz){ EFI_MEMORY_DESCRIPTOR* d=(EFI_MEMORY_DESCRIPTOR*)((u8*)mmap+o); u64 s=d->PhysicalStart, e=s+d->NumberOfPages*4096; if(usable_after_exit(d->Type)&&s<=c&&e>c){ c=e; moved=1; } } if(!moved)break; }
        if(c>c0)cur=c; }
    if(!cur)cur=0x100000;
    u32 ram_mb=(u32)(cur>>20);
    print("  memory: "); printnum(ram_mb,10); print(" MB usable for the kernel heap and page pool\n");
    { const u64 R[5][2]={{0x5000,0x8000},{KERNEL_PHYS,KERNEL_PHYS+total},{0x9800000,0x9C80000},{0x10000000,kbss},{0x18000000,0x19000000}}; int warned=0;
      for(UINTN o=0;o<msz;o+=dsz){ EFI_MEMORY_DESCRIPTOR* d=(EFI_MEMORY_DESCRIPTOR*)((u8*)mmap+o); if(usable_after_exit(d->Type))continue; u64 s=d->PhysicalStart, e=s+d->NumberOfPages*4096;
          for(int r=0;r<5;r++) if(s<R[r][1]&&e>R[r][0]){ if(warned<4){ print("  warning: firmware "); print(memtype(d->Type)); print(" memory at "); printnum(s,16); print(" overlaps a kernel area\n"); } warned++; break; } }
      if(rsdp&&rsdp>=0x10000000&&rsdp<kbss){ print("  warning: ACPI tables lie inside the kernel's memory; power off may not work\n"); warned++; }
      if(warned){ print("  continuing in 5 seconds...\n"); BS->Stall(5000000); } }
    if(li&&li->ImageBase){ u64 ib=(u64)li->ImageBase, ie=ib+li->ImageSize; if(ib<KERNEL_PHYS+total&&ie>KERNEL_PHYS)return fail("the loader itself sits where the kernel must go",EFI_LOAD_ERROR); if(ib<0x8000&&ie>0x5000)return fail("the loader itself sits where the boot information must go",EFI_LOAD_ERROR); }
    { u8 probe; u64 sp=(u64)&probe; if(sp>=KERNEL_PHYS&&sp<KERNEL_PHYS+total+0x10000)return fail("the firmware stack sits where the kernel must go",EFI_LOAD_ERROR); }
    u8 blk[64]; for(int i=0;i<64;i++)blk[i]=0;
    *(u64*)(blk+0)=gop->Mode->FrameBufferBase; *(u32*)(blk+8)=mi->PixelsPerScanLine*4; *(u16*)(blk+12)=(u16)mi->HorizontalResolution; *(u16*)(blk+14)=(u16)mi->VerticalResolution;
    blk[32]=32; blk[33]=(u8)rgbfb; *(u32*)(blk+40)=ram_mb; *(u32*)(blk+44)=0x49464555u;
    u8 mbi[136]; for(int i=0;i<136;i++)mbi[i]=0;
    *(u64*)(mbi+88)=gop->Mode->FrameBufferBase; *(u32*)(mbi+96)=mi->PixelsPerScanLine*4; *(u32*)(mbi+100)=mi->HorizontalResolution; *(u32*)(mbi+104)=mi->VerticalResolution; mbi[108]=32;
    *(u64*)(mbi+120)=rsdp; *(u32*)(mbi+128)=rsdp?0x50445352u:0;
    { u8 probe; u64 sp=(u64)&probe, ib=li?(u64)li->ImageBase:0; if(sp>=(64ULL<<30)||ib>=(64ULL<<30)||!build_tables(gop->Mode->FrameBufferBase)){ g_pt=0; print("  note: keeping the firmware page tables\n"); } }
    u64 fbb=gop->Mode->FrameBufferBase; u32 fpitch=mi->PixelsPerScanLine*4, fh=mi->VerticalResolution;
    u8 edblk[8+2*24]; for(int i=0;i<(int)sizeof edblk;i++)edblk[i]=0; *(u32*)(edblk+0)=0x4432584Eu; u32 ndisp=0;
#ifndef UEFI_TEST
    { typedef EFI_STATUS (EFIAPI *lhb_t)(u32,EFI_GUID*,void*,UINTN*,EFI_HANDLE**); lhb_t LHB=(lhb_t)BS->LocateHandleBuffer;
      UINTN nh=0; EFI_HANDLE* hs=0;
      if(LHB&&LHB(2,&GOP_GUID,0,&nh,&hs)==0&&hs){
        for(UINTN i=0;i<nh&&ndisp<2;i++){
          EFI_GOP* g2=0; if(BS->HandleProtocol(hs[i],&GOP_GUID,(void**)&g2)||!g2||!g2->Mode||!g2->Mode->Info)continue;
          u64 b2=g2->Mode->FrameBufferBase; if(!b2||b2==fbb)continue;
          int dup=0; for(u32 k=0;k<ndisp;k++) if(*(u64*)(edblk+8+k*24)==b2){dup=1;break;} if(dup)continue;
          EFI_GOP_INFO* i2=g2->Mode->Info; int pf2=i2->PixelFormat; if(pf2!=0&&pf2!=1)continue;
          if(i2->HorizontalResolution>MAX_W||i2->VerticalResolution>MAX_H||!i2->HorizontalResolution||!i2->VerticalResolution)continue;
          int mapped; if(!g_pt)mapped=1; else if(b2<(32ULL<<30))mapped=1; else if(fbb>=(64ULL<<30)&&fbb<(512ULL<<30)){ u64 gi=fbb>>30; mapped=(b2>=(gi<<30)&&b2<((gi+2)<<30)); } else mapped=0;
          u8* r=edblk+8+ndisp*24; *(u64*)(r+0)=b2; *(u32*)(r+8)=i2->PixelsPerScanLine*4; *(u16*)(r+12)=(u16)i2->HorizontalResolution; *(u16*)(r+14)=(u16)i2->VerticalResolution; r[16]=32; r[17]=(u8)(pf2==0); r[18]=(u8)mapped;
          print("  second display: "); printnum(i2->HorizontalResolution,10); print("x"); printnum(i2->VerticalResolution,10); print(mapped?" (mirroring)\n":" (detected, firmware did not map it)\n"); ndisp++;
        } } }
#endif
    *(u32*)(edblk+4)=ndisp;
    print("  starting the kernel...\n");
    int tries=0;
    for(;;){ msz=mcap; st=BS->GetMemoryMap(&msz,mmap,&mkey,&dsz,&dver); if(st==0){ st=BS->ExitBootServices(image,mkey); if(st==0)break; } if(++tries>=8)return fail("the firmware refused to hand over the machine (ExitBootServices)",st); }
    mm_summarize(mmap,msz,dsz);
#ifndef UEFI_TEST
    __asm__ volatile("cli");
    if(g_pt)__asm__ volatile("mov %0,%%cr3"::"r"(g_pt):"memory");
    quiet_interrupts();
    quiet_dma();
#endif
    mark(fbb,fpitch,fh,0,0x00E03030u);
    kcopy(PHYS(KERNEL_PHYS),(u8*)kbuf,total);
    kcopy(PHYS(0x5000),blk,64); kcopy(PHYS(0x5040),edblk,sizeof edblk); kcopy(PHYS(0x6000),mbi,136); kcopy(PHYS(0x7000),g_mmsum,sizeof(g_mmsum));
    mark(fbb,fpitch,fh,1,0x00FF9020u);
#ifdef UEFI_TEST
    test_jump(uentry); return 0;
#else
    ((void(*)(void))uentry)();
    for(;;)__asm__ volatile("hlt");
#endif
}
