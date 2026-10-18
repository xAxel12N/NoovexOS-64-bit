#include "include/nvx/base.h"
#include "include/nvx/lib/klibc.h"
/* ---- freestanding libc string/memory layer (standard names, for ported code) ---- */
unsigned long strlen(const char*s){ unsigned long n=0; while(s[n])n++; return n; }
int strcmp(const char*a,const char*b){ while(*a&&*a==*b){a++;b++;} return (int)(u8)*a-(int)(u8)*b; }
int strncmp(const char*a,const char*b,unsigned long n){ while(n&&*a&&*a==*b){a++;b++;n--;} if(!n)return 0; return (int)(u8)*a-(int)(u8)*b; }
char* strcpy(char*d,const char*s){ char*r=d; while((*d++=*s++)); return r; }
char* strncpy(char*d,const char*s,unsigned long n){ char*r=d; while(n&&(*d=*s)){d++;s++;n--;} while(n--)*d++=0; return r; }
char* strcat(char*d,const char*s){ char*r=d; while(*d)d++; while((*d++=*s++)); return r; }
char* strchr(const char*s,int c){ while(*s){ if(*s==(char)c)return (char*)s; s++; } return c?0:(char*)s; }
char* strrchr(const char*s,int c){ const char*r=0; do{ if(*s==(char)c)r=s; }while(*s++); return (char*)r; }
char* strstr(const char*h,const char*n){ if(!*n)return (char*)h; for(;*h;h++){ const char*a=h,*b=n; while(*a&&*b&&*a==*b){a++;b++;} if(!*b)return (char*)h; } return 0; }
void* memmove(void*d,const void*s,unsigned long n){ u8*pd=d; const u8*ps=s; if(pd<ps){ while(n--)*pd++=*ps++; } else { pd+=n; ps+=n; while(n--)*--pd=*--ps; } return d; }
int memcmp(const void*a,const void*b,unsigned long n){ const u8*x=a,*y=b; while(n--){ if(*x!=*y)return (int)*x-(int)*y; x++;y++; } return 0; }
void* memchr(const void*s,int c,unsigned long n){ const u8*p=s; while(n--){ if(*p==(u8)c)return (void*)p; p++; } return 0; }
int atoi(const char*s){ int r=0,neg=0; while(*s==' '||*s=='\t')s++; if(*s=='-'){neg=1;s++;}else if(*s=='+')s++; while(*s>='0'&&*s<='9'){ r=r*10+(*s-'0'); s++; } return neg?-r:r; }
int abs(int v){ return v<0?-v:v; }
