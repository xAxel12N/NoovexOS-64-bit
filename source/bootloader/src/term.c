#include "term.h"
#include "types.h"
#include "vga.h"
#include "kbd.h"
#include "fat.h"
#include "config.h"
#include "io.h"

static uint8_t catbuf[8192];

static int streq(const char* a, const char* b){
    while(*a && *b){ if(*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int parse_int(const char* s){
    int v = 0;
    while(*s >= '0' && *s <= '9'){ v = v * 10 + (*s - '0'); s++; }
    return v;
}

static void read_line(char* buf, int max){
    int n = 0;
    for(;;){
        int c = kbd_getchar();
        if(c == '\n'){ vga_putc('\n'); buf[n] = 0; return; }
        if(c == '\b'){ if(n > 0){ n--; vga_putc('\b'); } continue; }
        if(c >= ' ' && c < 127 && n < max - 1){ buf[n++] = (char)c; vga_putc((char)c); }
    }
}

static void reboot(void){
    uint8_t t;
    do { t = inb(0x64); if(t & 1) (void)inb(0x60); } while(t & 2);
    outb(0x64, 0xFE);
    for(;;) __asm__ volatile("hlt");
}

static void cmd_ls(void){
    char name[16];
    uint32_t size;
    int isd;
    int i = 0, any = 0;
    while(fat_readdir(i, name, &size, &isd) == 0){
        vga_puts("  ");
        vga_puts(name);
        if(isd) vga_puts("   <dir>");
        else { vga_puts("   "); vga_putdec(size); vga_puts(" bytes"); }
        vga_putc('\n');
        i++;
        any = 1;
        if(i > 256) break;
    }
    if(!any) vga_puts("  (no files)\n");
}

static void cmd_cat(const char* file){
    if(!file || !*file){ vga_puts("usage: cat <file>\n"); return; }
    uint32_t sz = 0;
    if(fat_read_file(file, catbuf, sizeof(catbuf) - 1, &sz) != 0){
        vga_puts("cannot read: ");
        vga_puts(file);
        vga_putc('\n');
        return;
    }
    for(uint32_t i = 0; i < sz; i++) vga_putc((char)catbuf[i]);
    if(sz == 0 || catbuf[sz - 1] != '\n') vga_putc('\n');
}

static void cmd_mem(void){
    volatile uint32_t* countp;
    uint8_t* ep;
    __asm__("" : "=r"(countp) : "0"((uint32_t)0x0FF0));
    __asm__("" : "=r"(ep) : "0"((uint32_t)0x1000));
    uint32_t count = *countp;
    if(count > 64) count = 64;
    vga_puts("  base        length      type\n");
    for(uint32_t i = 0; i < count; i++){
        uint8_t* e = ep + i * 24;
        uint32_t base_lo = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
        uint32_t len_lo = (uint32_t)e[8] | ((uint32_t)e[9] << 8) | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t type = (uint32_t)e[16] | ((uint32_t)e[17] << 8) | ((uint32_t)e[18] << 16) | ((uint32_t)e[19] << 24);
        vga_puts("  ");
        vga_puthex(base_lo);
        vga_puts("  ");
        vga_puthex(len_lo);
        vga_puts("  ");
        if(type == 1) vga_puts("usable");
        else if(type == 2) vga_puts("reserved");
        else if(type == 3) vga_puts("ACPI reclaim");
        else if(type == 4) vga_puts("ACPI NVS");
        else vga_puts("other");
        vga_putc('\n');
    }
    if(count == 0) vga_puts("  (no memory map)\n");
}

static void cmd_help(void){
    vga_puts("commands:\n");
    vga_puts("  help          show this help\n");
    vga_puts("  ls            list files on the boot partition\n");
    vga_puts("  cat <file>    print a file, e.g. cat boot.cfg\n");
    vga_puts("  mem           show the memory map\n");
    vga_puts("  boot [n]      boot menu entry n (default if omitted)\n");
    vga_puts("  clear         clear the screen\n");
    vga_puts("  reboot        restart the machine\n");
    vga_puts("  ver           show version\n");
    vga_puts("  exit          return to the boot menu\n");
}

int term_run(struct cfg* c){
    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_clear();
    vga_setcolor(VGA_CYAN, VGA_BLACK);
    vga_puts("myboot terminal\n");
    vga_setcolor(VGA_LGRAY, VGA_BLACK);
    vga_puts("Type 'help' for commands, 'exit' to return to the menu.\n\n");
    char line[128];
    for(;;){
        vga_setcolor(VGA_LGREEN, VGA_BLACK);
        vga_puts("myboot> ");
        vga_setcolor(VGA_LGRAY, VGA_BLACK);
        read_line(line, sizeof(line));
        char* p = line;
        while(*p == ' ') p++;
        char* cmd = p;
        while(*p && *p != ' ') p++;
        char* arg = 0;
        if(*p){ *p = 0; p++; while(*p == ' ') p++; arg = p; }
        if(cmd[0] == 0) continue;
        if(streq(cmd, "help")) cmd_help();
        else if(streq(cmd, "ls")) cmd_ls();
        else if(streq(cmd, "cat")) cmd_cat(arg);
        else if(streq(cmd, "mem")) cmd_mem();
        else if(streq(cmd, "clear")) vga_clear();
        else if(streq(cmd, "reboot")) reboot();
        else if(streq(cmd, "ver")) vga_puts("myboot 0.5\n");
        else if(streq(cmd, "exit")) return -1;
        else if(streq(cmd, "boot")){
            int idx = c->def;
            if(arg && *arg) idx = parse_int(arg) - 1;
            if(idx >= 0 && idx < c->count) return idx;
            vga_puts("no such entry\n");
        }
        else { vga_puts("unknown command: "); vga_puts(cmd); vga_puts("  (try 'help')\n"); }
    }
}
