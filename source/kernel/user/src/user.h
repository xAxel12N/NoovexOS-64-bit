/* ===== NoovexOS user-space C library (libc) =====
   Freestanding, self-contained. Syscalls via INT 0x80 (number in rax; args
   rbx, rcx, rdx, rsi, rdi). Covers the parts real programs actually use:
   I/O, files, framebuffer, input, memory (malloc), strings, and printf. */
#ifndef NVX_USER_H
#define NVX_USER_H
typedef unsigned long size_t; typedef unsigned char uint8_t; typedef unsigned int uint32_t;

static inline long nvx_sys(long n,long a,long b,long c,long d,long e){
    long r; register long r8 asm("rsi")=d; register long r9 asm("rdi")=e;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"r"(r8),"r"(r9):"memory");
    return r;
}
/* ---- process / console ---- */
static inline void _exit(int code){ nvx_sys(0,code,0,0,0,0); }
static inline void sys_write(const char* s){ nvx_sys(1,(long)s,0,0,0,0); }
static inline void putchar(int c){ nvx_sys(3,c,0,0,0,0); }
static inline long fork(void){ return nvx_sys(2,0,0,0,0,0); }
static inline void yield(void){ nvx_sys(20,0,0,0,0,0); }
static inline long sysinfo(int what){ return nvx_sys(7,what,0,0,0,0); }
static inline long rand(void){ return nvx_sys(15,0,0,0,0,0); }
/* ---- files ---- */
static inline int open(const char* name,int create){ return (int)nvx_sys(30,(long)name,create,0,0,0); }
static inline int read(int fd,void* buf,int len){ return (int)nvx_sys(31,fd,(long)buf,len,0,0); }
static inline int write(int fd,const void* buf,int len){ return (int)nvx_sys(32,fd,(long)buf,len,0,0); }
static inline int close(int fd){ return (int)nvx_sys(33,fd,0,0,0,0); }
static inline int fsize(int fd){ return (int)nvx_sys(34,fd,0,0,0,0); }
static inline int readdir(int n,char* namebuf){ return (int)nvx_sys(35,n,(long)namebuf,0,0,0); }
/* ---- framebuffer ---- */
static inline int screen_w(void){ return (int)sysinfo(0); }
static inline int screen_h(void){ return (int)sysinfo(1); }
static inline void fb_clear(uint32_t color){ nvx_sys(40,color,0,0,0,0); }
static inline void fb_pixel(int x,int y,uint32_t color){ nvx_sys(41,x,y,color,0,0); }
static inline void fb_rect(int x,int y,int w,int h,uint32_t color){ nvx_sys(42,x,y,w,h,color); }
static inline void fb_text(int x,int y,const char* s,uint32_t color){ nvx_sys(44,x,y,(long)s,color,0); }
static inline void fb_present(void){ nvx_sys(45,0,0,0,0,0); }
static inline uint32_t rgb(int r,int g,int b){ return ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b; }
/* ---- input ---- */
static inline int getkey(void){ return (int)nvx_sys(50,0,0,0,0,0); }
static inline int waitkey(void){ return (int)nvx_sys(51,0,0,0,0,0); }
static inline void getmouse(int* x,int* y,int* btn){ nvx_sys(52,(long)x,(long)y,(long)btn,0,0); }
/* ---- time ---- */
static inline unsigned long ticks(void){ return (unsigned long)nvx_sys(70,0,0,0,0,0); }
static inline void sleep_ms(int ms){ nvx_sys(71,ms,0,0,0,0); }

