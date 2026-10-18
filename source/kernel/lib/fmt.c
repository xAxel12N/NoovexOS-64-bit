#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
char *e4(char*p,u32 r){ for(int j=0;j<4;j++)*p++=(char)(r>>(j*8)); return p; }
char *scopy(char*p,const char*s){ while(*s)*p++=*s++; return p; }
char *udec(char*p,u32 v){ char t[12];int n=0; if(!v){*p++='0';return p;} while(v){t[n++]='0'+v%10;v/=10;} while(n)*p++=t[--n]; return p; }
char *u2(char*p,u32 v){ if(v<10)*p++='0'; return udec(p,v); }
char *uhex(char*p,u64 v,int dg){ static const char*H="0123456789ABCDEF"; *p++='0';*p++='x'; for(int i=dg-1;i>=0;i--)*p++=H[(v>>(i*4))&0xF]; return p; }
char *hx(char*p,u32 v,int dg){ static const char*H="0123456789ABCDEF"; for(int i=dg-1;i>=0;i--)*p++=H[(v>>(i*4))&0xF]; return p; }
int seq(const char*a,const char*b){ while(*a&&*b){if(*a!=*b)return 0;a++;b++;} return *a==*b; }
