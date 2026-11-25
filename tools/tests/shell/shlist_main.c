#include <stdio.h>
#include <string.h>
#include "shell/shlist.c"
static int fails=0;
static void expect(const char* what,int ok){ if(!ok){ printf("FAIL: %s\n",what); fails++; } }
static int split(const char* line,char out[8][128],int ops[8]){
    char buf[256]; strncpy(buf,line,255); buf[255]=0; char* p=buf; int n=0;
    while(p&&n<8){ int op=0; int nx=sh_list_cut(p,&op); strncpy(out[n],p,127); out[n][127]=0; ops[n]=op; n++; p=nx>=0?p+nx:0; }
    return n;
}
int main(void){
    char parts[8][128]; int ops[8]; int n;
    n=split("echo a; echo b",parts,ops);
    expect("two commands on ;",n==2&&!strcmp(parts[0],"echo a")&&!strcmp(parts[1]," echo b")&&ops[0]==';'&&ops[1]==0);
    n=split("mkdir x && cd x && pwd",parts,ops);
    expect("three commands on &&",n==3&&!strcmp(parts[0],"mkdir x ")&&!strcmp(parts[1]," cd x ")&&!strcmp(parts[2]," pwd")&&ops[0]=='&'&&ops[1]=='&');
    n=split("false || echo no",parts,ops);
    expect("|| is a list operator",n==2&&ops[0]=='|'&&!strcmp(parts[1]," echo no"));
    n=split("ls | wc -l",parts,ops);
    expect("single | is a pipe, not a list",n==1&&!strcmp(parts[0],"ls | wc -l"));
    n=split("sleep 5 &",parts,ops);
    expect("trailing & stays with the command",n==1&&!strcmp(parts[0],"sleep 5 &"));
    n=split("cmd > out 2>&1",parts,ops);
    expect("2>&1 is not a list operator",n==1&&!strcmp(parts[0],"cmd > out 2>&1"));
    n=split("bash -c 'a && b; c' && echo done",parts,ops);
    expect("single quotes hide ; and &&",n==2&&!strcmp(parts[0],"bash -c 'a && b; c' ")&&!strcmp(parts[1]," echo done"));
    n=split("echo \"x; y\"; echo z",parts,ops);
    expect("double quotes hide ;",n==2&&!strcmp(parts[0],"echo \"x; y\"")&&!strcmp(parts[1]," echo z"));
    n=split("echo a\\; b; echo c",parts,ops);
    expect("backslash escapes ;",n==2&&!strcmp(parts[0],"echo a\\; b"));
    n=split("echo \"it's\"; echo ok",parts,ops);
    expect("apostrophe inside double quotes",n==2&&!strcmp(parts[1]," echo ok"));
    n=split("echo a;",parts,ops);
    expect("trailing ; leaves an empty tail",n==2&&parts[1][0]==0);
    expect("&& runs the next command only on success",!sh_list_skip('&',0)&&sh_list_skip('&',1));
    expect("|| runs the next command only on failure",sh_list_skip('|',0)&&!sh_list_skip('|',1));
    expect("; always runs the next command",!sh_list_skip(';',0)&&!sh_list_skip(';',1)&&!sh_list_skip(0,1));
    if(fails){ printf("shlist: %d failures\n",fails); return 1; }
    printf("shlist: all ok\n"); return 0;
}
