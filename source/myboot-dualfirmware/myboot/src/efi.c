typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef u64 UINTN;
typedef u64 EFI_STATUS;
typedef void* EFI_HANDLE;
typedef u16 CHAR16;
typedef u64 EFI_PHYSICAL_ADDRESS;

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL

typedef struct { u32 a; u16 b; u16 c; u8 d[8]; } EFI_GUID;

typedef struct {
    u64 Signature;
    u32 Revision;
    u32 HeaderSize;
    u32 CRC32;
    u32 Reserved;
} EFI_TABLE_HEADER;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, CHAR16*);
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    EFI_TEXT_STRING OutputString;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    u32 Type;
    u32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    u64 VirtualStart;
    u64 NumberOfPages;
    u64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void* RaiseTPL;
    void* RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(u32 Type, u32 MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS* Memory);
    void* FreePages;
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN* MapSize, EFI_MEMORY_DESCRIPTOR* Map, UINTN* MapKey, UINTN* DescSize, u32* DescVersion);
    EFI_STATUS (EFIAPI *AllocatePool)(u32 PoolType, UINTN Size, void** Buffer);
    EFI_STATUS (EFIAPI *FreePool)(void* Buffer);
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface);
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;
    EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE, EFI_GUID*, void**, EFI_HANDLE, EFI_HANDLE, u32);
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID* Protocol, void* Registration, void** Interface);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16* FirmwareVendor;
    u32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    void* StdErr;
    void* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    UINTN NumberOfTableEntries;
    void* ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    u32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    u32 LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    u64 ImageSize;
} EFI_LOADED_IMAGE_PROTOCOL;

struct EFI_FILE_PROTOCOL;
typedef struct EFI_FILE_PROTOCOL {
    u64 Revision;
    EFI_STATUS (EFIAPI *Open)(struct EFI_FILE_PROTOCOL*, struct EFI_FILE_PROTOCOL**, CHAR16*, u64, u64);
    EFI_STATUS (EFIAPI *Close)(struct EFI_FILE_PROTOCOL*);
    void* Delete;
    EFI_STATUS (EFIAPI *Read)(struct EFI_FILE_PROTOCOL*, UINTN*, void*);
    void* Write;
    void* GetPosition;
    EFI_STATUS (EFIAPI *SetPosition)(struct EFI_FILE_PROTOCOL*, u64);
    EFI_STATUS (EFIAPI *GetInfo)(struct EFI_FILE_PROTOCOL*, EFI_GUID*, UINTN*, void*);
    void* SetInfo;
    void* Flush;
} EFI_FILE_PROTOCOL;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    u64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*, EFI_FILE_PROTOCOL**);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    u32 Version;
    u32 HorizontalResolution;
    u32 VerticalResolution;
    u32 PixelFormat;
    u32 PixelInformation[4];
    u32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    u32 MaxMode;
    u32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void* QueryMode;
    void* SetMode;
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

struct mb_info {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4];
    u32 mmap_length, mmap_addr, drives_length, drives_addr, config_table, boot_loader_name, apm_table, vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    u64 framebuffer_addr;
    u32 framebuffer_pitch, framebuffer_width, framebuffer_height;
    u8 framebuffer_bpp, framebuffer_type;
    u8 fb_red_pos, fb_red_size, fb_green_pos, fb_green_size, fb_blue_pos, fb_blue_size;
} __attribute__((packed));

struct mb_mmap_entry {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;
} __attribute__((packed));


