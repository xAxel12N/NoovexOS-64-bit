long __sys(long n,long a,long b,long c,long d,long e){
    long r; register long r8 __asm__("rsi")=d; register long r9 __asm__("rdi")=e;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"r"(r8),"r"(r9):"memory");
    return r;
}
