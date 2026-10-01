#include "posix.h"
static int cmpi(const void* a,const void* b,void* c){ (void)c; return *(const int*)a-*(const int*)b; }
int main(void){
    printf("uid=%d ppid=%d login=%s\n",getuid(),getppid(),getlogin());
    time_t now=time(0); printf("epoch=%ld  %s\n",(long)now,ctime(&now));
    struct timespec m; clock_gettime(CLOCK_MONOTONIC,&m); printf("uptime=%lds\n",m.tv_sec);
    FILE* f=fopen("notes.txt","w"); setvbuf(f,0,_IOFBF,256); fprintf(f,"line %d\n",1); fputs("line 2\n",f); fclose(f);
    printf("wrote notes.txt with a buffered stream\n");
    rename("notes.txt","renamed.txt"); struct stat st; printf("rename: old=%s new=%s\n",stat("notes.txt",&st)==0?"exists":"gone",stat("renamed.txt",&st)==0?"exists":"gone");
    truncate("renamed.txt",4); stat("renamed.txt",&st); printf("truncate -> %lu bytes\n",(unsigned long)st.st_size);
    copyfile("renamed.txt","copy.txt"); printf("copyfile: %s\n",stat("copy.txt",&st)==0?"ok":"failed");
    char t[]="tmp.XXXXXX"; int fd=mkstemp(t); printf("mkstemp -> %s fd=%d\n",t,fd); write(fd,"hi",2); fsync(fd); close(fd);
    FILE* tf=tmpfile(); fputs("scratch",tf); fclose(tf); printf("tmpfile closed and removed\n");
    char dt[]="dir.XXXXXX"; printf("mkdtemp -> %s\n",mkdtemp(dt)?dt:"failed");
    printf("rmtree(%s)=%d\n",dt,rmtree(dt));
    printf("remove: %d %d %d\n",remove("renamed.txt"),remove("copy.txt"),remove(t));
    int a[6]={9,2,7,1,8,3}; qsort_r(a,6,sizeof(int),cmpi,0); printf("heapsort: %d %d %d %d %d %d\n",a[0],a[1],a[2],a[3],a[4],a[5]);
    return 0;
}