/* ---- string / memory ---- */
static inline size_t strlen(const char* s){ size_t n=0; while(s[n])n++; return n; }
static inline void* memcpy(void* d,const void* s,size_t n){ char* a=d; const char* b=s; for(size_t i=0;i<n;i++)a[i]=b[i]; return d; }
static inline void* memset(void* d,int c,size_t n){ char* a=d; for(size_t i=0;i<n;i++)a[i]=(char)c; return d; }
static inline int strcmp(const char* a,const char* b){ while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
static inline char* strcpy(char* d,const char* s){ char* r=d; while((*d++=*s++)); return r; }
static inline int atoi(const char* s){ int n=0,neg=0; if(*s=='-'){neg=1;s++;} while(*s>='0'&&*s<='9')n=n*10+(*s++-'0'); return neg?-n:n; }

/* ---- more string / memory ---- */
static inline int strncmp(const char* a,const char* b,size_t n){ for(size_t i=0;i<n;i++){ if(a[i]!=b[i])return (unsigned char)a[i]-(unsigned char)b[i]; if(!a[i])return 0; } return 0; }
static inline char* strncpy(char* d,const char* s,size_t n){ size_t i=0; for(;i<n&&s[i];i++)d[i]=s[i]; for(;i<n;i++)d[i]=0; return d; }
static inline char* strcat(char* d,const char* s){ char* r=d; while(*d)d++; while((*d++=*s++)); return r; }
static inline char* strchr(const char* s,int c){ for(;*s;s++)if(*s==(char)c)return (char*)s; return c?0:(char*)s; }
static inline char* strstr(const char* h,const char* n){ if(!*n)return (char*)h; for(;*h;h++){ const char* a=h; const char* b=n; while(*a&&*b&&*a==*b){a++;b++;} if(!*b)return (char*)h; } return 0; }
static inline void* memmove(void* d,const void* s,size_t n){ char* a=d; const char* b=s; if(a<b){ for(size_t i=0;i<n;i++)a[i]=b[i]; } else { for(size_t i=n;i>0;i--)a[i-1]=b[i-1]; } return d; }
static inline int memcmp(const void* a,const void* b,size_t n){ const unsigned char* x=a,*y=b; for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i]; return 0; }
static inline int abs(int v){ return v<0?-v:v; }
static inline long labs(long v){ return v<0?-v:v; }
/* ---- snprintf (subset: %d %u %x %s %c) ---- */
static inline int snprintf(char* out,size_t cap,const char* f,...){ __builtin_va_list ap; __builtin_va_start(ap,f); size_t o=0;
    #define _PUT(ch) do{ if(o+1<cap)out[o]=(char)(ch); o++; }while(0)
    for(;*f;f++){ if(*f!='%'){ _PUT(*f); continue; } f++;
        if(*f=='d'||*f=='u'||*f=='x'){ long v; int base=(*f=='x')?16:10; int sign=(*f=='d'); unsigned long u;
            if(*f=='d'){ v=__builtin_va_arg(ap,int); if(v<0){_PUT('-');u=(unsigned long)(-v);}else u=(unsigned long)v; }
            else u=(unsigned long)__builtin_va_arg(ap,unsigned int);
            char b[24]; int i=0; if(u==0)b[i++]='0'; while(u){int d=u%base;b[i++]=(char)(d<10?'0'+d:'a'+d-10);u/=base;} while(i)_PUT(b[--i]); (void)sign; }
        else if(*f=='s'){ const char* s=__builtin_va_arg(ap,const char*); while(*s)_PUT(*s++); }
        else if(*f=='c')_PUT(__builtin_va_arg(ap,int));
        else if(*f=='%')_PUT('%'); else { _PUT('%'); _PUT(*f); } }
    if(cap)out[o<cap?o:cap-1]=0; __builtin_va_end(ap); return (int)o;
    #undef _PUT
}
static inline int sys_shm(int key,int size){ return (int)nvx_sys(80,key,size,0,0,0); }
static inline void* sys_shmmap(int id){ return (void*)nvx_sys(81,id,0,0,0,0); }
static inline int sys_msgsend(int q,const void* m,int len){ return (int)nvx_sys(82,q,(long)m,len,0,0); }
static inline int sys_msgrecv(int q,void* b,int cap){ return (int)nvx_sys(83,q,(long)b,cap,0,0); }
static inline void sys_scr_clear(unsigned c){ nvx_sys(90,c,0,0,0,0); }
static inline void sys_scr_blit(int key,int x,int y,int w,int h){ nvx_sys(91,key,x,y,w,h); }
static inline void sys_scr_rect(int x,int y,int w,int h,unsigned c){ nvx_sys(92,x,y,w,h,c); }
static inline void sys_scr_text(int x,int y,const char* s,unsigned c){ nvx_sys(93,x,y,(long)s,c,0); }
static inline void sys_scr_present(void){ nvx_sys(94,0,0,0,0,0); }
static inline int sys_wm_poll(void* m){ return (int)nvx_sys(95,(long)m,0,64,0,0); }
/* ---- POSIX fd layer (rung 1) ---- */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200
#define O_APPEND 0x400
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
static inline int px_open(const char* p,int flags){ return (int)nvx_sys(30,(long)p,flags,0,0,0); }
static inline int px_read(int fd,void* b,int n){ return (int)nvx_sys(31,fd,(long)b,n,0,0); }
static inline int px_write(int fd,const void* b,int n){ return (int)nvx_sys(32,fd,(long)b,n,0,0); }
static inline int px_close(int fd){ return (int)nvx_sys(33,fd,0,0,0,0); }
static inline int px_lseek(int fd,int off,int wh){ return (int)nvx_sys(36,fd,off,wh,0,0); }
static inline int px_dup(int fd){ return (int)nvx_sys(37,fd,0,0,0,0); }
static inline int px_fstat(int fd){ return (int)nvx_sys(34,fd,0,0,0,0); }
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
static inline void* px_mmap(void* a,unsigned long len,int prot,int flags,int fd){ return (void*)nvx_sys(9,(long)a,(long)len,prot,flags,fd); }
static inline int px_munmap(void* a,unsigned long len){ return (int)nvx_sys(11,(long)a,(long)len,0,0,0); }
static inline unsigned long px_brk(unsigned long addr){ return (unsigned long)nvx_sys(12,(long)addr,0,0,0,0); }
static inline int px_getpid(void){ return (int)nvx_sys(39,0,0,0,0,0); }
/* ---- threads (share the address space) ---- */
static inline int thread_create(void(*fn)(void)){ return (int)nvx_sys(61,(long)fn,0,0,0,0); }
/* ---- malloc (bump allocator over sbrk) ---- */
static inline void* sbrk(long inc){ return (void*)nvx_sys(60,inc,0,0,0,0); }
static char* _heap_ptr=0; static char* _heap_end=0;
static inline void* malloc(size_t n){ n=(n+15)&~15UL;
    if(!_heap_ptr){ _heap_ptr=(char*)sbrk(0); _heap_end=_heap_ptr; }
    if(_heap_ptr+n>_heap_end){ long need=(long)(n+65536); if(sbrk(need)==(void*)-1)return 0; _heap_end+=need; }
    void* p=_heap_ptr; _heap_ptr+=n; return p; }
