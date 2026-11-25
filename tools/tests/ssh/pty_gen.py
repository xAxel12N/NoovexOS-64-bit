import re, sys
src, out = sys.argv[1], sys.argv[2]
s = open(src).read()
def grab(start):
    i = s.index(start); d = 0; j = s.index('{', i)
    while True:
        c = s[j]
        if c == '{': d += 1
        elif c == '}':
            d -= 1
            if d == 0: return s[i:j + 1]
        j += 1
parts = [re.search(r'struct lx_pty \{[^\n]*\};', s).group(0), grab('static int pty_index('), grab('static int pty_chown_path('), grab('static int pty_chmod_path('), grab('static int pty_may_open(')]
body = r'''#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#define PTY_MAX 32
#define LX_EPERM 1
#define LX_ENOENT 2
#define LX_EIO 5
#define LX_EACCES 13
static int g_uid, g_gid;
'''
body += parts[0] + '\nstatic struct lx_pty g_pty[PTY_MAX];\n' + '\n'.join(parts[1:]) + r'''
static int fails;
#define CHK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
int main(void){
    memset(g_pty,0,sizeof g_pty); g_pty[3].used=1; g_pty[3].ou=0; g_pty[3].og=5; g_pty[3].om=0620;
    CHK(pty_chown_path("/etc/passwd",1000,1000)==1);
    CHK(pty_chmod_path("/dev/pts",0600)==1);
    CHK(pty_chown_path("/dev/pts/4",1000,5)==-LX_ENOENT);
    g_uid=1000; g_gid=1000;
    CHK(pty_chown_path("/dev/pts/3",1000,5)==-LX_EPERM);
    CHK(pty_chmod_path("/dev/pts/3",0666)==-LX_EPERM);
    g_uid=0; g_gid=0;
    CHK(pty_chown_path("/dev/pts/3",1000,5)==0&&g_pty[3].ou==1000&&g_pty[3].og==5);
    CHK(pty_chmod_path("/dev/pts/3",0100620)==0&&g_pty[3].om==0620);
    g_uid=1001; g_gid=1001;
    CHK(pty_chown_path("/dev/pts/3",1001,-1)==-LX_EPERM&&g_pty[3].ou==1000);
    g_uid=1000; g_gid=1000;
    CHK(pty_chown_path("/dev/pts/3",0,-1)==-LX_EPERM);
    CHK(pty_chown_path("/dev/pts/3",-1,1000)==0&&g_pty[3].og==1000);
    CHK(pty_chown_path("/dev/pts/3",-1,7)==-LX_EPERM);
    CHK(pty_chmod_path("/dev/pts/3",0600)==0&&g_pty[3].om==0600);
    g_pty[3].locked=1; CHK(pty_chown_path("/dev/pts/3",1000,5)==-LX_ENOENT);
    CHK(pty_chown_path("/dev/pts/99",0,0)==1);
    g_pty[3].locked=0; g_pty[3].ou=0; g_pty[3].og=5; g_pty[3].om=0666;
    g_uid=1000; g_gid=5; CHK(pty_may_open(3)==-LX_EACCES);
    g_uid=0; CHK(pty_may_open(3)==0);
    g_pty[3].ou=1000; g_uid=1000; CHK(pty_may_open(3)==0);
    g_uid=1001; g_gid=1001; CHK(pty_may_open(3)==-LX_EACCES);
    CHK(pty_may_open(4)==-LX_EIO&&pty_may_open(-1)==-LX_EIO&&pty_may_open(PTY_MAX)==-LX_EIO);
    if(fails){ printf("%d failures\n",fails); return 1; }
    printf("pty owner: all ok\n"); return 0; }
'''
open(out, 'w').write(body)