#define OFF(t,m) __builtin_offsetof(t,m)
_Static_assert(OFF(EFI_SYSTEM_TABLE, ConOut) == 64, "ConOut");
_Static_assert(OFF(EFI_SYSTEM_TABLE, BootServices) == 96, "BootServices");
_Static_assert(OFF(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, OutputString) == 8, "OutputString");
_Static_assert(OFF(EFI_BOOT_SERVICES, AllocatePages) == 40, "AllocatePages");
_Static_assert(OFF(EFI_BOOT_SERVICES, GetMemoryMap) == 56, "GetMemoryMap");
_Static_assert(OFF(EFI_BOOT_SERVICES, AllocatePool) == 64, "AllocatePool");
_Static_assert(OFF(EFI_BOOT_SERVICES, HandleProtocol) == 152, "HandleProtocol");
_Static_assert(OFF(EFI_BOOT_SERVICES, ExitBootServices) == 232, "ExitBootServices");
_Static_assert(OFF(EFI_BOOT_SERVICES, OpenProtocol) == 280, "OpenProtocol");
_Static_assert(OFF(EFI_BOOT_SERVICES, LocateProtocol) == 320, "LocateProtocol");
_Static_assert(OFF(EFI_LOADED_IMAGE_PROTOCOL, DeviceHandle) == 24, "DeviceHandle");
_Static_assert(OFF(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL, OpenVolume) == 8, "OpenVolume");
_Static_assert(OFF(EFI_FILE_PROTOCOL, Open) == 8, "Open");
_Static_assert(OFF(EFI_FILE_PROTOCOL, Read) == 32, "Read");
_Static_assert(OFF(EFI_FILE_PROTOCOL, SetPosition) == 56, "SetPosition");
_Static_assert(OFF(EFI_FILE_PROTOCOL, GetInfo) == 64, "GetInfo");
_Static_assert(OFF(EFI_GRAPHICS_OUTPUT_PROTOCOL, Mode) == 24, "GOP.Mode");
_Static_assert(OFF(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE, FrameBufferBase) == 24, "FrameBufferBase");
_Static_assert(OFF(struct mb_info, framebuffer_addr) == 88, "fb_addr");
_Static_assert(OFF(struct mb_info, framebuffer_pitch) == 96, "fb_pitch");
_Static_assert(OFF(struct mb_info, framebuffer_width) == 100, "fb_width");
_Static_assert(OFF(struct mb_info, framebuffer_height) == 104, "fb_height");
_Static_assert(OFF(struct mb_info, framebuffer_bpp) == 108, "fb_bpp");
_Static_assert(OFF(struct mb_info, framebuffer_type) == 109, "fb_type");
_Static_assert(sizeof(struct mb_mmap_entry) == 24, "mmap entry size");

