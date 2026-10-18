#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>
int main(int argc,char** argv){
    struct utsname u;
    if(argc>1&&!strcmp(argv[1],"x"))return 3;
    if(argc>1&&!strcmp(argv[1],"env")){ const char* v=getenv("MT_VAR"); printf("%s\n",v?v:"(unset)"); return v?4:5; }
    uname(&u);
    printf("Hello from musl libc on %s %s (%s)\n",u.sysname,u.release,u.machine);
    printf("argc=%d pi=%.5f\n",argc,3.14159265358979);
    return 0;
}
