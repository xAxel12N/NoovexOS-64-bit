static volatile unsigned short* const VGA = (volatile unsigned short*)0xB8000;

static void clear(unsigned char attr){
    for(int i = 0; i < 80 * 25; i++) VGA[i] = ((unsigned short)attr << 8) | ' ';
}

static int puts_at(int pos, const char* s, unsigned char attr){
    while(*s){ VGA[pos++] = ((unsigned short)attr << 8) | (unsigned char)*s++; }
    return pos;
}

static int putu_at(int pos, unsigned int v, unsigned char attr){
    char buf[12];
    int n = 0;
    if(v == 0){ VGA[pos++] = ((unsigned short)attr << 8) | '0'; return pos; }
    while(v){ buf[n++] = (char)('0' + v % 10); v /= 10; }
    while(n > 0){ n--; VGA[pos++] = ((unsigned short)attr << 8) | (unsigned char)buf[n]; }
    return pos;
}

static void draw_framebuffer(unsigned long long mbi_addr){
    volatile unsigned char* mbi = (volatile unsigned char*)mbi_addr;
    unsigned long long fb = *(volatile unsigned long long*)(mbi_addr + 88);
    unsigned int pitch = *(volatile unsigned int*)(mbi_addr + 96);
    unsigned int w = *(volatile unsigned int*)(mbi_addr + 100);
    unsigned int h = *(volatile unsigned int*)(mbi_addr + 104);
    unsigned char bpp = *(volatile unsigned char*)(mbi_addr + 108);
    if(bpp != 32 || fb == 0 || w == 0 || h == 0) return;
    unsigned char rp = mbi[110], gp = mbi[112], bp = mbi[114];
    for(unsigned int y = 0; y < h; y++){
        volatile unsigned int* row = (volatile unsigned int*)(fb + (unsigned long long)y * pitch);
        unsigned int band = (y * 8) / h;
        unsigned char r = 0, g = 0, b = 0;
        if(band == 0){ r = 24; g = 26; b = 40; }
        else if(band == 1){ r = 222; g = 44; b = 44; }
        else if(band == 2){ r = 44; g = 200; b = 60; }
        else if(band == 3){ r = 60; g = 96; b = 232; }
        else if(band == 4){ r = 232; g = 210; b = 44; }
        else if(band == 5){ r = 44; g = 200; b = 214; }
        else if(band == 6){ r = 212; g = 60; b = 204; }
        else { r = 232; g = 232; b = 232; }
        unsigned int col = ((unsigned int)r << rp) | ((unsigned int)g << gp) | ((unsigned int)b << bp);
        for(unsigned int x = 0; x < w; x++) row[x] = col;
    }
    unsigned int cy = h / 2;
    for(unsigned int y = (cy > 24 ? cy - 24 : 0); y < cy + 24 && y < h; y++){
        volatile unsigned int* row = (volatile unsigned int*)(fb + (unsigned long long)y * pitch);
        for(unsigned int x = (w > 320 ? w / 2 - 160 : 0); x < w / 2 + 160 && x < w; x++) row[x] = 0;
    }
}

void kmain(unsigned long long magic, unsigned long long mbi_addr){
    clear(0x1F);
    if(magic == 0x2BADB002ULL)
        puts_at(80 * 2 + 2, "Booted by myboot in 64-bit long mode.", 0x1F);
    else
        puts_at(80 * 2 + 2, "Bad Multiboot magic!", 0x4F);

    unsigned int flags = *(volatile unsigned int*)(unsigned long)mbi_addr;
    if(flags & 0x01){
        unsigned int mem_upper = *(volatile unsigned int*)(unsigned long)(mbi_addr + 8);
        int p = puts_at(80 * 4 + 2, "Upper memory (KB): ", 0x17);
        putu_at(p, mem_upper, 0x17);
    }
    puts_at(80 * 6 + 2, "Kernel is alive in long mode. Halting.", 0x12);

    if(flags & 0x1000) draw_framebuffer(mbi_addr);
}
