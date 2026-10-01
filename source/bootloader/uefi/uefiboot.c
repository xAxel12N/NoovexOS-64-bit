typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef unsigned long long UINTN; typedef u64 EFI_STATUS; typedef void* EFI_HANDLE;
typedef struct { u32 Data1; u16 Data2; u16 Data3; unsigned char Data4[8]; } EFI_GUID;

typedef struct {
    u32 Type; u32 Pad; u64 PhysicalStart; u64 VirtualStart; u64 NumberOfPages; u64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

struct EFI_SIMPLE_TEXT_OUTPUT { void* Reset; EFI_STATUS (*OutputString)(void*, u16*); };

typedef struct {
    u32 MaxMode, Mode; void* Info; UINTN SizeOfInfo; u64 FrameBufferBase; UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_MODE;
typedef struct {
    u32 Version, HorizontalResolution, VerticalResolution, PixelFormat;
    struct { u32 R,G,B,Rsvd; } PixelInfo; u32 PixelsPerScanLine;
} EFI_GOP_MODE_INFO;
struct EFI_GRAPHICS_OUTPUT {
    EFI_STATUS (*QueryMode)(void*, u32, UINTN*, EFI_GOP_MODE_INFO**);
    EFI_STATUS (*SetMode)(void*, u32);
    void* Blt;
    EFI_GRAPHICS_OUTPUT_MODE* Mode;
};

typedef struct {
    char _hdr[24];
    void* raise; void* restore;
    EFI_STATUS (*AllocatePages)(u32, u32, UINTN, u64*);
    void* FreePages;
    EFI_STATUS (*GetMemoryMap)(UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, u32*);
    EFI_STATUS (*AllocatePool)(u32, UINTN, void**);
    EFI_STATUS (*FreePool)(void*);
    void* pad2[9];
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID*, void**);
    void* pad3[2];
    EFI_STATUS (*LocateProtocol)(EFI_GUID*, void*, void**);
    void* pad4[2];
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE, UINTN);
} EFI_BOOT_SERVICES;

struct EFI_SIMPLE_FS;
typedef struct EFI_FILE {
    u64 Revision;
    EFI_STATUS (*Open)(struct EFI_FILE*, struct EFI_FILE**, u16*, u64, u64);
    EFI_STATUS (*Close)(struct EFI_FILE*);
    void* Delete;
    EFI_STATUS (*Read)(struct EFI_FILE*, UINTN*, void*);
    void* Write; void* GetPosition;
    EFI_STATUS (*SetPosition)(struct EFI_FILE*, u64);
    void* GetInfo;
} EFI_FILE;
struct EFI_SIMPLE_FS {
    u64 Revision;
    EFI_STATUS (*OpenVolume)(struct EFI_SIMPLE_FS*, EFI_FILE**);
};

typedef struct {
    char _hdr[24];
    u16* FirmwareVendor; u32 FirmwareRevision;
    void* ConsoleInHandle; void* ConIn;
    void* ConsoleOutHandle; struct EFI_SIMPLE_TEXT_OUTPUT* ConOut;
    void* StdErrHandle; void* StdErr;
    void* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    UINTN NumberOfTableEntries; void* ConfigurationTable;
} EFI_SYSTEM_TABLE;

static EFI_SYSTEM_TABLE* ST;
static void print(u16* s){ ST->ConOut->OutputString(ST->ConOut, s); }

