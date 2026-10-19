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
}
