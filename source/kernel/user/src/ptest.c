#include "posix.h"
void onint(int s){ printf("caught signal %d\n", s); }
int main(int argc, char** argv){
    printf("POSIX v2 on NoovexOS  argc=%d\n", argc);
    for(int i=0;i<argc;i++) fprintf(stdout, "  argv[%d]=%s\n", i, argv[i]);
    char line[64]; snprintf(line, sizeof line, "pid=%d sum=%d", getpid(), 2+2);
    puts(line);
    signal(SIGINT, onint); raise(SIGINT);
    FILE* f = fopen("greet.txt","w");
    if(f){ fputs("hello from fopen\n", f); fclose(f); printf("wrote greet.txt\n"); }
    f = fopen("greet.txt","r");
    if(f){ char b[64]; if(fgets(b,64,f)) printf("read back: %s", b); fclose(f); }
    return 0;
}