/* GOP + SimpleFS GUIDs */
static EFI_GUID GOP_GUID = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static EFI_GUID SFS_GUID = {0x0964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

/* the kernel physical load address + the 0x5000 framebuffer handoff block */
#define KERNEL_PHYS 0x100000ULL
#define FB_BLOCK    0x5000ULL

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    ST = SystemTable;
    EFI_BOOT_SERVICES* BS = ST->BootServices;
    print((u16*)L"NoovexOS UEFI loader\r\n");

    /* 1. GOP: get the framebuffer */
    struct EFI_GRAPHICS_OUTPUT* gop = 0;
    if(BS->LocateProtocol(&GOP_GUID, 0, (void**)&gop) || !gop){ print((u16*)L"no GOP\r\n"); for(;;); }
    u64 fb   = gop->Mode->FrameBufferBase;
    u32 w    = gop->Mode->Info ? ((EFI_GOP_MODE_INFO*)gop->Mode->Info)->HorizontalResolution : 0;
    /* Mode->Info pointer is in the mode struct; read resolution + pitch from it */
    EFI_GOP_MODE_INFO* mi = (EFI_GOP_MODE_INFO*)gop->Mode->Info;
    u32 width  = mi->HorizontalResolution;
    u32 height = mi->VerticalResolution;
    u32 pitch  = mi->PixelsPerScanLine * 4;
    (void)w; (void)fb;

    /* 2. Build a Multiboot-info block at a low fixed address; boot64_mb.S reads
          the framebuffer fields at offsets 88.. and expects RDI=magic, RSI=&mbi. */
    unsigned char* mbi = (unsigned char*)0x6000ULL;
    for(int i=0;i<120;i++) mbi[i]=0;
    *(volatile u64*)(mbi+88) = gop->Mode->FrameBufferBase;   /* 88: fb addr (64-bit) */
    *(volatile u32*)(mbi+96) = pitch;                        /* 96: pitch */
    *(volatile u32*)(mbi+100)= width;                        /* 100: width */
    *(volatile u32*)(mbi+104)= height;                       /* 104: height */
    *(volatile u8*) (mbi+108)= 32;                           /* 108: bpp */

    /* 3. Load the kernel file KERNEL64.BIN from the ESP root */
    struct EFI_SIMPLE_FS* fs = 0;
    if(BS->LocateProtocol(&SFS_GUID, 0, (void**)&fs) || !fs){ print((u16*)L"no FS\r\n"); for(;;); }
    EFI_FILE* root = 0; if(fs->OpenVolume(fs, &root)){ print((u16*)L"no volume\r\n"); for(;;); }
    EFI_FILE* kf = 0;
    if(root->Open(root, &kf, (u16*)L"KERNEL64.BIN", 1, 0)){ print((u16*)L"no kernel file\r\n"); for(;;); }

    /* read in chunks straight to KERNEL_PHYS */
    unsigned char* dst = (unsigned char*)KERNEL_PHYS;
    UINTN total=0;
    for(;;){
        UINTN chunk = 1024*1024;
        if(kf->Read(kf, &chunk, dst+total)) break;
        if(chunk==0) break;
        total += chunk;
        if(chunk < 1024*1024) break;
    }
    kf->Close(kf);
    print((u16*)L"kernel loaded, finding UEFI entry...\r\n");

    /* find the NVX64SIG signature in the loaded kernel and read uefi_entry */
    u64 uentry = 0;
    {
        unsigned char* k = (unsigned char*)KERNEL_PHYS;
        for(UINTN i=0; i+8<total; i++){
            if(k[i]=='N'&&k[i+1]=='V'&&k[i+2]=='X'&&k[i+3]=='6'&&k[i+4]=='4'&&k[i+5]=='S'&&k[i+6]=='I'&&k[i+7]=='G'){
                /* layout: magic8 version16 epoch4 size4 sha256[32] builder16 uefi_entry8 */
                UINTN ue_off = i + 8 + 16 + 4 + 4 + 32 + 16;
                uentry = *(u64*)(k + ue_off);
                break;
            }
        }
    }
    if(!uentry){ print((u16*)L"no uefi_entry in kernel signature\r\n"); for(;;); }

    /* 4. Get memory map + ExitBootServices */
    UINTN msz=0, mkey=0, dsz=0; u32 dver=0;
    EFI_MEMORY_DESCRIPTOR* mmap=0;
    BS->GetMemoryMap(&msz, mmap, &mkey, &dsz, &dver);
    msz += 4096;
    BS->AllocatePool(2, msz, (void**)&mmap);
    BS->GetMemoryMap(&msz, mmap, &mkey, &dsz, &dver);
    BS->ExitBootServices(ImageHandle, mkey);

    /* 5. Jump to the kernel at 0x100000 (already in long mode). RDI=0 (no multiboot). */
    void (*kentry)(void) = (void(*)(void))uentry;
    kentry();
    for(;;);
    return 0;
}
