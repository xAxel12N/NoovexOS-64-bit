#include "types.h"
#include "vga.h"
#include "kbd.h"
#include "rtc.h"
#include "disk.h"
#include "fat.h"
#include "elf.h"
#include "elf64.h"
#include "multiboot.h"
#include "config.h"
#include "serial.h"
#include "vbe.h"
#include "fb.h"
#include "pit.h"
#include "term.h"

extern void mb_jump(uint32_t entry, uint32_t mbi);
extern void enter_long_mode(uint32_t entry_lo, uint32_t entry_hi, uint32_t mbi);
extern void raw_boot(uint32_t src, uint32_t size, uint32_t mbi);
extern uint8_t g_boot_drive;
extern void bios_get_font(void);

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static void cpuid_call(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d){
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static int cpuid_present(void){
    uint32_t r;
    __asm__ volatile(
        "pushfl\n\t"
        "popl %%eax\n\t"
        "movl %%eax, %%ecx\n\t"
        "xorl $0x200000, %%eax\n\t"
        "pushl %%eax\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %%eax\n\t"
        "xorl %%ecx, %%eax\n\t"
        "andl $0x200000, %%eax\n\t"
        : "=a"(r) : : "ecx", "cc");
    return r != 0;
}

static int cpu_supports_64(void){
    uint32_t a, b, c, d;
    if(!cpuid_present()) return 0;
    cpuid_call(1, &a, &b, &c, &d);
    if(!((d >> 6) & 1)) return 0;
    cpuid_call(0x80000000u, &a, &b, &c, &d);
    if(a < 0x80000001u) return 0;
    cpuid_call(0x80000001u, &a, &b, &c, &d);
    return (d >> 29) & 1;
}

static void utoa(uint32_t v, char* buf){
    char t[12];
    int i = 0;
    if(v == 0){ buf[0] = '0'; buf[1] = 0; return; }
    while(v){ t[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    while(i--) buf[j++] = t[i];
    buf[j] = 0;
}

static void fail(const char* msg){
    vga_setcolor(VGA_WHITE, VGA_RED);
    vga_puts("\nERROR: ");
    vga_puts(msg);
    vga_puts("\n");
    for(;;) __asm__ volatile("cli; hlt");
}

static void ok_line(const char* msg){
    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_puts("[ ");
    vga_setcolor(VGA_LGREEN, VGA_BLACK);
    vga_puts("OK");
    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_puts(" ] ");
    vga_puts(msg);
    vga_putc('\n');
}

static void boot_animation(struct cfg_entry* e, uint32_t ksz, int seconds){
    if(seconds <= 0) return;
    static const char* steps[] = {
        "Reserving low memory regions",
        "Enabling A20 gate",
        "Detecting CPU features",
        "Long mode (x86-64) supported",
        "Scanning memory map (E820)",
        "Building 4 GB page tables",
        "Installing GDT and segment selectors",
        "Mounting boot filesystem (FAT32)",
        "Reading kernel image from disk",
        "Validating ELF64 program headers",
        "Mapping kernel text segment",
        "Mapping kernel data segment",
        "Zeroing kernel BSS",
        "Assembling Multiboot information",
        "Initializing serial console (115200 8N1)",
        "Calibrating programmable interval timer",
        "Probing framebuffer",
        "Starting core services",
        "Mounting virtual filesystems",
        "Starting device manager",
        "Bringing scheduler online",
        "Warming instruction caches",
        "Finalizing boot parameters"
    };
    int nsteps = (int)(sizeof(steps) / sizeof(steps[0]));
    int total = nsteps + 4;
    uint32_t per = (uint32_t)seconds * 1000u / (uint32_t)total;

    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_clear();
    vga_setcolor(VGA_CYAN, VGA_BLACK);
    vga_puts("myboot :: starting ");
    vga_puts(e->title);
    vga_putc('\n');
    vga_setcolor(VGA_DGRAY, VGA_BLACK);
    vga_puts("--------------------------------------------------------------\n");
    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_puts("kernel  : "); vga_puts(e->kernel); vga_putc('\n'); pit_wait_ms(per);
    vga_puts("size    : "); vga_putdec(ksz); vga_puts(" bytes\n"); pit_wait_ms(per);
    vga_puts("cmdline : "); vga_puts(e->cmdline[0] ? e->cmdline : "(none)"); vga_putc('\n'); pit_wait_ms(per);
    vga_putc('\n');
    for(int i = 0; i < nsteps; i++){ ok_line(steps[i]); pit_wait_ms(per); }
    ok_line("Boot sequence complete");
    pit_wait_ms(per);
    vga_putc('\n');
}

static int s_box_x, s_count_row, s_cols;

static int slen(const char* s){ int n = 0; while(s[n]) n++; return n; }
static int centercol(int width){ int x = (s_cols - width) / 2; return x < 0 ? 0 : x; }

static void menu_countdown(int remaining){
    vga_fill_row(s_count_row, 0x00);
    if(remaining < 0) return;
    char num[12];
    utoa((uint32_t)remaining, num);
    char line[48];
    int p = 0;
    const char* a = "Booting in ";
    while(*a) line[p++] = *a++;
    char* n = num;
    while(*n) line[p++] = *n++;
    const char* b = " s";
    while(*b) line[p++] = *b++;
    line[p] = 0;
    vga_put_at(s_count_row, centercol(p), line, 0x08);
}

static void menu_draw(struct cfg* c, int sel, int remaining){
    int cols = vga_cols();
    int rows = vga_rows();
    s_cols = cols;
    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_clear();

    const char* t1 = "myboot 0.5";
    vga_put_at(1, centercol(slen(t1)), t1, 0x0B);
    const char* t2 = "Select an operating system to boot";
    vga_put_at(2, centercol(slen(t2)), t2, 0x08);

    int box_w = 56;
    if(box_w > cols - 8) box_w = cols - 8;
    int inner = box_w - 2;
    int box_h = c->count + 4;
    int box_x = (cols - box_w) / 2;
    int box_y = (rows - box_h) / 2 - 1;
    if(box_y < 4) box_y = 4;
    s_box_x = box_x;

    char buf[130];
    buf[0] = (char)0xDA;
    for(int i = 1; i < box_w - 1; i++) buf[i] = (char)0xC4;
    buf[box_w - 1] = (char)0xBF;
    buf[box_w] = 0;
    vga_put_at(box_y, box_x, buf, 0x08);

    buf[0] = (char)0xC0;
    buf[box_w - 1] = (char)0xD9;
    vga_put_at(box_y + box_h - 1, box_x, buf, 0x08);

    for(int r = box_y + 1; r < box_y + box_h - 1; r++){
        vga_put_at(r, box_x, "\xB3", 0x08);
        vga_put_at(r, box_x + box_w - 1, "\xB3", 0x08);
    }

    for(int i = 0; i < inner; i++) buf[i] = ' ';
    buf[inner] = 0;

    for(int i = 0; i < c->count; i++){
        int r = box_y + 2 + i;
        uint8_t attr = (i == sel) ? 0x1F : 0x0F;
        vga_put_at(r, box_x + 1, buf, attr);
        vga_put_at(r, box_x + 4, c->entries[i].title, attr);
    }

    int hy = box_y + box_h + 2;
    if(hy > rows - 3) hy = rows - 3;
    const char* help = "\x18/\x19 Select    Enter Boot    C Terminal    1-9 Pick";
    vga_put_at(hy, centercol(slen(help)), help, 0x07);
    s_count_row = hy + 2;
    if(remaining >= 0) menu_countdown(remaining);
}

static int menu_run(struct cfg* c){
    int sel = c->def;
    if(sel < 0 || sel >= c->count) sel = 0;
    int remaining = c->timeout;
    menu_draw(c, sel, remaining > 0 ? remaining : -1);
    uint8_t g = rtc_seconds();
    int changes = 0;
    while(changes < 2){
        kbd_flush();
        uint8_t n = rtc_seconds();
        if(n != g){ g = n; changes++; }
    }
    kbd_flush();
    int armed = 0;
    uint8_t last = rtc_seconds();
    for(;;){
        int k = kbd_poll();
        if(k == KEY_NONE) armed = 1;
        if(armed){
            if(k == KEY_UP){ sel = (sel - 1 + c->count) % c->count; remaining = -1; menu_draw(c, sel, -1); }
            else if(k == KEY_DOWN){ sel = (sel + 1) % c->count; remaining = -1; menu_draw(c, sel, -1); }
            else if(k == KEY_ENTER){ return sel; }
            else if(k >= '1' && k <= '9'){ int idx = k - '1'; if(idx < c->count) return idx; }
            else if(k == 'c'){
                int t = term_run(c);
                if(t >= 0) return t;
                menu_draw(c, sel, remaining > 0 ? remaining : -1);
                kbd_flush();
                armed = 0;
            }
        }
        if(remaining > 0){
            uint8_t now = rtc_seconds();
            if(now != last){
                last = now;
                remaining--;
                if(remaining <= 0) return sel;
                menu_countdown(remaining);
            }
        }
    }
}

void stage2_main(void){
    serial_init();
    vga_init();
    vga_setcolor(VGA_LGREEN, VGA_BLACK);
    vga_puts("myboot 0.3\n");
    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_puts("boot drive: ");
    vga_puthex(g_boot_drive);
    vga_putc('\n');

    static uint8_t mbr[512];
    if(disk_read(0, 1, mbr) != 0) fail("disk read (MBR) failed");

    uint32_t part_lba = 0;
    for(int i = 0; i < 4 && !part_lba; i++){
        uint8_t* pe = mbr + 0x1BE + i * 16;
        uint32_t lba = rd32(pe + 8);
        if(pe[0] == 0x80 && (pe[4] == 0x0B || pe[4] == 0x0C) && lba) part_lba = lba;
    }
    for(int i = 0; i < 4 && !part_lba; i++){
        uint8_t* pe = mbr + 0x1BE + i * 16;
        uint8_t type = pe[4];
        uint32_t lba = rd32(pe + 8);
        if((type == 0x0B || type == 0x0C) && lba) part_lba = lba;
    }
    if(!part_lba){
        for(int i = 0; i < 4; i++){
            uint32_t lba = rd32(mbr + 0x1BE + i * 16 + 8);
            if(lba){ part_lba = lba; break; }
        }
    }
    if(!part_lba) fail("no partition found in MBR");

    vga_puts("FAT partition LBA: ");
    vga_puthex(part_lba);
    vga_putc('\n');

    if(fat_mount(part_lba) != 0) fail("FAT32 mount failed");

    static char cfgbuf[8192];
    uint32_t cfgsz = 0;
    if(fat_read_file("/boot.cfg", (uint8_t*)cfgbuf, sizeof(cfgbuf) - 1, &cfgsz) != 0) fail("cannot read /boot.cfg");
    cfgbuf[cfgsz] = 0;

    static struct cfg cfg;
    cfg_parse(cfgbuf, &cfg);
    if(cfg.count == 0) fail("boot.cfg has no entries");

    vga_puts("config: entries=");
    vga_putdec((uint32_t)cfg.count);
    vga_puts(" timeout=");
    vga_putdec((uint32_t)cfg.timeout);
    vga_puts(" default=");
    vga_putdec((uint32_t)cfg.def);
    vga_putc('\n');
    if(cfg.timeout <= 0) vga_puts("menu: waiting for your selection (no auto-boot)\n");

    struct fb_info g_fb;
    int graphics = 0;
    if(cfg.video_w && cfg.video_h){
        bios_get_font();
        static const uint16_t fbk[][2] = {{1920,1080},{1600,900},{1366,768},{1280,1024},{1280,800},{1024,768},{800,600},{640,480}};
        int ok = 0;
        if(cfg.video_w == 0xFFFF){
            uint16_t nw = 0, nh = 0;
            if(vbe_edid_native(&nw, &nh) == 0 && nw <= 1920 && nh <= 1080) ok = (vbe_set_mode(nw, nh, &g_fb) == 0);
            cfg.video_w = 1920; cfg.video_h = 1080;
        }
        if(!ok) ok = (vbe_set_mode((uint16_t)cfg.video_w, (uint16_t)cfg.video_h, &g_fb) == 0);
        for(int i = 0; !ok && i < 8; i++)
            if(!(fbk[i][0] == cfg.video_w && fbk[i][1] == cfg.video_h))
                ok = (vbe_set_mode(fbk[i][0], fbk[i][1], &g_fb) == 0);
        if(ok){ fb_init(&g_fb); vga_set_graphics(1); graphics = 1; }
    }

    int sel = menu_run(&cfg);
    struct cfg_entry* e = &cfg.entries[sel];

    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_clear();

    uint8_t* scratch = (uint8_t*)0x18000000;
    uint32_t ksz = 0;
    if(fat_read_file(e->kernel, scratch, 0x6400000, &ksz) != 0) fail("cannot read kernel file");

    if(scratch[0] != 0x7F || scratch[1] != 'E' || scratch[2] != 'L' || scratch[3] != 'F'){
        /* flat binary (NoovexOS): show the boot animation, then Multiboot handoff with
           the framebuffer + the DOOM WAD as mods[0]. */
        boot_animation(e, ksz, cfg.bootlog);
        struct mb_info* mbi = mb_build_info(e->cmdline);
        if(graphics) mb_set_fb(mbi, &g_fb);
        { uint32_t wsz = 0;
          if(fat_read_file("/doom/DOOM1.WAD", (uint8_t*)0x0E000000, 32u*1024u*1024u, &wsz) == 0 && wsz > 1000000u)
              mb_set_module(mbi, 0x0E000000u, 0x0E000000u + wsz); }
        raw_boot((uint32_t)(uint32_t*)scratch, ksz, (uint32_t)(uint32_t*)mbi);
        fail("kernel returned");
    }

    boot_animation(e, ksz, cfg.bootlog);

    if(scratch[4] == 2){
        if(!cpu_supports_64()) fail("this CPU does not support 64-bit long mode");
        uint64_t entry64 = 0;
        if(elf64_load(scratch, ksz, &entry64) != 0) fail("not a valid 64-bit ELF kernel");
        struct mb_info* mbi = mb_build_info(e->cmdline);
        vga_puts("RAM:     ");
        vga_putdec((mbi->mem_upper >> 10) + 1);
        vga_puts(" MB\nMode:    64-bit long mode\nEntry:   ");
        vga_puthex((uint32_t)entry64);
        vga_puts("\nHanding off...\n");
        if(graphics) mb_set_fb(mbi, &g_fb);
        enter_long_mode((uint32_t)entry64, (uint32_t)(entry64 >> 32), (uint32_t)(uint32_t*)mbi);
        fail("kernel returned");
    } else {
        uint32_t entry = 0;
        if(mb_load_kernel(scratch, ksz, &entry) != 0) fail("not a valid multiboot kernel");
        struct mb_info* mbi = mb_build_info(e->cmdline);
        vga_puts("RAM:     ");
        vga_putdec((mbi->mem_upper >> 10) + 1);
        vga_puts(" MB\nMode:    32-bit protected mode\nEntry:   ");
        vga_puthex(entry);
        vga_puts("\nHanding off...\n");
        if(graphics) mb_set_fb(mbi, &g_fb);
        mb_jump(entry, (uint32_t)(uint32_t*)mbi);
        fail("kernel returned");
    }
}
