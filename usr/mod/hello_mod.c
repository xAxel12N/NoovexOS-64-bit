extern int  nvx_puts(const char* s);
extern void nvx_putc(int c);
extern unsigned int nvx_ticks(void);
static const char* MSG = "Hello from a dynamically-loaded, relocated module!";
int module_main(void){
    nvx_puts(MSG);
    nvx_putc('\n');
    unsigned int t = nvx_ticks();
    nvx_puts("  (called kernel symbols by name after relocation)\n");
    return (int)(t & 0xff);
}