static EFI_GUID GUID_LOADED_IMAGE = {0x5B1B31A1,0x9562,0x11d2,{0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};
static EFI_GUID GUID_SFS = {0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID GUID_FILE_INFO = {0x9576e92,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID GUID_GOP = {0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

static EFI_SYSTEM_TABLE* ST;
static EFI_BOOT_SERVICES* BS;

static void print(CHAR16* s){ if(ST && ST->ConOut) ST->ConOut->OutputString(ST->ConOut, s); }

static void print_hex(u64 v){
    CHAR16 buf[19];
    const CHAR16* d = u"0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for(int i = 0; i < 16; i++) buf[2 + i] = d[(v >> ((15 - i) * 4)) & 0xF];
    buf[18] = 0;
    print(buf);
}

static u32 rd32(const u8* p){ return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 rd16(const u8* p){ return (u16)(p[0] | (p[1] << 8)); }
static u64 rd64(const u8* p){ return (u64)rd32(p) | ((u64)rd32(p + 4) << 32); }

static void die(CHAR16* msg){
    print(u"myboot(UEFI) ERROR: ");
    print(msg);
    print(u"\r\n");
    for(;;) __asm__ volatile("hlt");
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
    ST = SystemTable;
    BS = SystemTable->BootServices;

    print(u"myboot UEFI loader\r\n");
    print(u"Opening boot filesystem...\r\n");

    EFI_LOADED_IMAGE_PROTOCOL* li = 0;
    if(BS->HandleProtocol(ImageHandle, &GUID_LOADED_IMAGE, (void**)&li) != EFI_SUCCESS || !li)
        die(u"loaded image protocol");

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = 0;
    if(BS->HandleProtocol(li->DeviceHandle, &GUID_SFS, (void**)&fs) != EFI_SUCCESS || !fs)
        die(u"filesystem protocol");

    EFI_FILE_PROTOCOL* root = 0;
    if(fs->OpenVolume(fs, &root) != EFI_SUCCESS || !root)
        die(u"open volume");

    EFI_FILE_PROTOCOL* kf = 0;
    if(root->Open(root, &kf, u"\\KERNEL.ELF", 1, 0) != EFI_SUCCESS || !kf)
        die(u"open \\KERNEL.ELF");

    print(u"Reading kernel image...\r\n");
    u8 infobuf[512];
    UINTN infosz = sizeof(infobuf);
    if(kf->GetInfo(kf, &GUID_FILE_INFO, &infosz, infobuf) != EFI_SUCCESS)
        die(u"file info");
    u64 filesize = rd64(infobuf + 8);
    if(filesize == 0 || filesize > 0x4000000ULL) die(u"kernel size");

    void* filebuf = 0;
    if(BS->AllocatePool(2, (UINTN)filesize, &filebuf) != EFI_SUCCESS || !filebuf)
        die(u"alloc file buffer");
    UINTN rd = (UINTN)filesize;
    if(kf->Read(kf, &rd, filebuf) != EFI_SUCCESS) die(u"read kernel");
    kf->Close(kf);

    u8* img = (u8*)filebuf;
    if(img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') die(u"not ELF");
    if(img[4] != 2) die(u"not ELF64");
    if(rd16(img + 18) != 0x3E) die(u"not x86-64");

    u64 entry = rd64(img + 24);
    u64 phoff = rd64(img + 32);
    u32 phentsz = rd16(img + 54);
    u32 phnum = rd16(img + 56);

    u64 lo = 0xFFFFFFFFFFFFFFFFULL, hi = 0;
    for(u32 i = 0; i < phnum; i++){
        u8* ph = img + phoff + (u64)i * phentsz;
        if(rd32(ph + 0) != 1) continue;
        u64 pa = rd64(ph + 24);
        u64 fs = rd64(ph + 32);
        u64 ms = rd64(ph + 40);
        if(fs == 0) continue;              /* skip pure-NOBITS (.bss): the kernel zeros it itself */
        if(pa < lo) lo = pa;
        if(pa + ms > hi) hi = pa + ms;     /* reserve only real-data segments (small) */
    }
    if(hi == 0) die(u"no PT_LOAD");
    u64 lo_page = lo & ~0xFFFULL;
    u64 hi_page = (hi + 0xFFF) & ~0xFFFULL;
    u64 pages = (hi_page - lo_page) / 0x1000;
    EFI_PHYSICAL_ADDRESS load_at = lo_page;
    if(BS->AllocatePages(2, 2, (UINTN)pages, &load_at) != EFI_SUCCESS)
        die(u"cannot reserve kernel load address");

    print(u"Loading segments at ");
    print_hex(lo_page);
    print(u"\r\n");
    for(u32 i = 0; i < phnum; i++){
        u8* ph = img + phoff + (u64)i * phentsz;
        if(rd32(ph + 0) != 1) continue;
        u64 off = rd64(ph + 8);
        u64 pa = rd64(ph + 24);
        u64 fs2 = rd64(ph + 32);
        u64 ms = rd64(ph + 40);
        if(fs2 == 0) continue;             /* NOBITS .bss handled by the kernel entry after ExitBootServices */
        u8* dst = (u8*)pa;
        for(u64 j = 0; j < fs2; j++) dst[j] = img[off + j];
        for(u64 j = fs2; j < ms; j++) dst[j] = 0;
    }

    struct mb_info* mbi = 0;
    EFI_PHYSICAL_ADDRESS infopage = 0xFFFFF000ULL;
    if(BS->AllocatePages(1, 2, 8, &infopage) != EFI_SUCCESS)
        die(u"alloc info page");
    u8* infomem = (u8*)infopage;
    for(int i = 0; i < 8 * 4096; i++) infomem[i] = 0;
    mbi = (struct mb_info*)infomem;
    struct mb_mmap_entry* mmap = (struct mb_mmap_entry*)(infomem + 512);
    char* loader_name = (char*)(infomem + 512 + 4096 * 4);
    char* cmdline = loader_name + 32;
    {
        const char* nm = "myboot";
        int i = 0;
        for(; nm[i]; i++) loader_name[i] = nm[i];
        loader_name[i] = 0;
        cmdline[0] = 0;
    }

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = 0;
    if(BS->LocateProtocol(&GUID_GOP, 0, (void**)&gop) == EFI_SUCCESS && gop && gop->Mode && gop->Mode->Info){
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mi = gop->Mode->Info;
        mbi->framebuffer_addr = gop->Mode->FrameBufferBase;
        mbi->framebuffer_pitch = mi->PixelsPerScanLine * 4;
        mbi->framebuffer_width = mi->HorizontalResolution;
        mbi->framebuffer_height = mi->VerticalResolution;
        mbi->framebuffer_bpp = 32;
        mbi->framebuffer_type = 1;
        if(mi->PixelFormat == 1){
            mbi->fb_red_pos = 16; mbi->fb_green_pos = 8; mbi->fb_blue_pos = 0;
        } else {
            mbi->fb_red_pos = 0; mbi->fb_green_pos = 8; mbi->fb_blue_pos = 16;
        }
        mbi->fb_red_size = mbi->fb_green_size = mbi->fb_blue_size = 8;
        mbi->flags |= 0x1000;
    }

    UINTN mapsize = 0, mapkey = 0, descsize = 0;
    u32 descver = 0;
    BS->GetMemoryMap(&mapsize, 0, &mapkey, &descsize, &descver);
    mapsize += 8 * descsize;
    void* mapbuf = 0;
    if(BS->AllocatePool(2, mapsize, &mapbuf) != EFI_SUCCESS) die(u"alloc mmap");

    UINTN msz = mapsize;
    if(BS->GetMemoryMap(&msz, (EFI_MEMORY_DESCRIPTOR*)mapbuf, &mapkey, &descsize, &descver) != EFI_SUCCESS)
        die(u"get memory map");

    u32 nent = 0;
    u64 mem_upper = 0;
    UINTN nd = msz / descsize;
    for(UINTN i = 0; i < nd && nent < 512; i++){
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)((u8*)mapbuf + i * descsize);
        u64 base = d->PhysicalStart;
        u64 len = d->NumberOfPages * 4096ULL;
        u32 t = d->Type;
        u32 mbtype = (t == 7 || t == 3 || t == 4 || t == 5 || t == 6 || t == 1 || t == 2) ? 1 : 2;
        if(t == 0 || t == 11 || t == 12 || t == 13) mbtype = 2;
        mmap[nent].size = 20;
        mmap[nent].addr = base;
        mmap[nent].len = len;
        mmap[nent].type = mbtype;
        if(mbtype == 1 && base <= 0x100000ULL && base + len > 0x100000ULL){
            u64 kb = (base + len - 0x100000ULL) >> 10;
            if(kb > mem_upper) mem_upper = kb;
        }
        nent++;
    }

    mbi->mem_lower = 640;
    mbi->mem_upper = (u32)mem_upper;
    mbi->flags |= 0x01;
    mbi->mmap_addr = (u32)(u64)mmap;
    mbi->mmap_length = nent * (u32)sizeof(struct mb_mmap_entry);
    mbi->flags |= 0x40;
    mbi->boot_loader_name = (u32)(u64)loader_name;
    mbi->flags |= 0x200;
    mbi->cmdline = (u32)(u64)cmdline;
    mbi->flags |= 0x04;

    print(u"Exiting boot services and starting kernel...\r\n");

    EFI_STATUS st = BS->ExitBootServices(ImageHandle, mapkey);
    if(st != EFI_SUCCESS){
        for(int tries = 0; tries < 8; tries++){
            msz = mapsize;
            if(BS->GetMemoryMap(&msz, (EFI_MEMORY_DESCRIPTOR*)mapbuf, &mapkey, &descsize, &descver) != EFI_SUCCESS) break;
            st = BS->ExitBootServices(ImageHandle, mapkey);
            if(st == EFI_SUCCESS) break;
        }
    }
    if(st != EFI_SUCCESS) die(u"ExitBootServices");

    u64 magic = 0x2BADB002ULL;
    u64 mbiaddr = (u64)mbi;
    __asm__ volatile(
        "movq %0, %%rdi\n"
        "movq %1, %%rsi\n"
        "movq %0, %%rax\n"
        "movq %1, %%rbx\n"
        "jmp *%2\n"
        :: "r"(magic), "r"(mbiaddr), "r"(entry)
        : "rax", "rbx", "rdi", "rsi", "memory");

    for(;;) __asm__ volatile("hlt");
    return EFI_SUCCESS;
}