static inline void free(void* p){ (void)p; }   /* bump allocator: no per-object free */

/* ---- printf (supports %d %u %x %s %c %%) ---- */
static inline void _putn(long v,int base,int sign){ char b[24]; int i=0; unsigned long u;
    if(sign&&v<0){ putchar('-'); u=(unsigned long)(-v); } else u=(unsigned long)v;
    if(u==0)b[i++]='0'; while(u){ int d=u%base; b[i++]=(char)(d<10?'0'+d:'a'+d-10); u/=base; }
    while(i)putchar(b[--i]); }
static inline void printf(const char* f,...){ __builtin_va_list ap; __builtin_va_start(ap,f);
    for(;*f;f++){ if(*f!='%'){ putchar(*f); continue; } f++;
        if(*f=='d')_putn(__builtin_va_arg(ap,int),10,1);
        else if(*f=='u')_putn((long)__builtin_va_arg(ap,unsigned int),10,0);
        else if(*f=='x')_putn((long)__builtin_va_arg(ap,unsigned int),16,0);
        else if(*f=='s'){ const char* s=__builtin_va_arg(ap,const char*); while(*s)putchar(*s++); }
        else if(*f=='c')putchar(__builtin_va_arg(ap,int));
        else if(*f=='%')putchar('%');
        else { putchar('%'); putchar(*f); } }
    __builtin_va_end(ap); }
#endif
